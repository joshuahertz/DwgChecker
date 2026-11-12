#include "MainWindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSplashScreen>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <functional>

namespace {

// The splash card: the app icon, its name and version, what it does, and the credit; drawn at
// the screen's pixel ratio so it is sharp at any display scaling.
QPixmap splashPixmap() {
    const qreal dpr = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
    const QSize size(520, 300);
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::white);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setPen(QPen(QColor(24, 72, 140), 2));  // a thin frame in the icon's blue
    p.drawRect(QRectF(1, 1, size.width() - 2, size.height() - 2));

    const QPixmap icon(QStringLiteral(":/icons/app_256.png"));
    p.drawPixmap(QRectF(36, 52, 128, 128), icon, QRectF(icon.rect()));

    const qreal x = 188, w = size.width() - x - 28;
    QFont title = QApplication::font();
    title.setPointSizeF(title.pointSizeF() * 2.3);
    title.setBold(true);
    p.setFont(title);
    p.setPen(QColor(20, 20, 20));
    p.drawText(QRectF(x, 56, w, 46), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("DWG Checker"));

    QFont body = QApplication::font();
    body.setPointSizeF(body.pointSizeF() * 1.1);
    p.setFont(body);
    p.setPen(QColor(70, 70, 70));
    p.drawText(QRectF(x, 106, w, 48), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               QObject::tr("Grades CAD drawings (DWG) against reference solutions"));
    p.setPen(QColor(120, 120, 120));
    p.drawText(QRectF(x, 156, w, 22), Qt::AlignLeft | Qt::AlignVCenter,
               QObject::tr("Version %1").arg(QStringLiteral(DWGCHECK_VERSION)));

    QFont small = QApplication::font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    p.setFont(small);
    p.setPen(QColor(140, 140, 140));
    p.drawText(QRectF(36, 222, size.width() - 72, 20), Qt::AlignLeft | Qt::AlignVCenter,
               QObject::tr("Designed by Joshua Hertz; its code was written with Claude (Anthropic)."));
    return pm;
}

// QSplashScreen draws its message against the window's edge; this one keeps the card's margin.
class Splash : public QSplashScreen {
public:
    explicit Splash(const QPixmap& pm) : QSplashScreen(pm, Qt::WindowStaysOnTopHint) {}
    std::function<void()> onClick;  // a click closes the splash and shows the window at once (D76)

protected:
    void drawContents(QPainter* p) override {
        p->setPen(QColor(24, 72, 140));
        p->drawText(QRectF(rect()).adjusted(36, 0, -36, -26), Qt::AlignLeft | Qt::AlignBottom, message());
    }
    void mousePressEvent(QMouseEvent* e) override {
        QSplashScreen::mousePressEvent(e);
        if (onClick) onClick();
    }
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DwgChecker"));
    QApplication::setApplicationName(QStringLiteral("DWG Checker"));
    QApplication::setApplicationVersion(QStringLiteral(DWGCHECK_VERSION));
    QIcon icon;
    for (int s : {16, 24, 32, 48, 64, 128, 256}) icon.addFile(QStringLiteral(":/icons/app_%1.png").arg(s), QSize(s, s));
    QApplication::setWindowIcon(icon);

    // The splash covers the start (the window is built, the saved state loaded and dwgread's
    // version checked). That work blocks the event loop, and a window is drawn only once the
    // system has put it on screen, so the splash is first shown and painted, then the work
    // starts. It stays 1.5 s from then (counted from before it was drawn, it only flashed), and
    // the main window takes its place when it closes; a click closes it sooner.
    Splash splash(splashPixmap());
    splash.show();
    splash.showMessage(QObject::tr("Starting…"));
    QElapsedTimer started;
    started.start();
    while (!(splash.windowHandle() && splash.windowHandle()->isExposed()) && started.elapsed() < 1000)
        app.processEvents(QEventLoop::AllEvents, 20);
    splash.repaint();
    app.processEvents();
    started.restart();  // on screen from now

    dcgui::MainWindow w;  // shown when the splash closes: after 1.5 s, or at a click
    bool shown = false;
    auto showWindow = [&splash, &w, &shown]() {
        if (shown) return;
        shown = true;
        w.show();
        splash.finish(&w);
    };
    splash.onClick = showWindow;
    const int left = std::max(0, 1500 - static_cast<int>(started.elapsed()));
    QTimer::singleShot(left, &splash, showWindow);
    return app.exec();
}
