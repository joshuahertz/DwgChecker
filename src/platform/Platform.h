#pragma once
// Platform-specific services behind small interfaces. Implementations live in
// platform/win and platform/mac; everything else is platform independent.

#include <QString>

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

// Where the read-only copies opened in AutoCAD go: Documents/DWG Checker/Opened in AutoCAD
// (D96). Other programs see that folder from every build; a Store-installed app's AppData
// and temp files are its own, and AutoCAD could not open a copy there.
QString evidenceFolder();
// Protects the evidence: copies `file` into evidenceFolder() (read-only) and returns the
// copy, so saving in AutoCAD can never rewrite the submission. Copies older than a week, and
// the temp folder earlier versions used, are removed first.
QString makeEvidenceCopy(const QString& file, QString* error);

}  // namespace dc::platform
