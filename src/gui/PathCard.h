#pragma once
// A step card for choosing a file or folder. It shows a numbered badge (a check once
// done), a caption ("Reference (solution)"), the name in bold and a gray detail line.
// Clicking anywhere on the card opens the file or folder dialog; files or folders can
// be dropped on it; a small "Recent" button lists recent entries. The current step
// is outlined in the highlight color.

#include <QFrame>
#include <QUrl>

class QHBoxLayout;
class QLabel;
class QMenu;
class QToolButton;

namespace dcgui {

class ElidedLabel;

class PathCard : public QFrame {
    Q_OBJECT
public:
    enum class Kind { File, Folder };
    enum class State { Waiting, Active, Done };
    // `fileFilter` (File cards): "Drawings (*.dwg *.json)"; `suffixes`: accepted on drop.
    // `multiple` (File cards): several files may be chosen at once.
    PathCard(Kind kind, int step, const QString& caption, const QString& emptyTitle, const QString& fileFilter,
             const QStringList& suffixes, bool multiple = false, QWidget* parent = nullptr);

    QString path() const { return m_paths.value(0); }
    QStringList paths() const { return m_paths; }
    // Shows the choice (empty = nothing chosen) and emits pathsChanged when it differs.
    void setPaths(const QStringList& paths);
    void setPath(const QString& p) { setPaths(p.isEmpty() ? QStringList() : QStringList{p}); }
    // A recent entry: several files are joined by '|'.
    static QString encode(const QStringList& paths) { return paths.join(QLatin1Char('|')); }
    static QStringList decode(const QString& entry) { return entry.split(QLatin1Char('|'), Qt::SkipEmptyParts); }
    void setDetail(const QString& text, bool problem = false);
    // Gray text after the caption, e.g. "settings saved for this quiz".
    void setCaptionNote(const QString& note);
    // Most recent first; listed by the Recent button (hidden when empty).
    void setRecent(const QStringList& recent);
    // A button shown on the card (e.g. "Edit views…"); clicking it does not browse.
    void addButton(QWidget* w);
    void setState(State s);
    State state() const { return m_state; }
    void browse();

signals:
    void pathsChanged();
    void clearRecentRequested();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QString acceptedPath(const QString& local) const;
    QStringList acceptedPaths(const QList<QUrl>& urls) const;
    void rebuildMenu();
    void refresh();
    void updateBadge();

    Kind m_kind;
    int m_step;
    bool m_multiple = false;
    QStringList m_paths;
    State m_state = State::Waiting;
    QString m_caption, m_emptyTitle, m_filter, m_note;
    QStringList m_suffixes, m_recent;
    QLabel* m_badge = nullptr;
    QLabel* m_captionLabel = nullptr;
    ElidedLabel* m_title = nullptr;
    QLabel* m_detail = nullptr;  // wraps (a line per solution drawing)
    QToolButton* m_recentBtn = nullptr;
    QMenu* m_menu = nullptr;
    QHBoxLayout* m_buttons = nullptr;
};

// A round step badge: the number, or a check when done (also used for the Run step).
QPixmap stepBadge(int step, PathCard::State state, int size, qreal dpr);

// A step of the main window that holds controls instead of a path (step 2: the location
// mode). It looks like a PathCard: a badge for the step's state, a frame that is outlined
// while the step is next.
class StepBox : public QFrame {
public:
    StepBox(int step, QWidget* content, QWidget* parent = nullptr);
    void setState(PathCard::State s);

private:
    int m_step;
    PathCard::State m_state = PathCard::State::Waiting;
    QLabel* m_badge = nullptr;
};

}  // namespace dcgui
