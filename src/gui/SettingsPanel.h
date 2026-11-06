#pragma once
// Settings panel: the basic choices (mode, tolerance, what to check) always shown,
// everything else under "Advanced settings" (collapsed by default).

#include "Settings.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QToolButton;

namespace dcgui {

class AutoCadController;

class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    SettingsPanel(AutoCadController* acad, QWidget* parent = nullptr);
    void setSettings(const dc::AppSettings& s);
    dc::AppSettings settings() const;
    void refreshAutoCad();

signals:
    void changed();

private:
    QLineEdit* sciEdit(double v);
    static double sciValue(const QLineEdit* e, double def);
    void updateModeHelp();  // the mode's one-line description; rotation/mirror only in Relative mode

    AutoCadController* m_acad;
    QComboBox* m_mode;
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
    QCheckBox* m_detectScale;
    QSpinBox* m_scalePenalty;
    QLineEdit* m_nearMiss;
    QLineEdit* m_ignoreRe;
    QCheckBox* m_paper;
    QCheckBox* m_skipDefpoints;
    QCheckBox* m_skipHidden;
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
