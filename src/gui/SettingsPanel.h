#pragma once
// Settings panel: the basic choices (mode, tolerance, what to check) always shown,
// everything else under "Advanced settings" (collapsed by default).

#include "Settings.h"

#include <QWidget>

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

class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    SettingsPanel(AutoCadController* acad, QWidget* parent = nullptr);
    void setSettings(const dc::AppSettings& s);
    // The chosen solution drawings: with two or more, each gets its own mode and checks.
    void setSolutionDrawings(const QStringList& paths);
    dc::AppSettings settings() const;
    void refreshAutoCad();

signals:
    void changed();
    void editViewsRequested(const QString& refPath);  // "Edit views…"; empty = the only drawing
    void sizeChanged();  // Advanced or the drawings table shown or hidden

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QLineEdit* sciEdit(double v);
    static double sciValue(const QLineEdit* e, double def);
    void updateModeHelp();  // the mode's one-line description, then updateScopes()
    // A grey note under a shared setting that only some solution drawings use.
    struct Scope {
        QFormLayout* form = nullptr;
        QWidget* row = nullptr;
        QLabel* label = nullptr;
    };
    Scope addScope(QFormLayout* form, bool indent);
    // Enables the mode- and check-specific settings, and with several solution drawings
    // says which drawings each one applies to.
    void updateScopes();
    void rebuildDrawingRows();
    void applyDrawingLayout();

    AutoCadController* m_acad;
    QComboBox* m_mode;
    QGroupBox* m_drawingsBox;
    QGridLayout* m_drawingsGrid;
    QFormLayout* m_gradingForm;
    QFormLayout* m_checksForm;
    Scope m_rotationScope, m_locationScope, m_dimPrecScope, m_hatchPatternScope, m_orthoScope;
    QWidget* m_editViewsRow;
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
