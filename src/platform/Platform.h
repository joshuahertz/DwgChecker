#pragma once
// Platform-specific services behind small interfaces. Implementations live in
// platform/win and platform/mac; everything else is platform independent.

#include <QString>
#include <QStringList>

#include <memory>

namespace dc::platform {

// AutoCAD (D96): a file is opened only in the full AutoCAD, already running. The app never
// starts AutoCAD, so AutoCAD always runs as the user started it, outside the app's Store
// package: its own settings and files stay its own, and the package needs no restricted
// capability. AutoCAD LT is not supported (it cannot be driven by another program on
// Windows). The same on every platform and in every build (Store, zip, macOS).
class IAutoCadLauncher {
public:
    virtual ~IAutoCadLauncher() = default;
    // The running full AutoCAD, for messages ("AutoCAD 25.1"), or empty when none is running.
    virtual QString runningAutoCad() const = 0;
    // Opens `file` read-only in the running AutoCAD; false, with *error, when that fails.
    virtual bool openInRunning(const QString& file, QString* error) = 0;
};

std::unique_ptr<IAutoCadLauncher> createAutoCadLauncher();

// Where the bundled dwgread lives: next to the executable (Windows) or in
// Contents/Helpers (macOS).
QString bundledDwgReadPath();
// The dwgread the app uses: the bundled copy, else one next to the executable. There
// is no user setting. $DWGCHECK_DWGREAD and PATH are developer fallbacks for a build
// made without a dwgread to bundle.
QString findDwgRead();

// Where the read-only copies opened in AutoCAD go: <home>/DWG Checker/Temp (D97). Outside
// AppData, whose new files a Store-installed app keeps to itself (AutoCAD could not open a
// copy there, the system temp folder included), and outside Documents and Desktop, which
// OneDrive and iCloud often sync. The same in every build.
QString evidenceFolder();
// Protects the evidence: copies `file` into `root` (read-only), in a folder named after the
// start of its SHA-256, and returns the copy, so saving in AutoCAD can never rewrite the
// submission. The same file again reuses its copy.
QString makeEvidenceCopy(const QString& file, QString* error, const QString& root = evidenceFolder());
// Removes the app's own copies from `root`: a file whose SHA-256 begins with its folder's
// name. Anything else stays, such as a drawing saved there with Save As, and so does a copy
// still open in AutoCAD (Windows cannot delete it). Then removes the folders left empty,
// `root` itself and its "DWG Checker" parent included. Called at start and close (D97).
void removeCopies(const QString& root = evidenceFolder());
// The folders earlier versions copied into: Documents/DWG Checker/Opened in AutoCAD (0.3.0)
// and DwgChecker-evidence in the system temp folder (before). Cleared at start, the same way.
QStringList earlierEvidenceFolders();

}  // namespace dc::platform
