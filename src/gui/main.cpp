#include "MainWindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DwgChecker"));
    QApplication::setApplicationName(QStringLiteral("DWG Checker"));
    QApplication::setApplicationVersion(QStringLiteral(DWGCHECK_VERSION));
    QIcon icon;
    for (int s : {16, 24, 32, 48, 64, 128, 256}) icon.addFile(QStringLiteral(":/icons/app_%1.png").arg(s), QSize(s, s));
    QApplication::setWindowIcon(icon);
    dcgui::MainWindow w;
    w.show();
    return app.exec();
}
