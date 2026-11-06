#include "MainWindow.h"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DwgChecker"));
    QApplication::setApplicationName(QStringLiteral("DWG Checker"));
    QApplication::setApplicationVersion(QStringLiteral(DWGCHECK_VERSION));
    dcgui::MainWindow w;
    w.show();
    return app.exec();
}
