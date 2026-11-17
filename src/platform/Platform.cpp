#include "Platform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <algorithm>

namespace dc::platform {

QString findDwgRead() {
    const QString bundled = bundledDwgReadPath();
    if (!bundled.isEmpty() && QFileInfo(bundled).isFile()) return bundled;
#ifdef Q_OS_WIN
    const QString exeName = QStringLiteral("dwgread.exe");
#else
    const QString exeName = QStringLiteral("dwgread");
#endif
    if (QCoreApplication::instance()) {
        const QString beside = QCoreApplication::applicationDirPath() + "/" + exeName;
        if (QFileInfo(beside).isFile()) return beside;
    }
    // Developer fallbacks for a build made without a dwgread to bundle.
    const QString env = qEnvironmentVariable("DWGCHECK_DWGREAD");
    if (!env.isEmpty() && QFileInfo(env).isFile()) return env;
    return QStandardPaths::findExecutable(QStringLiteral("dwgread"));
}

QString evidenceFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/DWG Checker/Temp");
}

QStringList earlierEvidenceFolders() {
    return {QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/DWG Checker/Opened in AutoCAD"),
            QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/DwgChecker-evidence")};
}

namespace {

// The name of a copy's folder: the first 12 hex digits of its SHA-256.
bool isCopyFolderName(const QString& name) {
    if (name.size() != 12) return false;
    return std::all_of(name.cbegin(), name.cend(), [](QChar c) { return c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f')); });
}

QString sha256Hex(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return {};
    return QString::fromLatin1(h.result().toHex());
}

}  // namespace

void removeCopies(const QString& root) {
    for (const QFileInfo& d : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (!isCopyFolderName(d.fileName())) continue;  // not a folder the app made
        for (const QFileInfo& f : QDir(d.absoluteFilePath()).entryInfoList(QDir::Files | QDir::Hidden | QDir::System)) {
            if (!sha256Hex(f.absoluteFilePath()).startsWith(d.fileName())) continue;  // not its copy: the user's
            // Read-only, which Windows will not delete; one still open in AutoCAD stays.
            QFile::setPermissions(f.absoluteFilePath(), QFile::ReadOwner | QFile::WriteOwner);
            if (!QFile::remove(f.absoluteFilePath()))
                QFile::setPermissions(f.absoluteFilePath(), QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup | QFile::ReadOther);
        }
        QDir(root).rmdir(d.fileName());  // only when empty
    }
    // The folder itself, then its "DWG Checker" parent, when nothing is left in them.
    const QFileInfo r(root);
    if (QDir(r.absolutePath()).rmdir(r.fileName()) && QFileInfo(r.absolutePath()).fileName() == QStringLiteral("DWG Checker")) {
        const QFileInfo parent(r.absolutePath());
        QDir(parent.absolutePath()).rmdir(parent.fileName());
    }
}

QString makeEvidenceCopy(const QString& file, QString* error, const QString& root) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(file);
        return {};
    }
    const QByteArray data = in.readAll();
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(12));
    const QString dir = root + QLatin1Char('/') + sha;
    if (!QDir().mkpath(dir)) {
        if (error) *error = QStringLiteral("cannot make the folder %1").arg(QDir::toNativeSeparators(dir));
        return {};
    }
    const QString out = dir + "/" + QFileInfo(file).fileName();
    if (QFileInfo::exists(out)) {
        QFile existing(out);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == data) return out;
        existing.close();
        QFile::setPermissions(out, QFile::ReadOwner | QFile::WriteOwner);
        QFile::remove(out);
    }
    QFile o(out);
    if (!o.open(QIODevice::WriteOnly) || o.write(data) != data.size()) {
        if (error) *error = QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(out));
        return {};
    }
    o.close();
    QFile::setPermissions(out, QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup | QFile::ReadOther);
    return out;
}

}  // namespace dc::platform

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// Other platforms (Linux CI): no AutoCAD; dwgread is found next to the binary or on PATH.
namespace dc::platform {
namespace {
class NullLauncher : public IAutoCadLauncher {
public:
    QString runningAutoCad() const override { return {}; }
    bool openInRunning(const QString&, QString* error) override {
        if (error) *error = QStringLiteral("AutoCAD is not available on this platform");
        return false;
    }
};
}  // namespace

std::unique_ptr<IAutoCadLauncher> createAutoCadLauncher() { return std::make_unique<NullLauncher>(); }
QString bundledDwgReadPath() {
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() + QStringLiteral("/dwgread") : QString();
}

}  // namespace dc::platform
#endif
