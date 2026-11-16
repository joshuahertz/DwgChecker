// macOS: open a file in the full AutoCAD that is already running (D96; not AutoCAD LT).
// AutoCAD bundles live under /Applications/Autodesk (for example /Applications/Autodesk/
// AutoCAD 2024/AutoCAD 2024.app); the running one is found from the process list, and
// `open -a <bundle> <file>` hands it the file. The app never starts AutoCAD: `open -a` is
// used only once one runs.

#include "../Platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace dc::platform {

QString bundledDwgReadPath() {
    if (!QCoreApplication::instance()) return {};
    // .../DwgChecker.app/Contents/MacOS/DwgChecker -> .../Contents/Helpers/dwgread
    return QDir(QCoreApplication::applicationDirPath() + "/../Helpers").absoluteFilePath(QStringLiteral("dwgread"));
}

namespace {

// The bundle of a running full AutoCAD: a process whose executable is
// /Applications/Autodesk/.../AutoCAD*.app/Contents/MacOS/...; empty when none runs. AutoCAD
// LT is not supported (D96), as on Windows, and is passed over.
QString runningBundle() {
    QProcess ps;
    ps.start(QStringLiteral("/bin/ps"), {QStringLiteral("-axo"), QStringLiteral("command=")});
    if (!ps.waitForFinished(3000)) return {};
    static const QString marker = QStringLiteral(".app/Contents/MacOS/");
    for (const QString& line : QString::fromLocal8Bit(ps.readAllStandardOutput()).split(QLatin1Char('\n'))) {
        const int at = static_cast<int>(line.indexOf(marker));
        if (at < 0 || !line.startsWith(QStringLiteral("/Applications/Autodesk/"))) continue;
        const QString bundle = line.left(at + 4);  // ".../AutoCAD 2024.app"
        const QString name = QFileInfo(bundle).fileName();
        if (name.startsWith(QStringLiteral("AutoCAD")) && !name.contains(QStringLiteral(" LT"))) return bundle;
    }
    return {};
}

class MacLauncher : public IAutoCadLauncher {
public:
    QString runningAutoCad() const override {
        const QString bundle = runningBundle();
        return bundle.isEmpty() ? QString() : QFileInfo(bundle).completeBaseName();
    }

    bool openInRunning(const QString& file, QString* error) override {
        const QString bundle = runningBundle();
        if (bundle.isEmpty()) {
            if (error) *error = QStringLiteral("AutoCAD is not running");
            return false;
        }
        if (!QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), bundle, file})) {
            if (error) *error = QStringLiteral("could not run open -a %1").arg(bundle);
            return false;
        }
        return true;
    }
};

}  // namespace

std::unique_ptr<IAutoCadLauncher> createAutoCadLauncher() { return std::make_unique<MacLauncher>(); }

}  // namespace dc::platform
