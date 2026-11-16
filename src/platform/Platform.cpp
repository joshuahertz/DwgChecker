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
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/DWG Checker/Opened in AutoCAD");
}

namespace {

// Removes a folder of copies: they are read-only, which Windows will not delete.
void removeCopies(const QString& dir) {
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) QFile::setPermissions(it.next(), QFile::ReadOwner | QFile::WriteOwner);
    QDir(dir).removeRecursively();
}

// Copies older than a week (one still open in AutoCAD cannot be removed, and stays), and the
// temp folder earlier versions used (D96).
void removeOldCopies() {
    const QDateTime weekAgo = QDateTime::currentDateTime().addDays(-7);
    for (const QFileInfo& d : QDir(evidenceFolder()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (d.lastModified() < weekAgo) removeCopies(d.absoluteFilePath());
    const QString old = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/DwgChecker-evidence");
    if (QFileInfo(old).isDir()) removeCopies(old);
}

}  // namespace

QString makeEvidenceCopy(const QString& file, QString* error) {
    removeOldCopies();
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(file);
        return {};
    }
    const QByteArray data = in.readAll();
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(12));
    const QString dir = evidenceFolder() + QLatin1Char('/') + sha;
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
