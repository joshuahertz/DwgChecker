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
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSortFilterProxyModel;
class QTabWidget;
class QTableView;
class QToolButton;

namespace dcgui {

class AutoCadController;
class DetailDialog;
class DuplicatesTab;
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
    void editViews();
    void exportResults();
    void exportDuplicates();
    void openDetail(int student, int errorIndex = -1);
    void openInAutoCad(int student);
    void updateModeUi();

    // Top bar
    QLineEdit* m_refEdit = nullptr;
    QLineEdit* m_dirEdit = nullptr;
    QToolButton* m_settingsToggle = nullptr;
    QScrollArea* m_settingsArea = nullptr;
    SettingsPanel* m_settings = nullptr;
    QPushButton* m_runBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_viewsBtn = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    // Tabs
    QTabWidget* m_tabs = nullptr;
    QTableView* m_table = nullptr;
    ResultsModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    ResultsDelegate* m_delegate = nullptr;
    DuplicatesTab* m_dups = nullptr;
    QPlainTextEdit* m_log = nullptr;

    AutoCadController* m_acad = nullptr;
    std::shared_ptr<const dc::BatchOutput> m_batch;
    QString m_batchRefPath;
    std::optional<dc::ViewSet> m_views;  // confirmed in the editor this session
    QFutureWatcher<dc::BatchOutput> m_watcher;
    std::shared_ptr<dc::IDrawingLoader> m_runLoader;
    std::atomic_bool m_cancel{false};
    QHash<int, QPointer<DetailDialog>> m_details;  // one open detail view per student
};

}  // namespace dcgui
