#include "Platform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
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

QString makeEvidenceCopy(const QString& file, QString* error) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(file);
        return {};
    }
    const QByteArray data = in.readAll();
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(12));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/DwgChecker-evidence/" + sha;
    QDir().mkpath(dir);
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
        if (error) *error = QStringLiteral("cannot write %1").arg(out);
        return {};
    }
    o.close();
    QFile::setPermissions(out, QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup | QFile::ReadOther);
    return out;
}

}  // namespace dc::platform

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// Other platforms (Linux CI): no AutoCAD; dwgread is found next to the binary or on PATH.
#include <QProcess>
namespace dc::platform {
namespace {
class NullLauncher : public IAutoCadLauncher {
public:
    QList<AutoCadInstall> detect() const override { return {}; }
    bool open(const AutoCadInstall&, const QString&, QString* error) override {
        if (error) *error = QStringLiteral("AutoCAD is not available on this platform");
        return false;
    }
    AutoCadInstall fromUserPath(const QString& path) const override {
        AutoCadInstall i;
        i.exe = path;
        i.name = QFileInfo(path).fileName();
        return i;
    }
};
}  // namespace
std::unique_ptr<IAutoCadLauncher> createAutoCadLauncher() { return std::make_unique<NullLauncher>(); }
QString bundledDwgReadPath() {
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() + "/dwgread" : QString();
}
}  // namespace dc::platform
#endif
