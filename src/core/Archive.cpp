#include "Archive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <QStringDecoder>

#include <zlib.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace dc {

namespace {

constexpr quint32 kLocalSig = 0x04034b50;
constexpr quint32 kCentralSig = 0x02014b50;
constexpr quint32 kEndSig = 0x06054b50;
constexpr quint32 kEnd64Sig = 0x06064b50;
constexpr quint32 kEnd64LocatorSig = 0x07064b50;
constexpr quint64 kMaxEntrySize = quint64(1) << 30;  // 1 GiB: no drawing comes near it
constexpr quint64 kMaxDirectorySize = quint64(256) << 20;

// Little-endian numbers; the caller checks that the bytes are there.
quint16 u16(const char* p) { return quint16(uchar(p[0]) | uchar(p[1]) << 8); }
quint32 u32(const char* p) { return quint32(u16(p)) | quint32(u16(p + 2)) << 16; }
quint64 u64(const char* p) { return quint64(u32(p)) | quint64(u32(p + 4)) << 32; }

// Code page 437, bytes 0x80 to 0xFF: the zip format's own encoding for names written
// without the UTF-8 flag (older Windows zips).
constexpr char16_t kCp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

// A name in the zip. With the UTF-8 flag it is UTF-8; without it, many tools still write
// UTF-8, so a name that reads as UTF-8 is taken as such, and any other is code page 437.
QString decodeName(const QByteArray& raw, bool utf8Flag) {
    QString name;
    if (utf8Flag) {
        name = QString::fromUtf8(raw);
    } else {
        QStringDecoder utf8(QStringDecoder::Utf8);
        name = utf8.decode(raw);
        if (utf8.hasError()) {
            name.clear();
            for (const char c : raw) name += QChar(uchar(c) < 0x80 ? char16_t(uchar(c)) : kCp437[uchar(c) - 0x80]);
        }
    }
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));  // some Windows tools write backslashes
    return name;
}

struct Entry {
    QString name;
    quint64 packedSize = 0, size = 0, offset = 0;
    quint32 crc = 0;
    quint16 method = 0, flags = 0;
};

// A zip's central directory, and its files read on request.
class Zip {
public:
    explicit Zip(const QString& path) : m_file(path) {}
    // Reads the directory; false, with error(), when the file is not a zip that can be read.
    bool open();
    QString error() const { return m_error; }
    const std::vector<Entry>& entries() const { return m_entries; }
    // An entry's bytes, unpacked and checked against its checksum.
    std::optional<QByteArray> read(const Entry& e, QString* why);

private:
    bool fail(const QString& why) {
        m_error = why;
        return false;
    }
    bool readAt(quint64 pos, qint64 len, QByteArray* out);

    QFile m_file;
    std::vector<Entry> m_entries;
    QString m_error;
};

bool Zip::readAt(quint64 pos, qint64 len, QByteArray* out) {
    if (pos > quint64(m_file.size()) || !m_file.seek(qint64(pos))) return false;
    *out = m_file.read(len);
    return out->size() == len;
}

bool Zip::open() {
    if (!m_file.open(QIODevice::ReadOnly)) return fail(m_file.errorString());
    const qint64 size = m_file.size();
    // The end record is 22 bytes plus a comment of at most 65535, at the very end. Its
    // signature counts only with exactly its comment after it: a file stored in the zip (a
    // zip inside it) can hold the same four bytes.
    const qint64 tailLen = std::min<qint64>(size, 22 + 65535);
    QByteArray tail;
    if (!readAt(quint64(size - tailLen), tailLen, &tail)) return fail(QStringLiteral("could not read the file"));
    qint64 end = -1;
    for (qint64 i = tail.size() - 22; i >= 0; --i)
        if (u32(tail.constData() + i) == kEndSig && i + 22 + u16(tail.constData() + i + 20) == tail.size()) {
            end = i;
            break;
        }
    if (end < 0) return fail(QStringLiteral("not a zip file"));
    const char* e = tail.constData() + end;
    quint64 count = u16(e + 10), dirSize = u32(e + 12), dirOffset = u32(e + 16);
    bool split = u16(e + 4) != 0 || u16(e + 6) != 0;
    if (count == 0xFFFF || dirSize == 0xFFFFFFFF || dirOffset == 0xFFFFFFFF) {  // Zip64
        if (end < 20 || u32(e - 20) != kEnd64LocatorSig) return fail(QStringLiteral("damaged zip (no Zip64 record)"));
        QByteArray rec;
        if (!readAt(u64(e - 20 + 8), 56, &rec) || u32(rec.constData()) != kEnd64Sig)
            return fail(QStringLiteral("damaged zip (Zip64 record)"));
        split = u32(rec.constData() + 16) != 0 || u32(rec.constData() + 20) != 0;
        count = u64(rec.constData() + 32);
        dirSize = u64(rec.constData() + 40);
        dirOffset = u64(rec.constData() + 48);
    }
    if (split) return fail(QStringLiteral("a zip split over several files"));
    if (dirSize > kMaxDirectorySize || dirOffset + dirSize > quint64(size))
        return fail(QStringLiteral("damaged zip (directory out of range)"));
    QByteArray dir;
    if (!readAt(dirOffset, qint64(dirSize), &dir)) return fail(QStringLiteral("could not read the zip's directory"));

    qint64 pos = 0;
    for (quint64 i = 0; i < count; ++i) {
        if (pos + 46 > dir.size() || u32(dir.constData() + pos) != kCentralSig)
            return fail(QStringLiteral("damaged zip (directory)"));
        const char* h = dir.constData() + pos;
        const int nameLen = u16(h + 28), extraLen = u16(h + 30), commentLen = u16(h + 32);
        if (pos + 46 + nameLen + extraLen + commentLen > dir.size()) return fail(QStringLiteral("damaged zip (directory)"));
        Entry en;
        en.flags = u16(h + 8);
        en.method = u16(h + 10);
        en.crc = u32(h + 16);
        en.packedSize = u32(h + 20);
        en.size = u32(h + 24);
        en.offset = u32(h + 42);
        en.name = decodeName(QByteArray(h + 46, nameLen), en.flags & 0x0800);
        // Zip64: the real sizes and offset follow, in this order, for each field that is
        // all ones above.
        const char* x = h + 46 + nameLen;
        const char* const xEnd = x + extraLen;
        while (x + 4 <= xEnd) {
            const quint16 id = u16(x), len = u16(x + 2);
            const char* q = x + 4;
            const char* const qEnd = q + len;
            if (qEnd > xEnd) break;
            if (id == 0x0001) {
                for (quint64* v : {&en.size, &en.packedSize, &en.offset})
                    if (*v == 0xFFFFFFFF && q + 8 <= qEnd) {
                        *v = u64(q);
                        q += 8;
                    }
            }
            x = qEnd;
        }
        m_entries.push_back(std::move(en));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

std::optional<QByteArray> Zip::read(const Entry& e, QString* why) {
    auto no = [why](const QString& w) {
        if (why) *why = w;
        return std::optional<QByteArray>();
    };
    if (e.flags & 0x0041) return no(QStringLiteral("password-protected"));
    if (e.method != 0 && e.method != 8)
        return no(QStringLiteral("packed with compression method %1, which cannot be unpacked here").arg(e.method));
    if (e.size > kMaxEntrySize || e.packedSize > kMaxEntrySize) return no(QStringLiteral("larger than 1 GB"));
    QByteArray head;
    if (!readAt(e.offset, 30, &head) || u32(head.constData()) != kLocalSig) return no(QStringLiteral("damaged zip (file header)"));
    // The local header's name and extra field may differ in length from the directory's.
    const quint64 start = e.offset + 30 + u16(head.constData() + 26) + u16(head.constData() + 28);
    QByteArray packed;
    if (!readAt(start, qint64(e.packedSize), &packed)) return no(QStringLiteral("damaged zip (file cut short)"));
    QByteArray data;
    if (e.method == 0) {
        if (e.packedSize != e.size) return no(QStringLiteral("damaged zip (sizes differ)"));
        data = std::move(packed);
    } else if (e.size > 0) {
        data = QByteArray(qsizetype(e.size), Qt::Uninitialized);
        z_stream zs{};
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return no(QStringLiteral("could not start unpacking"));
        zs.next_in = reinterpret_cast<Bytef*>(packed.data());
        zs.avail_in = uInt(packed.size());
        zs.next_out = reinterpret_cast<Bytef*>(data.data());
        zs.avail_out = uInt(data.size());
        const int rc = inflate(&zs, Z_FINISH);
        const uLong written = zs.total_out;
        inflateEnd(&zs);
        if (rc != Z_STREAM_END || written != e.size) return no(QStringLiteral("damaged zip (does not unpack)"));
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size()));
    if (quint32(crc) != e.crc) return no(QStringLiteral("damaged zip (checksum differs)"));
    return data;
}

bool isMacFork(const QString& name) {
    return name.startsWith(QStringLiteral("__MACOSX/")) || name.section(QLatin1Char('/'), -1).startsWith(QStringLiteral("._"));
}

// The submission drawings among the entries, as findSubmissionFiles picks them in a folder.
std::vector<int> pickSubmissions(const std::vector<Entry>& es, int* nestedZips) {
    std::vector<int> dwg, json;
    for (int i = 0; i < static_cast<int>(es.size()); ++i) {
        const QString& n = es[i].name;
        if (n.endsWith(QLatin1Char('/')) || isMacFork(n)) continue;
        if (n.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive)) {
            dwg.push_back(i);
        } else if (n.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
            if (!n.endsWith(QStringLiteral(".views.json"), Qt::CaseInsensitive)) json.push_back(i);
        } else if (n.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) && nestedZips) {
            ++*nestedZips;
        }
    }
    std::vector<int>& picked = dwg.empty() ? json : dwg;
    std::stable_sort(picked.begin(), picked.end(),
                     [&](int a, int b) { return es[a].name.compare(es[b].name, Qt::CaseInsensitive) < 0; });
    return picked;
}

// Where a name in the zip goes under the destination folder: never outside it, and never a
// name Windows refuses.
QString safeRelativePath(const QString& name) {
    static const QRegularExpression reserved(QStringLiteral(R"(^(con|prn|aux|nul|com\d|lpt\d)(\..*)?$)"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QString refused = QStringLiteral("<>:\"|?*");
    QStringList parts;
    for (QString part : name.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part == QStringLiteral(".") || part == QStringLiteral("..")) continue;
        for (QChar& c : part)
            if (c.unicode() < 0x20 || refused.contains(c)) c = QLatin1Char('_');
        while (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' '))) part.chop(1);  // Windows drops them
        if (part.isEmpty()) continue;
        if (reserved.match(part).hasMatch()) part.prepend(QLatin1Char('_'));
        parts << part;
    }
    return parts.join(QLatin1Char('/'));
}

// `rel`, or "name (2).dwg" and so on when a file of that name (ignoring case, as Windows
// and macOS do) is already there.
QString uniquePath(const QString& rel, QSet<QString>& used) {
    QString out = rel;
    const int slash = static_cast<int>(rel.lastIndexOf(QLatin1Char('/')));
    const int dot = static_cast<int>(rel.lastIndexOf(QLatin1Char('.')));
    const int at = dot > slash + 1 ? dot : static_cast<int>(rel.size());
    for (int k = 2; used.contains(out.toLower()); ++k) out = rel.left(at) + QStringLiteral(" (%1)").arg(k) + rel.mid(at);
    used.insert(out.toLower());
    return out;
}

}  // namespace

bool isZipPath(const QString& path) { return path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive); }

ZipListing listZipSubmissions(const QString& zipPath) {
    ZipListing out;
    Zip zip(zipPath);
    if (!zip.open()) {
        out.error = zip.error();
        return out;
    }
    for (const int i : pickSubmissions(zip.entries(), &out.nestedZips)) out.names << zip.entries()[i].name;
    return out;
}

ZipUnpacked unpackZipSubmissions(const QString& zipPath, const QString& destDir, const std::atomic_bool* cancel,
                                 const std::function<void(int done, int total)>& progress) {
    ZipUnpacked out;
    Zip zip(zipPath);
    if (!zip.open()) {
        out.error = zip.error();
        return out;
    }
    const std::vector<int> picked = pickSubmissions(zip.entries(), nullptr);
    const int total = static_cast<int>(picked.size());
    // Room for them all, before anything is written (D88): a large or damaged zip must not fill
    // the drive. The sizes are the zip's own; each is checked again as it is unpacked.
    if (!QDir().mkpath(destDir)) {
        out.error = QStringLiteral("could not make the folder %1").arg(destDir);
        return out;
    }
    quint64 needed = 0;
    for (const int i : picked) needed += zip.entries()[i].size;
    const QStorageInfo drive(destDir);
    if (drive.isValid() && drive.bytesAvailable() >= 0 && needed > quint64(drive.bytesAvailable())) {
        out.error = QStringLiteral("its drawings need %1 MB, and the drive of %2 has %3 MB free")
                        .arg(QString::number((needed >> 20) + 1), destDir, QString::number(drive.bytesAvailable() >> 20));
        return out;
    }
    const QDir dest(destDir);
    QSet<QString> used;
    for (int k = 0; k < total; ++k) {
        if (cancel && cancel->load()) {
            out.cancelled = true;
            break;
        }
        if (progress) progress(k, total);
        const Entry& e = zip.entries()[picked[k]];
        const QString rel = safeRelativePath(e.name);
        if (rel.isEmpty()) {
            out.problems << QStringLiteral("%1: no usable file name").arg(e.name);
            continue;
        }
        QString why;
        const std::optional<QByteArray> data = zip.read(e, &why);
        if (!data) {
            out.problems << QStringLiteral("%1: %2").arg(e.name, why);
            continue;
        }
        const QString target = dest.absoluteFilePath(uniquePath(rel, used));
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            out.problems << QStringLiteral("%1: could not make the folder %2").arg(e.name, QFileInfo(target).absolutePath());
            continue;
        }
        QFile f(target);
        if (!f.open(QIODevice::WriteOnly) || f.write(*data) != data->size() || !f.flush()) {
            out.problems << QStringLiteral("%1: could not write it: %2").arg(e.name, f.errorString());
            f.remove();  // no half-written file left behind
            continue;
        }
        f.close();
        out.files << target;
    }
    if (progress && !out.cancelled) progress(total, total);
    return out;
}

}  // namespace dc
