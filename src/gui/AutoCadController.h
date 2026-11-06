#pragma once
// GUI-side AutoCAD handling: version choice (QSettings), locating AutoCAD when
// nothing is detected, evidence protection and the "changed since scan" check.

#include "Platform.h"

#include <QObject>

#include <functional>
#include <memory>

class QWidget;

namespace dcgui {

class AutoCadController : public QObject {
    Q_OBJECT
public:
    explicit AutoCadController(QObject* parent = nullptr);

    QList<dc::platform::AutoCadInstall> installs() const { return m_installs; }
    // The chosen install (newest detected by default). Empty exe = none.
    dc::platform::AutoCadInstall current() const;
    void setCurrentExe(const QString& exe);
    bool protectEvidence() const;
    void setProtectEvidence(bool on);
    // Prompts for acad.exe / AutoCAD*.app; returns false if the user cancels.
    bool locate(QWidget* parent);
    void redetect();

    // Opens a submission in AutoCAD. `scanSha` is the SHA-256 recorded at scan time;
    // `rescan` is called if the user chooses to re-scan after a change was detected.
    void open(QWidget* parent, const QString& path, const QByteArray& scanSha, const std::function<void()>& rescan);

private:
    std::unique_ptr<dc::platform::IAutoCadLauncher> m_launcher;
    QList<dc::platform::AutoCadInstall> m_installs;
};

}  // namespace dcgui
