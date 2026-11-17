#pragma once
// GUI-side "Open in AutoCAD" (D96): the "changed since scan" check, the running-AutoCAD
// check, and the read-only evidence copy opened there.

#include "Platform.h"

#include <QObject>

#include <functional>
#include <memory>

class QWidget;

namespace dcgui {

class AutoCadController : public QObject {
    Q_OBJECT
public:
    // Both clear the read-only copies (D97): this session's at close, and any left from earlier.
    explicit AutoCadController(QObject* parent = nullptr);
    ~AutoCadController() override;

    // Opens a submission, as a read-only copy, in the AutoCAD that is already running; says so
    // when none is. `scanSha` is the SHA-256 recorded at scan time; `rescan` is called if the
    // user chooses to re-scan after a change was detected.
    void open(QWidget* parent, const QString& path, const QByteArray& scanSha, const std::function<void()>& rescan);

private:
    std::unique_ptr<dc::platform::IAutoCadLauncher> m_launcher;
};

}  // namespace dcgui
