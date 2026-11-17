#include "AutoCadController.h"

#include "Loader.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

using namespace dc;

namespace dcgui {

AutoCadController::AutoCadController(QObject* parent)
    : QObject(parent), m_launcher(platform::createAutoCadLauncher()) {
    // The AutoCAD choice and the read-only switch of earlier versions are gone (D96).
    QSettings().remove(QStringLiteral("autocad"));
    // Copies are kept only while the app runs (D97): any left over (one was still open in
    // AutoCAD at close), and those of earlier versions' folders, go now.
    for (const QString& old : platform::earlierEvidenceFolders()) platform::removeCopies(old);
    platform::removeCopies();
}

AutoCadController::~AutoCadController() { platform::removeCopies(); }

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
    // The full AutoCAD must already be running: the app never starts it, and AutoCAD LT can't
    // be used (D96).
    const QString acad = m_launcher->runningAutoCad();
    if (acad.isEmpty()) {
        QMessageBox::information(parent, tr("Open in AutoCAD"),
                                 tr("AutoCAD isn't running. Start AutoCAD, then click Open in AutoCAD again."));
        return;
    }
    // The read-only copy, made once it will be opened, and the bytes just checked: a sync
    // client may have changed the file in between (D76).
    QString err;
    const QString target = platform::makeEvidenceCopy(path, &err);
    if (target.isEmpty()) {
        QMessageBox::warning(parent, tr("Open in AutoCAD"), tr("Could not make the read-only copy: %1").arg(err));
        return;
    }
    if (sha256File(target) != now) {
        QMessageBox::warning(parent, tr("Open in AutoCAD"),
                             tr("%1 changed while it was being opened. Try again.").arg(QFileInfo(path).fileName()));
        return;
    }
    // Opening may wait a few seconds for a busy AutoCAD.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool opened = m_launcher->openInRunning(target, &err);
    QApplication::restoreOverrideCursor();
    if (!opened) QMessageBox::warning(parent, tr("Open in AutoCAD"), tr("Could not open the file in %1: %2").arg(acad, err));
}

}  // namespace dcgui
