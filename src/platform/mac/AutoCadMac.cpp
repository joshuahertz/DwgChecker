// macOS: AutoCAD bundles live under /Applications/Autodesk/ (for example
// /Applications/Autodesk/AutoCAD 2024/AutoCAD 2024.app). `open -a <bundle> <file>`
// reuses a running instance automatically.

#include "../Platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

namespace dc::platform {

QString bundledDwgReadPath() {
    if (!QCoreApplication::instance()) return {};
    // .../DwgChecker.app/Contents/MacOS/DwgChecker -> .../Contents/Helpers/dwgread
    return QDir(QCoreApplication::applicationDirPath() + "/../Helpers").absoluteFilePath(QStringLiteral("dwgread"));
}

namespace {

AutoCadInstall fromBundle(const QString& path) {
    AutoCadInstall i;
    i.exe = path;
    i.name = QFileInfo(path).completeBaseName();
    i.lt = i.name.contains(QStringLiteral("LT"));
    const auto m = QRegularExpression(QStringLiteral("(20\\d\\d)")).match(i.name);
    i.year = m.hasMatch() ? m.captured(1) : QString();
    i.release = i.year;
    return i;
}

class MacLauncher : public IAutoCadLauncher {
public:
    QList<AutoCadInstall> detect() const override {
        QList<AutoCadInstall> out;
        QDirIterator it(QStringLiteral("/Applications/Autodesk"), {QStringLiteral("AutoCAD*.app")}, QDir::Dirs,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (p.count(QStringLiteral(".app")) > 1) continue;  // nested helper apps inside a bundle
            out << fromBundle(p);
        }
        std::sort(out.begin(), out.end(), [](const AutoCadInstall& a, const AutoCadInstall& b) {
            if (a.year != b.year) return a.year > b.year;
            return !a.lt && b.lt;
        });
        return out;
    }

    AutoCadInstall fromUserPath(const QString& path) const override { return fromBundle(path); }

    bool open(const AutoCadInstall& inst, const QString& file, QString* error) override {
        if (!QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), inst.exe, file})) {
            if (error) *error = QStringLiteral("could not run open -a %1").arg(inst.exe);
            return false;
        }
        return true;
    }
};

}  // namespace

std::unique_ptr<IAutoCadLauncher> createAutoCadLauncher() { return std::make_unique<MacLauncher>(); }

}  // namespace dc::platform
