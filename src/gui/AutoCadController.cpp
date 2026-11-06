#include "AutoCadController.h"

#include "Loader.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

using namespace dc;

namespace dcgui {

AutoCadController::AutoCadController(QObject* parent)
    : QObject(parent), m_launcher(platform::createAutoCadLauncher()) {
    redetect();
}

void AutoCadController::redetect() { m_installs = m_launcher->detect(); }

platform::AutoCadInstall AutoCadController::current() const {
    const QString exe = QSettings().value(QStringLiteral("autocad/exe")).toString();
    if (!exe.isEmpty()) {
        for (const auto& i : m_installs)
            if (i.exe == exe) return i;
        if (QFileInfo::exists(exe)) return m_launcher->fromUserPath(exe);
    }
    return m_installs.isEmpty() ? platform::AutoCadInstall{} : m_installs.front();
}

void AutoCadController::setCurrentExe(const QString& exe) { QSettings().setValue(QStringLiteral("autocad/exe"), exe); }

bool AutoCadController::protectEvidence() const { return QSettings().value(QStringLiteral("autocad/protectEvidence"), true).toBool(); }

void AutoCadController::setProtectEvidence(bool on) { QSettings().setValue(QStringLiteral("autocad/protectEvidence"), on); }

bool AutoCadController::locate(QWidget* parent) {
#ifdef Q_OS_MACOS
    const QString path = QFileDialog::getOpenFileName(parent, tr("Locate AutoCAD"), QStringLiteral("/Applications/Autodesk"),
                                                      tr("Applications (*.app)"));
#else
    const QString path = QFileDialog::getOpenFileName(parent, tr("Locate AutoCAD (acad.exe or acadlt.exe)"),
                                                      QStringLiteral("C:/Program Files/Autodesk"), tr("AutoCAD (acad.exe acadlt.exe)"));
#endif
    if (path.isEmpty()) return false;
    setCurrentExe(path);
    return true;
}

void AutoCadController::open(QWidget* parent, const QString& path, const QByteArray& scanSha, const std::function<void()>& rescan) {
    // The evidence must not have changed since it was graded.
    const QByteArray now = sha256File(path);
    if (now.isEmpty()) {
        QMessageBox::warning(parent, tr("Open in AutoCAD"), tr("Cannot read %1.").arg(path));
        return;
    }
    if (!scanSha.isEmpty() && now != scanSha) {
        QMessageBox box(QMessageBox::Warning, tr("File changed since the scan"),
                        tr("%1 has changed since it was scanned (SHA-256 differs). The results shown may not match the file.")
                            .arg(QFileInfo(path).fileName()),
                        QMessageBox::NoButton, parent);
        QPushButton* re = box.addButton(tr("Re-scan"), QMessageBox::AcceptRole);
        QPushButton* anyway = box.addButton(tr("Open anyway"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == re) { if (rescan) rescan(); return; }
        if (box.clickedButton() != anyway) return;
    }
    platform::AutoCadInstall inst = current();
    if (inst.exe.isEmpty()) {
        if (QMessageBox::question(parent, tr("AutoCAD not found"),
                                  tr("No AutoCAD installation was detected. Locate it now?")) != QMessageBox::Yes ||
            !locate(parent))
            return;
        inst = current();
    }
    QString target = path;
    if (protectEvidence()) {
        QString err;
        target = platform::makeEvidenceCopy(path, &err);
        if (target.isEmpty()) {
            QMessageBox::warning(parent, tr("Open in AutoCAD"), tr("Could not make a protected copy: %1").arg(err));
            return;
        }
    }
    QString err;
    if (!m_launcher->open(inst, target, &err))
        QMessageBox::warning(parent, tr("Open in AutoCAD"), tr("Could not open the file in %1: %2").arg(inst.name, err));
}

}  // namespace dcgui
