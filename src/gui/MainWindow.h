#pragma once

#include "Batch.h"

#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QPointer>

#include <atomic>
#include <map>
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
class QSplitter;
class QSortFilterProxyModel;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QTreeView;
class QToolButton;

namespace dcgui {

class AutoCadController;
class DetailDialog;
class DuplicatesTab;
class PathCard;
class StepBox;
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
    // A window with the solution drawing in its own colors; opens once it is read.
    void previewDrawing(const QString& refPath);
    void exportResults();
    void exportDuplicates();
    void openDetail(int student, int errorIndex = -1);
    void openInAutoCad(int student);
    // Top cards.
    void referenceChanged();
    void folderChanged();
    void updateRefInfo();    // "inches · 4 objects · 5 dimensions", read in the background
    void endRefRead();       // hides the reading progress and the busy cursor; drops a read in flight
    void updateDirInfo();    // "33 drawings found"
    void updateSteps();      // card highlights, Run enabled, the step guide
    void storeQuizSettings();  // the current settings, remembered for the chosen reference
    // Puts settings in the panel and re-reads the solution drawings if the reading rules
    // changed, as a change made in the panel does (D76).
    void applySettings(const dc::AppSettings& s);
    void closePreviews();
    void addRecent(const char* key, PathCard* card, const QString& path);
    void clearRecent(const char* key, PathCard* card);

    // Top bar
    PathCard* m_refCard = nullptr;
    QToolButton* m_showRef = nullptr;  // "Show drawing" on the card, for a single solution drawing
    PathCard* m_dirCard = nullptr;
    StepBox* m_modeBox = nullptr;  // step 2: the location mode
    int m_refReadable = 0;  // 1 read, -1 not readable, 0 reading / nothing chosen
    QByteArray m_refCanonSig;  // reading rules the solution card was last described with
    int m_refRead = 0;      // counts reads of the solution drawings; only the latest updates the card
    bool m_refReading = false;
    std::shared_ptr<std::atomic_bool> m_refCancel;  // set to stop the read in flight between drawings
    bool m_busyCursor = false;  // the application's busy cursor is set (one level, while reading)
    // The solution drawings as last read for the card, by path (the reading rules of
    // m_refCanonSig), with each one's summary; for the preview and Edit views.
    std::map<QString, dc::DrawingPtr> m_refDrawings;
    std::map<QString, QString> m_refSummaries;
    QString m_pendingPreview;  // clicked while still being read: opens when the read ends
    QHash<QString, QPointer<QDialog>> m_previews;  // one open preview per drawing
    int m_dirCount = 0;     // drawings in the chosen folder
    int m_dirScan = 0;      // counts scans of the folder (in the background); only the latest updates the card
    QString m_dirScanned;   // the folder m_dirCount is for
    QTimer* m_relayout = nullptr;  // lays the results out again once a column drag pauses
    QToolButton* m_settingsToggle = nullptr;
    QScrollArea* m_settingsArea = nullptr;
    QSplitter* m_split = nullptr;  // the steps and settings | the results
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
    // Per solution drawing, the views confirmed in the editor, passed to every run. The views a
    // run found itself are not kept: they counted as confirmed, so later settings and sidecar
    // changes were ignored (D76).
    QHash<QString, dc::ViewSet> m_confirmedViews;
    QFutureWatcher<dc::BatchOutput> m_watcher;
    std::shared_ptr<dc::IDrawingLoader> m_runLoader;
    std::atomic_bool m_cancel{false};
    QHash<int, QPointer<DetailDialog>> m_details;  // one open detail view per student
};

}  // namespace dcgui
