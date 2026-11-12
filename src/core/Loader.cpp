#include "Loader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dc {

QByteArray sha256File(const QString& path, qint64* size) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    if (size) *size = f.size();
    return h.result().toHex();
}

static bool isAscii(const QString& s) {
    for (QChar c : s)
        if (c.unicode() > 0x7e) return false;
    return true;
}

// An ASCII name dwgread (narrow fopen) can open `path` by: the path itself, or on Windows its
// short 8.3 form when the volume has them; empty when neither is ASCII.
static QString asciiAlias(const QString& path) {
    if (isAscii(QDir::toNativeSeparators(path))) return path;
#ifdef Q_OS_WIN
    const std::wstring w = QDir::toNativeSeparators(path).toStdWString();
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetShortPathNameW(w.c_str(), buf.data(), static_cast<DWORD>(buf.size()));
    if (n > 0 && n < buf.size()) {
        const QString s = QString::fromWCharArray(buf.data(), static_cast<qsizetype>(n));
        if (isAscii(s)) return QDir::fromNativeSeparators(s);
    }
#endif
    return {};
}

// Where to stage a file when the system's temporary folder is not ASCII either (a Windows user
// name with accents): a folder of the app's own under ProgramData. One per user (named by a
// hash of the home folder), so that it is the user's own and writable: a folder another
// account created first may not be. Empty if that fails too.
static QString asciiStagingRoot() {
    const QString user = QString::fromLatin1(
        QCryptographicHash::hash(QDir::homePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
#ifdef Q_OS_WIN
    const QString root = QDir::fromNativeSeparators(qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData"))) +
                         QStringLiteral("/DwgChecker/staging-") + user;
#else
    const QString root = QStringLiteral("/tmp/DwgChecker-staging-") + user;
#endif
    if (!isAscii(root) || !QDir().mkpath(root)) return {};
    return root;
}

static QString tail(const QByteArray& b, int n = 400) {
    QString s = QString::fromLocal8Bit(b).trimmed();
    if (s.size() > n) s = QStringLiteral("…") + s.right(n);
    return s;
}

DwgReadLoader::DwgReadLoader(LoaderSettings settings) : m_settings(std::move(settings)) {
    if (!m_settings.dwgreadPath.isEmpty()) m_version = probeVersion(m_settings.dwgreadPath, &m_versionError);
    else m_versionError = QStringLiteral("dwgread was not found");
}

bool DwgReadLoader::isUsable(QString* why) const {
    if (!m_version.isEmpty()) return true;
    if (why) *why = m_versionError;
    return false;
}

QString DwgReadLoader::probeVersion(const QString& exe, QString* error) {
    if (!QFileInfo::exists(exe)) {
        if (error) *error = QStringLiteral("dwgread not found at %1").arg(exe);
        return {};
    }
    QProcess p;
    p.start(exe, {QStringLiteral("--version")});
    if (!p.waitForStarted(10000) || !p.waitForFinished(15000)) {
        p.kill();
        p.waitForFinished(2000);
        if (error) *error = QStringLiteral("could not run %1 --version").arg(exe);
        return {};
    }
    const QString first = QString::fromLocal8Bit(p.readAllStandardOutput()).split('\n').value(0).trimmed();
    const QByteArray sha = sha256File(exe);
    return QStringLiteral("%1 (sha256 %2)").arg(first.isEmpty() ? QStringLiteral("dwgread ?") : first,
                                                QString::fromLatin1(sha.left(16)));
}

bool DwgReadLoader::runDwgRead(const QString& file, QByteArray* out, QString* error, QStringList* warnings) const {
    QProcess p;
    p.setProgram(m_settings.dwgreadPath);
    p.setArguments({QStringLiteral("-O"), QStringLiteral("minJSON"), QDir::toNativeSeparators(file)});
    p.start();
    if (!p.waitForStarted(15000)) {
        *error = QStringLiteral("could not start dwgread: %1").arg(p.errorString());
        return false;
    }
    const int timeoutMs = std::max(1, m_settings.timeoutSec) * 1000;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(5000);
        *error = QStringLiteral("dwgread timed out after %1 s").arg(m_settings.timeoutSec);
        return false;
    }
    *out = p.readAllStandardOutput();
    const QByteArray err = p.readAllStandardError();
    if (p.exitStatus() == QProcess::CrashExit) {
        *error = QStringLiteral("dwgread crashed: %1").arg(tail(err));
        return false;
    }
    if (out->trimmed().isEmpty()) {
        *error = QStringLiteral("dwgread produced no output (exit %1): %2").arg(p.exitCode()).arg(tail(err));
        return false;
    }
    if (p.exitCode() != 0)
        warnings->append(QStringLiteral("dwgread exited with code %1: %2").arg(p.exitCode()).arg(tail(err, 200)));
    return true;
}

bool DwgReadLoader::convertWithOda(const QString& file, QString* outFile, QString* error, const QString& workDir) const {
    // ODAFileConverter <in dir> <out dir> <version> <type> <recurse> <audit> [<filter>]
    const QString inDir = workDir + "/oda_in", outDir = workDir + "/oda_out";
    QDir().mkpath(inDir);
    QDir().mkpath(outDir);
    const QString name = QStringLiteral("input.dwg");
    QFile::remove(inDir + "/" + name);
    if (!QFile::copy(file, inDir + "/" + name)) {
        *error = QStringLiteral("could not stage file for ODA File Converter");
        return false;
    }
    QProcess p;
    p.start(m_settings.odaConverterPath,
            {QDir::toNativeSeparators(inDir), QDir::toNativeSeparators(outDir), QStringLiteral("ACAD2018"),
             QStringLiteral("DWG"), QStringLiteral("0"), QStringLiteral("1"), name});
    if (!p.waitForStarted(15000) || !p.waitForFinished(std::max(1, m_settings.timeoutSec) * 2000)) {
        p.kill();
        p.waitForFinished(5000);
        *error = QStringLiteral("ODA File Converter failed or timed out");
        return false;
    }
    *outFile = outDir + "/" + name;
    if (!QFileInfo::exists(*outFile)) {
        *error = QStringLiteral("ODA File Converter produced no output");
        return false;
    }
    return true;
}

LoadOutcome DwgReadLoader::load(const QString& path) const {
    LoadOutcome r;
    QString why;
    if (!isUsable(&why)) { r.error = why; return r; }
    if (!QFileInfo::exists(path)) { r.error = QStringLiteral("file not found"); return r; }

    QTemporaryDir tmp;
    std::unique_ptr<QTemporaryDir> staging;  // only when the system's temporary folder is not ASCII
    QString file = asciiAlias(path);
    if (file.isEmpty()) {
        // dwgread opens files with narrow fopen(); stage non-ASCII paths under an ASCII name, in
        // a folder whose own path is ASCII (D76).
        QString dir = tmp.isValid() ? asciiAlias(tmp.path()) : QString();
        if (dir.isEmpty()) {
            const QString root = asciiStagingRoot();
            if (!root.isEmpty()) {
                staging = std::make_unique<QTemporaryDir>(root + QStringLiteral("/dc-XXXXXX"));
                if (staging->isValid()) dir = staging->path();
            }
        }
        if (dir.isEmpty()) {
            r.error = QStringLiteral("the path has non-ASCII characters, which dwgread cannot open, and no folder with an "
                                     "ASCII path was found to copy it to; move the file to a folder such as C:\\Grading");
            return r;
        }
        file = dir + "/input.dwg";
        if (!QFile::copy(path, file)) { r.error = QStringLiteral("could not stage file with non-ASCII path"); return r; }
    }

    QByteArray out;
    QString err;
    bool ok = runDwgRead(file, &out, &err, &r.warnings);
    if (ok) {
        ok = r.json.parse(out, &err);
    }
    if (!ok && !m_settings.odaConverterPath.isEmpty() && tmp.isValid()) {
        QString converted, odaErr;
        const QString firstError = err;
        if (convertWithOda(path, &converted, &odaErr, tmp.path())) {
            out.clear();
            err.clear();
            ok = runDwgRead(converted, &out, &err, &r.warnings) && r.json.parse(out, &err);
            if (ok)
                r.warnings << QStringLiteral("dwgread could not read the file (%1); read a copy rewritten by ODA File "
                                             "Converter instead, so metadata may reflect the converter")
                                  .arg(firstError);
        } else {
            err = firstError + "; ODA fallback: " + odaErr;
        }
    }
    r.ok = ok;
    if (!ok) r.error = err;
    return r;
}

LoadOutcome JsonFileLoader::load(const QString& path) const {
    LoadOutcome r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { r.error = QStringLiteral("cannot open %1").arg(path); return r; }
    QString err;
    r.ok = r.json.parse(f.readAll(), &err);
    if (!r.ok) r.error = err;
    return r;
}

LoadOutcome AutoLoader::load(const QString& path) const {
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) return m_json.load(path);
    return m_dwg.load(path);
}

}  // namespace dc
