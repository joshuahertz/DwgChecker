#pragma once
// Platform-specific services behind small interfaces. Implementations live in
// platform/win and platform/mac; everything else is platform independent.

#include <QList>
#include <QString>

#include <memory>

namespace dc::platform {

struct AutoCadInstall {
    QString name;     // "AutoCAD 2026 - English"
    QString year;     // "2026"
    QString release;  // "R25.1" (Windows registry) or bundle version (macOS)
    QString exe;      // acad.exe / acadlt.exe, or the .app bundle on macOS
    QString progId;   // COM ProgID (Windows, full AutoCAD only)
    bool lt = false;
    QString key() const { return exe; }  // stored in QSettings
};

class IAutoCadLauncher {
public:
    virtual ~IAutoCadLauncher() = default;
    // Installed versions, newest first.
    virtual QList<AutoCadInstall> detect() const = 0;
    // Opens `file` in the given AutoCAD (reusing a running instance where possible).
    virtual bool open(const AutoCadInstall& install, const QString& file, QString* error) = 0;
    // Builds an install record for a user-picked executable / bundle.
    virtual AutoCadInstall fromUserPath(const QString& path) const = 0;
};

std::unique_ptr<IAutoCadLauncher> createAutoCadLauncher();

// Where the bundled dwgread lives: next to the executable (Windows) or in
// Contents/Helpers (macOS).
QString bundledDwgReadPath();
// The dwgread the app uses: the bundled copy, else one next to the executable. There
// is no user setting. $DWGCHECK_DWGREAD and PATH are developer fallbacks for a build
// made without a dwgread to bundle.
QString findDwgRead();

// Protect the evidence: copies `file` into a per-session temp folder (read-only) and
// returns the copy, so saving in AutoCAD can never rewrite the submission.
QString makeEvidenceCopy(const QString& file, QString* error);

}  // namespace dc::platform
