#pragma once
// Settings panel: the basic choices (mode, tolerance, what to check) always shown,
// everything else under "Advanced settings" (collapsed by default).

#include "Settings.h"

#include <QWidget>

#include <QSet>

#include <map>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace dcgui {

class AutoCadController;

// What a solution drawing contains, as far as Settings cares: a check for dimensions or
// hatches is shown only for a drawing that has some. Unknown (not read yet) counts as yes.
struct DrawingContent {
    bool dims = true;
    bool hatches = true;
};

class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    SettingsPanel(AutoCadController* acad, QWidget* parent = nullptr);
    void setSettings(const dc::AppSettings& s);
    // The chosen solution drawings: with two or more, each gets its own mode and checks.
    void setSolutionDrawings(const QStringList& paths);
    // Step 2 of the main window: the location mode (or the table of solution drawings). The
    // panel builds it; MainWindow puts it in its step box.
    QWidget* modeStepWidget() const { return m_modeStep; }
    // What each solution drawing contains, by AppSettings::drawingKey (once they are read).
    void setDrawingContents(const std::map<QString, DrawingContent>& byKey);
    dc::AppSettings settings() const;
    void refreshAutoCad();

signals:
    void changed();
    void editViewsRequested(const QString& refPath);  // "Edit views…"; empty = the only drawing
    void previewRequested(const QString& refPath);    // a drawing's name in the Solution drawings table

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    // A number field; `bottom` is the smallest value allowed (a tolerance must be above 0).
    QLineEdit* sciEdit(double v, double bottom = 0);
    static double sciValue(const QLineEdit* e, double def);
    // Puts a value in a field from sciEdit; one out of its range shows `def` instead.
    static void setSci(QLineEdit* e, double v, double def);
    void updateModeHelp();  // the mode's one-line description, then updateScopes()
    // A grey note under a shared setting that only some solution drawings use.
    struct Scope {
        QFormLayout* form = nullptr;
        QWidget* row = nullptr;
        QLabel* label = nullptr;
    };
    Scope addScope(QFormLayout* form, bool indent);
    // Shows only the settings that apply (by mode, checks and what the drawings contain), and
    // with several solution drawings says which drawings each one applies to.
    void updateScopes();
    DrawingContent contentOf(const QString& key) const;
    void rebuildDrawingRows();
    void applyDrawingLayout();

    AutoCadController* m_acad;
    QComboBox* m_mode;
    QWidget* m_drawingsBox;  // the Solution drawings table, in step 2
    QWidget* m_modeStep;
    QFormLayout* m_modeForm;
    QCheckBox* m_matchColor;
    QDoubleSpinBox* m_colorTol;
    QWidget* m_colorTolRow;
    QCheckBox* m_byLayer;
    QGridLayout* m_drawingsGrid;
    QFormLayout* m_rulesForm;  // "Matching rules"
    QGroupBox* m_orthoBox;
    Scope m_dimPrecScope, m_hatchPatternScope, m_orthoScope;
    QWidget* m_editViewsRow;
    QWidget* m_dimPrecRow;
    QWidget* m_hatchPatternRow;
    std::map<QString, DrawingContent> m_contents;
    // The Solution drawings table: per drawing, the controls that may not apply.
    struct DrawingRow {
        QString key;
        QWidget* dims;
        QWidget* hatches;
        QWidget* views;
    };
    std::vector<DrawingRow> m_rows;
    QWidget* m_headDims = nullptr;
    QWidget* m_headHatches = nullptr;
    QStringList m_drawingPaths;
    std::map<QString, dc::DrawingSettings> m_perDrawing;
    QLabel* m_modeHelp;
    QToolButton* m_advancedToggle;
    QWidget* m_advanced;
    QListWidget* m_templates;
    QCheckBox* m_matchLt;
    QCheckBox* m_rotation;
    QCheckBox* m_mirror;
    QLineEdit* m_linTol;
    QLineEdit* m_angTol;
    QCheckBox* m_layerMatch;
    QComboBox* m_extras;
    QCheckBox* m_dims;
    QCheckBox* m_dimPrec;
    QCheckBox* m_strictCl;
    QCheckBox* m_hatches;
    QCheckBox* m_hatchPattern;
    QComboBox* m_scaleMode;  // wrong scale counts as an error: never / once / every time
    QComboBox* m_locationMode;  // wrong location (Absolute mode) counts as an error: never / once / every time
    QPushButton* m_editViews;
    QLineEdit* m_nearMiss;
    QLineEdit* m_ignoreRe;
    QCheckBox* m_paper;
    QCheckBox* m_skipDefpoints;
    QCheckBox* m_skipHidden;
    QCheckBox* m_ignoreHiddenRef;
    QLineEdit* m_hiddenPat;
    QLineEdit* m_centerPat;
    QLineEdit* m_phantomPat;
    QLineEdit* m_visiblePat;
    QSpinBox* m_topK;
    QDoubleSpinBox* m_gapPct;
    QLineEdit* m_alignTol;
    QComboBox* m_alignSev;
    QComboBox* m_arrangeSev;
    QDoubleSpinBox* m_templateShare;
    QDoubleSpinBox* m_smallEdit;
    QLineEdit* m_identity;
    QSpinBox* m_timeout;
    QLineEdit* m_oda;
    QComboBox* m_acadCombo;
    QCheckBox* m_protect;
};

}  // namespace dcgui
