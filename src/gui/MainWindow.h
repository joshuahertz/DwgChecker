#pragma once

#include "Batch.h"

#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QPointer>

#include <atomic>
#include <memory>
#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QDialog;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSortFilterProxyModel;
class QStackedWidget;
class QTabWidget;
class QTreeView;
class QToolButton;

namespace dcgui {

class AutoCadController;
class DetailDialog;
class DuplicatesTab;
class PathCard;
class ResultsDelegate;
class ResultsModel;
class SettingsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildUi();
    void loadState();
    void saveState() const;
    void log(const QString& line);
    void probeParser();
    dc::AppSettings currentSettings() const;

    void run();
    void cancel();
    void onFinished();
    void editViews(const QString& refPath = QString());  // empty: the only drawing, or ask which
    void fitSettingsArea();
    void exportResults();
    void exportDuplicates();
    void openDetail(int student, int errorIndex = -1);
    void openInAutoCad(int student);
    // Top cards.
    void referenceChanged();
    void folderChanged();
    void updateRefInfo();    // "inches · 4 objects · 5 dimensions", read in the background
    void updateDirInfo();    // "33 drawings found"
    void updateSteps();      // card highlights, Run enabled, the step guide
    void storeQuizSettings();  // the current settings, remembered for the chosen reference
    void addRecent(const char* key, PathCard* card, const QString& path);
    void clearRecent(const char* key, PathCard* card);

    // Top bar
    PathCard* m_refCard = nullptr;
    PathCard* m_dirCard = nullptr;
    int m_refReadable = 0;  // 1 read, -1 not readable, 0 reading / nothing chosen
    QByteArray m_refCanonSig;  // reading rules the solution card was last described with
    int m_dirCount = 0;     // drawings in the chosen folder
    QToolButton* m_settingsToggle = nullptr;
    QScrollArea* m_settingsArea = nullptr;
    SettingsPanel* m_settings = nullptr;
    QPushButton* m_runBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    // Tabs
    QTabWidget* m_tabs = nullptr;
    QStackedWidget* m_resultsStack = nullptr;  // the step guide, then the results table
    QLabel* m_guide = nullptr;
    QTreeView* m_table = nullptr;
    ResultsModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    ResultsDelegate* m_delegate = nullptr;
    DuplicatesTab* m_dups = nullptr;
    QDialog* m_logWindow = nullptr;
    QPlainTextEdit* m_log = nullptr;

    AutoCadController* m_acad = nullptr;
    std::shared_ptr<const dc::BatchOutput> m_batch;
    QHash<QString, dc::ViewSet> m_viewsByRef;  // per solution drawing: confirmed in the editor, or from the last run
    QFutureWatcher<dc::BatchOutput> m_watcher;
    std::shared_ptr<dc::IDrawingLoader> m_runLoader;
    std::atomic_bool m_cancel{false};
    QHash<int, QPointer<DetailDialog>> m_details;  // one open detail view per student
};

}  // namespace dcgui
