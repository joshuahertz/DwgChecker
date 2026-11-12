#include "PathCard.h"

#include <QCollator>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QStyle>
#include <QToolButton>

#include <algorithm>

namespace dcgui {

// A one-line label that shortens long text with "…" instead of growing the window.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent) : QLabel(parent) { setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); }
    void setFullText(const QString& t) {
        m_full = t;
        setText(t);  // keeps accessibility and the height hint
        update();
    }
    QSize minimumSizeHint() const override { return {20, fontMetrics().height()}; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setFont(font());
        p.setPen(palette().color(foregroundRole()));
        p.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
                   fontMetrics().elidedText(m_full, Qt::ElideMiddle, contentsRect().width()));
    }

private:
    QString m_full;
};

namespace {
constexpr int kBadge = 30;
const QColor kActive(40, 120, 220), kDone(46, 170, 80), kWaiting(150, 150, 150);
}  // namespace

bool samePath(const QString& a, const QString& b) {
    const QString x = QDir::cleanPath(QFileInfo(a).absoluteFilePath()), y = QDir::cleanPath(QFileInfo(b).absoluteFilePath());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return x.compare(y, Qt::CaseInsensitive) == 0;
#else
    return x == y;
#endif
}

QPixmap stepBadge(int step, PathCard::State state, int size, qreal dpr) {
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(state == PathCard::State::Done ? kDone : state == PathCard::State::Active ? kActive : kWaiting);
    p.drawEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));
    if (state == PathCard::State::Done) {
        p.setPen(QPen(Qt::white, size * 0.12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath check;
        check.moveTo(size * 0.28, size * 0.52);
        check.lineTo(size * 0.44, size * 0.68);
        check.lineTo(size * 0.73, size * 0.36);
        p.drawPath(check);
    } else {
        QFont f;
        f.setBold(true);
        f.setPixelSize(static_cast<int>(size * 0.55));
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, QString::number(step));
    }
    return pm;
}

PathCard::PathCard(Kind kind, int step, const QString& caption, const QString& emptyTitle, const QString& fileFilter,
                   const QStringList& suffixes, bool multiple, QWidget* parent)
    : QFrame(parent), m_kind(kind), m_step(step), m_multiple(multiple && kind == Kind::File), m_caption(caption),
      m_emptyTitle(emptyTitle), m_filter(fileFilter), m_suffixes(suffixes) {
    setObjectName(QStringLiteral("pathCard"));
    setAttribute(Qt::WA_Hover);
    setStyleSheet(QStringLiteral(
        "QFrame#pathCard { border: 1px solid palette(mid); border-radius: 8px; background: palette(base); }"
        "QFrame#pathCard:hover { border: 1px solid palette(highlight); }"
        "QFrame#pathCard[step=\"active\"] { border: 2px solid palette(highlight); }"
        "QFrame#pathCard[step=\"waiting\"] { background: palette(window); }"));
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);  // the detail wraps, so the card's height follows its width
    setSizePolicy(sp);

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(12, 10, 12, 10);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(1);
    m_badge = new QLabel(this);
    m_badge->setAlignment(Qt::AlignTop);
    grid->addWidget(m_badge, 0, 0, 3, 1);
    m_captionLabel = new QLabel(this);
    m_captionLabel->setForegroundRole(QPalette::PlaceholderText);
    grid->addWidget(m_captionLabel, 0, 1);
    m_title = new ElidedLabel(this);
    QFont big = font();
    big.setBold(true);
    big.setPointSizeF(big.pointSizeF() * 1.15);
    m_title->setFont(big);
    grid->addWidget(m_title, 1, 1);
    m_detail = new QLabel(this);
    m_detail->setWordWrap(true);
    m_detail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // never widens the card
    grid->addWidget(m_detail, 2, 1);
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);  // the style's own height: a 6 px bar under gray text went unnoticed
    m_progress->setVisible(false);      // a hidden row takes no space
    grid->addWidget(m_progress, 3, 1);
    grid->setColumnStretch(1, 1);

    m_buttons = new QHBoxLayout;
    m_buttons->setSpacing(6);
    m_recentBtn = new QToolButton(this);
    m_recentBtn->setText(tr("Recent"));
    m_recentBtn->setPopupMode(QToolButton::InstantPopup);
    m_recentBtn->setCursor(Qt::ArrowCursor);
    m_menu = new QMenu(this);
    m_recentBtn->setMenu(m_menu);
    m_buttons->addWidget(m_recentBtn);
    grid->addLayout(m_buttons, 0, 2, 3, 1, Qt::AlignVCenter);

    rebuildMenu();
    setState(State::Waiting);
    refresh();
}

void PathCard::setPaths(const QStringList& paths) {
    QStringList clean;
    for (const QString& p : paths)
        if (!p.isEmpty() && std::none_of(clean.cbegin(), clean.cend(), [&](const QString& c) { return samePath(c, p); }))
            clean << QDir::cleanPath(p);
    if (!m_multiple && clean.size() > 1) clean = clean.mid(0, 1);
    // Several solution drawings in name order, numbers by value ("Problem 2" before "Problem 10"):
    // the results call them Drawing 1, 2, 3… in this order.
    QCollator byName;
    byName.setNumericMode(true);
    byName.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(clean.begin(), clean.end(), [&](const QString& a, const QString& b) {
        const int byFile = byName.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName());
        return byFile != 0 ? byFile < 0 : byName.compare(a, b) < 0;  // same name in two folders: by folder
    });
    if (clean == m_paths) return;
    m_paths = clean;
    refresh();
    emit pathsChanged();
}

void PathCard::setDetail(const QString& text, bool problem) {
    m_detail->setText(text);
    QPalette pal = m_detail->palette();
    if (problem) {
        pal.setColor(QPalette::WindowText, QColor(200, 40, 30));
        m_detail->setForegroundRole(QPalette::WindowText);
    } else {
        m_detail->setForegroundRole(QPalette::PlaceholderText);
    }
    m_detail->setPalette(pal);
    m_detail->setToolTip(text);
    m_detail->update();
}

void PathCard::showProgress(int done, int total) {
    if (total > 1) {
        m_progress->setRange(0, total);
        m_progress->setValue(done);
    } else {
        m_progress->setRange(0, 0);  // busy: a moving bar
    }
    m_progress->setVisible(true);
}

void PathCard::hideProgress() { m_progress->setVisible(false); }

void PathCard::setCaptionNote(const QString& note) {
    m_note = note;
    refresh();
}

void PathCard::setRecent(const QStringList& recent) {
    m_recent = recent;
    rebuildMenu();
}

void PathCard::addButton(QWidget* w) {
    w->setCursor(Qt::ArrowCursor);
    m_buttons->insertWidget(0, w);
}

void PathCard::setState(State s) {
    m_state = s;
    setProperty("step", s == State::Active ? "active" : s == State::Done ? "done" : "waiting");
    style()->unpolish(this);
    style()->polish(this);
    updateBadge();
    update();
}

StepBox::StepBox(int step, QWidget* content, QWidget* parent) : QFrame(parent), m_step(step) {
    setObjectName(QStringLiteral("stepBox"));
    setStyleSheet(QStringLiteral(
        "QFrame#stepBox { border: 1px solid palette(mid); border-radius: 8px; background: palette(base); }"
        "QFrame#stepBox[step=\"active\"] { border: 2px solid palette(highlight); }"
        "QFrame#stepBox[step=\"waiting\"] { background: palette(window); }"));
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(12, 10, 12, 10);
    grid->setHorizontalSpacing(12);
    m_badge = new QLabel(this);
    m_badge->setAlignment(Qt::AlignTop);
    grid->addWidget(m_badge, 0, 0, Qt::AlignTop);
    content->setParent(this);
    grid->addWidget(content, 0, 1);
    grid->setColumnStretch(1, 1);
    content->show();
    setState(PathCard::State::Waiting);
}

void StepBox::setState(PathCard::State s) {
    m_state = s;
    setProperty("step", s == PathCard::State::Active ? "active" : s == PathCard::State::Done ? "done" : "waiting");
    style()->unpolish(this);
    style()->polish(this);
    m_badge->setPixmap(stepBadge(m_step, m_state, kBadge, devicePixelRatioF()));
    update();
}

void PathCard::updateBadge() {
    m_badge->setPixmap(stepBadge(m_step, m_state, kBadge, devicePixelRatioF()));
}

void PathCard::refresh() {
    const QString caption = tr("Step %1: %2").arg(m_step).arg(m_caption);
    m_captionLabel->setText(m_note.isEmpty() ? caption : QStringLiteral("%1  ·  %2").arg(caption, m_note));
    if (m_paths.isEmpty()) {
        m_title->setFullText(m_emptyTitle);
        m_title->setForegroundRole(QPalette::Link);  // reads as something to click
        setToolTip(m_kind == Kind::Folder ? (m_suffixes.isEmpty() ? tr("Click to choose the folder, or drop it here")
                                                                  : tr("Click to choose the folder, or drop it (or a .zip of it) here"))
                   : m_multiple           ? tr("Click to choose the drawing (or several, for a multi-drawing assignment), or drop them here")
                                          : tr("Click to choose the drawing, or drop it here"));
    } else {
        QStringList names, full;
        for (const QString& p : m_paths) {
            const QFileInfo fi(p);
            names << (fi.fileName().isEmpty() ? p : fi.fileName());
            full << QDir::toNativeSeparators(p);
        }
        m_title->setFullText(names.join(QStringLiteral("  +  ")));
        m_title->setForegroundRole(QPalette::WindowText);
        setToolTip(full.join(QLatin1Char('\n')) + tr("\nClick to choose another"));
    }
}

void PathCard::rebuildMenu() {
    m_menu->clear();
    for (const QString& entry : m_recent) {
        const QStringList ps = decode(entry);
        if (ps.isEmpty()) continue;
        const QFileInfo fi(ps.front());
        QString label = fi.fileName();
        if (ps.size() > 1) label += tr(" + %1 more").arg(ps.size() - 1);
        QAction* a = m_menu->addAction(QStringLiteral("%1   —   %2").arg(label, QDir::toNativeSeparators(fi.absolutePath())));
        QStringList tips;
        bool exists = true;
        for (const QString& p : ps) {
            tips << QDir::toNativeSeparators(p);
            exists = exists && (m_kind == Kind::File ? QFileInfo(p).isFile() : QFileInfo(p).isDir());
        }
        a->setToolTip(tips.join(QLatin1Char('\n')));
        a->setEnabled(exists);
        connect(a, &QAction::triggered, this, [this, ps]() { setPaths(ps); });
    }
    if (!m_recent.isEmpty()) {
        m_menu->addSeparator();
        m_menu->addAction(tr("Clear this list"), this, &PathCard::clearRecentRequested);
    }
    m_recentBtn->setVisible(!m_recent.isEmpty());
}

void PathCard::browse() {
    QString start = path();
    if (start.isEmpty() && !m_recent.isEmpty()) start = decode(m_recent.front()).value(0);
    if ((m_kind == Kind::File || QFileInfo(start).isFile()) && !start.isEmpty()) start = QFileInfo(start).absolutePath();
    if (m_kind == Kind::Folder) {
        const QString p = QFileDialog::getExistingDirectory(this, m_caption, start);
        if (!p.isEmpty()) setPath(p);
    } else if (m_multiple) {
        const QStringList ps = QFileDialog::getOpenFileNames(this, m_caption + tr(" (select several for a multi-drawing assignment)"),
                                                             start, m_filter);
        if (!ps.isEmpty()) setPaths(ps);
    } else {
        const QString p = QFileDialog::getOpenFileName(this, m_caption, start, m_filter);
        if (!p.isEmpty()) setPath(p);
    }
}

void PathCard::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        e->accept();  // so that the release comes here too
        return;
    }
    QFrame::mousePressEvent(e);
}

void PathCard::mouseReleaseEvent(QMouseEvent* e) {
    // A click anywhere on the card (labels included; its buttons handle their own clicks).
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) {
        e->accept();
        browse();
        return;
    }
    QFrame::mouseReleaseEvent(e);
}

void PathCard::browseFile() {
    QString start = path();
    if (start.isEmpty() && !m_recent.isEmpty()) start = decode(m_recent.front()).value(0);
    if (QFileInfo(start).isFile()) start = QFileInfo(start).absolutePath();
    const QString p = QFileDialog::getOpenFileName(this, m_caption, start, m_filter);
    if (!p.isEmpty()) setPath(p);
}

QString PathCard::acceptedPath(const QString& local) const {
    const QFileInfo fi(local);
    const bool suffixOk = std::any_of(m_suffixes.cbegin(), m_suffixes.cend(),
                                      [&](const QString& s) { return fi.suffix().compare(s, Qt::CaseInsensitive) == 0; });
    if (m_kind == Kind::Folder) {
        if (fi.isDir()) return fi.absoluteFilePath();
        if (fi.isFile()) return suffixOk ? fi.absoluteFilePath()  // a .zip of the folder
                                         : fi.absolutePath();     // a dropped submission: use its folder
        return {};
    }
    return fi.isFile() && suffixOk ? fi.absoluteFilePath() : QString();
}

QStringList PathCard::acceptedPaths(const QList<QUrl>& urls) const {
    QStringList out;
    for (const QUrl& u : urls) {
        if (!u.isLocalFile()) continue;
        const QString p = acceptedPath(u.toLocalFile());
        if (!p.isEmpty() && std::none_of(out.cbegin(), out.cend(), [&](const QString& o) { return samePath(o, p); })) out << p;
        if (!m_multiple && !out.isEmpty()) break;
    }
    return out;
}

void PathCard::dragEnterEvent(QDragEnterEvent* e) {
    if (!acceptedPaths(e->mimeData()->urls()).isEmpty()) e->acceptProposedAction();
}

void PathCard::dropEvent(QDropEvent* e) {
    const QStringList ps = acceptedPaths(e->mimeData()->urls());
    if (ps.isEmpty()) return;
    setPaths(ps);
    e->acceptProposedAction();
}

}  // namespace dcgui
