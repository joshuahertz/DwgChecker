#include "SettingsPanel.h"

#include "AutoCadController.h"
#include "Geometry.h"

#include <QCheckBox>
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

using namespace dc;

namespace dcgui {

namespace {

QStringList splitPatterns(const QString& s) {
    QStringList out;
    for (const QString& p : s.split(';', Qt::SkipEmptyParts)) out << p.trimmed();
    return out;
}

QWidget* withBrowse(QLineEdit* edit, const QString& title, QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(edit);
    auto* b = new QPushButton(QObject::tr("Browse…"), w);
    h->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, w, [edit, title, w]() {
        const QString p = QFileDialog::getOpenFileName(w, title, edit->text());
        if (!p.isEmpty()) edit->setText(p);
    });
    return w;
}

// A sub-setting shown indented under the checkbox it belongs to.
QWidget* indented(QWidget* child, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(20, 0, 0, 0);
    lay->addWidget(child);
    lay->addStretch();
    return row;
}

constexpr double kRadToDeg = 180.0 / kPi;
const char* kAdvancedKey = "ui/advancedSettingsOpen";

}  // namespace

QLineEdit* SettingsPanel::sciEdit(double v) {
    auto* e = new QLineEdit(QString::number(v, 'g', 6), this);
    auto* val = new QDoubleValidator(0, 1e9, 12, e);
    val->setNotation(QDoubleValidator::ScientificNotation);
    e->setValidator(val);
    e->setMaximumWidth(110);
    connect(e, &QLineEdit::editingFinished, this, &SettingsPanel::changed);
    return e;
}

double SettingsPanel::sciValue(const QLineEdit* e, double def) {
    bool ok = false;
    const double v = e->text().toDouble(&ok);
    return ok ? v : def;
}

SettingsPanel::SettingsPanel(AutoCadController* acad, QWidget* parent) : QWidget(parent), m_acad(acad) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto check = [this](const QString& text) {
        auto* c = new QCheckBox(text, this);
        connect(c, &QCheckBox::toggled, this, &SettingsPanel::changed);
        return c;
    };
    auto combo = [this](const QStringList& items) {
        auto* c = new QComboBox(this);
        c->addItems(items);
        connect(c, &QComboBox::currentIndexChanged, this, &SettingsPanel::changed);
        return c;
    };
    auto line = [this]() {
        auto* e = new QLineEdit(this);
        connect(e, &QLineEdit::editingFinished, this, &SettingsPanel::changed);
        return e;
    };

    // ======================= Basic =======================
    // Several solution drawings: each one's mode and checks (shown only then).
    m_drawingsBox = new QGroupBox(tr("Solution drawings"), this);
    auto* dLay = new QVBoxLayout(m_drawingsBox);
    auto* dHint = new QLabel(tr("Each drawing's mode and checks; the other settings apply to all of them."), m_drawingsBox);
    dHint->setForegroundRole(QPalette::PlaceholderText);
    dLay->addWidget(dHint);
    m_drawingsGrid = new QGridLayout;
    dLay->addLayout(m_drawingsGrid);
    m_drawingsBox->setVisible(false);
    outer->addWidget(m_drawingsBox);

    // Grading: how the drawing is compared.
    auto* gGrading = new QGroupBox(tr("Grading"), this);
    auto* fGrading = new QFormLayout(gGrading);
    m_gradingForm = fGrading;
    m_mode = combo({tr("Absolute"), tr("Relative"), tr("Orthographic")});
    fGrading->addRow(tr("Mode"), m_mode);
    m_modeHelp = new QLabel(this);
    m_modeHelp->setWordWrap(true);
    m_modeHelp->setForegroundRole(QPalette::PlaceholderText);
    fGrading->addRow(m_modeHelp);
    m_editViews = new QPushButton(tr("Edit views…"), this);
    m_editViews->setToolTip(tr("Orthographic mode: confirm or fix how the reference splits into views"));
    connect(m_editViews, &QPushButton::clicked, this, [this]() { emit editViewsRequested(QString()); });
    m_editViewsRow = indented(m_editViews, this);
    fGrading->addRow(m_editViewsRow);
    m_rotation = check(tr("Allow rotation"));
    m_mirror = check(tr("Allow a mirrored (flipped) drawing"));
    m_rotation->setToolTip(tr("Relative mode only"));
    m_mirror->setToolTip(tr("Relative mode only"));
    fGrading->addRow(m_rotation);
    fGrading->addRow(m_mirror);
    m_rotationScope = addScope(fGrading, false);
    m_linTol = sciEdit(1e-4);
    m_linTol->setToolTip(tr("How far apart two points may be and still count as the same point"));
    fGrading->addRow(tr("Tolerance (drawing units)"), m_linTol);
    m_extras = combo({tr("Count against the score"), tr("List only")});
    m_extras->setToolTip(tr("Items in the drawing that are not in the reference"));
    fGrading->addRow(tr("Extra items"), m_extras);

    // What to check.
    auto* gChecks = new QGroupBox(tr("What to check"), this);
    auto* fChecks = new QFormLayout(gChecks);
    m_checksForm = fChecks;
    m_matchLt = check(tr("Linetypes must match (hidden, center, …)"));
    m_layerMatch = check(tr("Layers must match"));
    fChecks->addRow(m_matchLt);
    fChecks->addRow(m_layerMatch);
    m_dims = check(tr("Check dimensions"));
    m_dimPrec = check(tr("…and the precision they show (4.00 vs 4.000)"));
    m_dimPrec->setToolTip(tr("A dimension with the right value must also show the same decimal places (or fraction "
                             "precision) as the reference, after its style, overrides and zero suppression."));
    fChecks->addRow(m_dims);
    fChecks->addRow(indented(m_dimPrec, this));
    m_dimPrecScope = addScope(fChecks, true);
    connect(m_dims, &QCheckBox::toggled, this, &SettingsPanel::updateScopes);
    m_hatches = check(tr("Check hatches (each hatched region is hatched)"));
    m_hatchPattern = check(tr("…and with the same hatch pattern"));
    fChecks->addRow(m_hatches);
    fChecks->addRow(indented(m_hatchPattern, this));
    m_hatchPatternScope = addScope(fChecks, true);
    connect(m_hatches, &QCheckBox::toggled, this, &SettingsPanel::updateScopes);
    m_scaleMode = combo({tr("Never"), tr("Once"), tr("Every time")});
    m_scaleMode->setItemData(0, tr("Graded after undoing the scale, with no error for the scale itself"), Qt::ToolTipRole);
    m_scaleMode->setItemData(1, tr("Graded after undoing the scale, with one error for the scale"), Qt::ToolTipRole);
    m_scaleMode->setItemData(2, tr("Not rescaled: every object drawn at the wrong size is an error"), Qt::ToolTipRole);
    m_scaleMode->setToolTip(tr("A drawing at the wrong scale, e.g. in millimeters instead of inches, or twice too large. "
                               "Never / Once: it is graded after undoing the scale. Every time: every object is wrong."));
    fChecks->addRow(tr("Wrong scale counts as an error"), m_scaleMode);
    m_locationMode = combo({tr("Never"), tr("Once"), tr("Every time")});
    m_locationMode->setItemData(0, tr("Graded after moving the drawing back, with no error for the location"), Qt::ToolTipRole);
    m_locationMode->setItemData(1, tr("Graded after moving the drawing back, with one error for the location"), Qt::ToolTipRole);
    m_locationMode->setItemData(2, tr("Not moved: every object away from its place is an error"), Qt::ToolTipRole);
    m_locationMode->setToolTip(tr("Absolute mode: a drawing that is right but displaced as a whole (most of it matches once "
                                  "moved). Never / Once: it is graded after moving it back. Every time: every object is "
                                  "wrong."));
    fChecks->addRow(tr("Wrong location counts as an error"), m_locationMode);
    m_locationScope = addScope(fChecks, false);

    auto* basic = new QHBoxLayout;
    basic->addWidget(gGrading, 1);
    basic->addWidget(gChecks, 1);
    outer->addLayout(basic);

    // ======================= Advanced =======================
    m_advancedToggle = new QToolButton(this);
    m_advancedToggle->setText(tr("Advanced settings"));
    m_advancedToggle->setCheckable(true);
    m_advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedToggle->setAutoRaise(true);
    outer->addWidget(m_advancedToggle);
    m_advanced = new QWidget(this);
    auto* adv = new QGridLayout(m_advanced);
    adv->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_advanced);

    // Matching details.
    auto* gMatch = new QGroupBox(tr("Matching"), m_advanced);
    auto* fMatch = new QFormLayout(gMatch);
    m_angTol = sciEdit(1e-6 * kRadToDeg);
    m_angTol->setToolTip(tr("How far apart two directions may be and still count as the same"));
    fMatch->addRow(tr("Angle tolerance (degrees)"), m_angTol);
    m_nearMiss = sciEdit(0);
    m_nearMiss->setToolTip(tr("How far apart a wrong item and the reference item may be to be reported as one near miss "
                              "(\"is 4.010 long\") instead of missing + extra. 0 = 10% of the reference size."));
    fMatch->addRow(tr("Near-miss distance"), m_nearMiss);
    m_strictCl = check(tr("Centerlines must have the same length as in the reference"));
    m_strictCl->setToolTip(tr("Off: a centerline counts as correct when it lies on the reference centerline and overlaps "
                              "it, whatever its length. On: its ends must match the reference, like any other line."));
    fMatch->addRow(m_strictCl);

    // Reading drawings.
    auto* gRead = new QGroupBox(tr("Reading drawings"), m_advanced);
    auto* fRead = new QFormLayout(gRead);
    m_paper = check(tr("Use paper space instead of model space"));
    m_skipDefpoints = check(tr("Skip the Defpoints layer"));
    m_ignoreHiddenRef = check(tr("Ignore objects on turned-off or frozen layers in the solution"));
    m_ignoreHiddenRef->setToolTip(tr("What you turned off in the solution drawing (a sketching grid, a dimension layer) "
                                     "is not part of the answer, so students are not expected to have it."));
    m_skipHidden = check(tr("Ignore objects on turned-off or frozen layers in student drawings"));
    m_skipHidden->setToolTip(tr("Off: a student's hidden objects still count, with a warning (turning a layer off "
                                "changes only the display)."));
    for (QCheckBox* c : {m_paper, m_skipDefpoints, m_ignoreHiddenRef, m_skipHidden}) fRead->addRow(c);
    m_ignoreRe = line();
    m_ignoreRe->setPlaceholderText(tr("e.g. ^(CONST|CONSTRUCTION)$"));
    m_ignoreRe->setToolTip(tr("Layers whose names match this regular expression are ignored"));
    fRead->addRow(tr("Ignore layers"), m_ignoreRe);
    m_hiddenPat = line(); m_centerPat = line(); m_phantomPat = line(); m_visiblePat = line();
    for (QLineEdit* e : {m_hiddenPat, m_centerPat, m_phantomPat, m_visiblePat})
        e->setToolTip(tr("Linetype names, separated by ';'. * matches anything."));
    fRead->addRow(tr("Hidden linetypes"), m_hiddenPat);
    fRead->addRow(tr("Center linetypes"), m_centerPat);
    fRead->addRow(tr("Phantom linetypes"), m_phantomPat);
    fRead->addRow(tr("Visible linetypes"), m_visiblePat);

    // Orthographic views.
    auto* gOrtho = new QGroupBox(tr("Orthographic views"), m_advanced);
    gOrtho->setToolTip(tr("Orthographic mode only"));
    auto* fOrtho = new QFormLayout(gOrtho);
    m_orthoScope = addScope(fOrtho, false);
    m_topK = new QSpinBox(this);
    m_topK->setRange(1, 20);
    m_topK->setToolTip(tr("How many possible positions of each reference view are kept when searching the student "
                          "drawing; the best combination across the views is then chosen. More helps when views look "
                          "alike, but is slower."));
    connect(m_topK, &QSpinBox::valueChanged, this, &SettingsPanel::changed);
    fOrtho->addRow(tr("Positions compared for each view"), m_topK);
    m_gapPct = new QDoubleSpinBox(this);
    m_gapPct->setRange(0.1, 20);
    m_gapPct->setSuffix(QStringLiteral(" %"));
    m_gapPct->setDecimals(1);
    m_gapPct->setToolTip(tr("Used only to split the reference into views automatically: items closer together than this "
                            "(a share of the drawing's size) belong to the same view."));
    connect(m_gapPct, &QDoubleSpinBox::valueChanged, this, &SettingsPanel::changed);
    fOrtho->addRow(tr("Minimum gap between views"), m_gapPct);
    m_alignTol = sciEdit(0);
    m_alignTol->setToolTip(tr("0 = use the tolerance"));
    fOrtho->addRow(tr("Alignment tolerance"), m_alignTol);
    const QStringList sev{tr("Error"), tr("Warning"), tr("Off")};
    m_alignSev = combo(sev);
    m_arrangeSev = combo(sev);
    fOrtho->addRow(tr("Views not aligned"), m_alignSev);
    fOrtho->addRow(tr("Views arranged wrongly"), m_arrangeSev);

    // Copy detection, including the starter files handed out to students.
    auto* gCopy = new QGroupBox(tr("Copy detection"), m_advanced);
    auto* fCopy = new QFormLayout(gCopy);
    m_templates = new QListWidget(this);
    m_templates->setMaximumHeight(70);
    m_templates->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_templates->setToolTip(tr("Starter drawings handed out to students. Files built from them share their internal IDs, "
                               "which are then not flagged as copying."));
    auto* tplButtons = new QHBoxLayout;
    auto* tplAdd = new QPushButton(tr("Add…"), this);
    auto* tplRemove = new QPushButton(tr("Remove"), this);
    tplButtons->addWidget(tplAdd);
    tplButtons->addWidget(tplRemove);
    tplButtons->addStretch();
    auto* tplBox = new QWidget(this);
    auto* tplLay = new QVBoxLayout(tplBox);
    tplLay->setContentsMargins(0, 0, 0, 0);
    tplLay->addWidget(m_templates);
    tplLay->addLayout(tplButtons);
    fCopy->addRow(tr("Template files"), tplBox);
    connect(tplAdd, &QPushButton::clicked, this, [this]() {
        const QStringList ps = QFileDialog::getOpenFileNames(this, tr("Template drawings"), QString(), tr("Drawings (*.dwg *.json)"));
        bool added = false;
        for (const QString& p : ps)
            if (m_templates->findItems(p, Qt::MatchExactly).isEmpty()) {
                m_templates->addItem(p);
                added = true;
            }
        if (added) emit changed();
    });
    connect(tplRemove, &QPushButton::clicked, this, [this]() {
        if (m_templates->selectedItems().isEmpty()) return;
        qDeleteAll(m_templates->selectedItems());
        emit changed();
    });
    m_templateShare = new QDoubleSpinBox(this);
    m_templateShare->setRange(1, 100);
    m_templateShare->setSuffix(QStringLiteral(" %"));
    m_templateShare->setToolTip(tr("A file ID shared by more than this share of the class is treated as coming from a "
                                   "template, not as copying"));
    connect(m_templateShare, &QDoubleSpinBox::valueChanged, this, &SettingsPanel::changed);
    fCopy->addRow(tr("Probable template above"), m_templateShare);
    m_smallEdit = new QDoubleSpinBox(this);
    m_smallEdit->setRange(0, 600);
    m_smallEdit->setSuffix(tr(" min"));
    m_smallEdit->setToolTip(tr("A mostly correct drawing edited for less than this long is noted"));
    connect(m_smallEdit, &QDoubleSpinBox::valueChanged, this, &SettingsPanel::changed);
    fCopy->addRow(tr("Note editing time under"), m_smallEdit);
    m_identity = line();
    m_identity->setToolTip(tr("Regular expression that finds the student's name in the file name"));
    fCopy->addRow(tr("Student name pattern"), m_identity);

    // AutoCAD and files.
    auto* gTools = new QGroupBox(tr("AutoCAD and files"), m_advanced);
    auto* fTools = new QFormLayout(gTools);
    m_acadCombo = new QComboBox(this);
    auto* acadRow = new QWidget(this);
    auto* ah = new QHBoxLayout(acadRow);
    ah->setContentsMargins(0, 0, 0, 0);
    ah->addWidget(m_acadCombo, 1);
    auto* locate = new QPushButton(tr("Browse…"), acadRow);
    ah->addWidget(locate);
    fTools->addRow(tr("AutoCAD"), acadRow);
    m_protect = check(tr("Open a read-only copy (protect the evidence)"));
    fTools->addRow(m_protect);
    m_timeout = new QSpinBox(this);
    m_timeout->setRange(5, 3600);
    m_timeout->setSuffix(tr(" s"));
    m_timeout->setToolTip(tr("If reading one submission takes longer than this, that file is listed as unreadable and "
                             "the batch goes on."));
    connect(m_timeout, &QSpinBox::valueChanged, this, &SettingsPanel::changed);
    fTools->addRow(tr("Give up reading a file after"), m_timeout);
    m_oda = line();
    m_oda->setPlaceholderText(tr("optional fallback for unreadable files"));
    m_oda->setToolTip(tr("ODA File Converter is a free tool from the Open Design Alliance (a separate download). "
                         "When dwgread cannot read a submission, the checker reads a copy rewritten by it instead. "
                         "Leave this empty unless some files fail to load."));
    fTools->addRow(tr("ODA File Converter"), withBrowse(m_oda, tr("Locate ODA File Converter"), this));
    connect(locate, &QPushButton::clicked, this, [this]() {
        if (m_acad->locate(this)) refreshAutoCad();
    });
    connect(m_acadCombo, &QComboBox::activated, this, [this](int i) { m_acad->setCurrentExe(m_acadCombo->itemData(i).toString()); });
    connect(m_protect, &QCheckBox::toggled, this, [this](bool on) { m_acad->setProtectEvidence(on); });

    adv->addWidget(gMatch, 0, 0);
    adv->addWidget(gRead, 0, 1);
    adv->addWidget(gOrtho, 0, 2);
    adv->addWidget(gCopy, 1, 0, 1, 2);
    adv->addWidget(gTools, 1, 2);
    for (int c = 0; c < 3; ++c) adv->setColumnStretch(c, 1);

    // Advanced starts collapsed, or as it was left.
    const bool open = QSettings().value(kAdvancedKey, false).toBool();
    m_advanced->setVisible(open);
    m_advancedToggle->setChecked(open);
    m_advancedToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    connect(m_advancedToggle, &QToolButton::toggled, this, [this](bool on) {
        m_advanced->setVisible(on);
        m_advancedToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        QSettings().setValue(kAdvancedKey, on);
        emit sizeChanged();
    });
    connect(m_mode, &QComboBox::currentIndexChanged, this, &SettingsPanel::updateModeHelp);
    for (QWidget* w : findChildren<QWidget*>())
        if (qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSpinBox*>(w)) {
            w->setFocusPolicy(Qt::StrongFocus);  // not focused by the wheel
            w->installEventFilter(this);
        }

    refreshAutoCad();
    setSettings(AppSettings{});
}

void SettingsPanel::updateModeHelp() {
    const auto mode = static_cast<Mode>(m_mode->currentIndex());
    switch (mode) {
    case Mode::Absolute: m_modeHelp->setText(tr("Every item must be at the same coordinates as in the reference.")); break;
    case Mode::Relative:
        m_modeHelp->setText(tr("The drawing may sit anywhere (and be rotated or mirrored if allowed below); sizes and "
                               "the positions of items relative to each other must match."));
        break;
    case Mode::Orthographic:
        m_modeHelp->setText(tr("A multi-view drawing: each view is found and compared on its own, then the views' "
                               "alignment and arrangement are checked. Use \"Edit views…\" to confirm the reference views."));
        break;
    }
    m_editViews->setEnabled(mode == Mode::Orthographic);
    updateScopes();
}

SettingsPanel::Scope SettingsPanel::addScope(QFormLayout* form, bool indent) {
    auto* l = new QLabel(this);
    l->setWordWrap(true);
    l->setForegroundRole(QPalette::PlaceholderText);
    QWidget* row = indent ? indented(l, this) : l;
    form->addRow(row);
    form->setRowVisible(row, false);
    return {form, row, l};
}

void SettingsPanel::updateScopes() {
    const bool multi = m_drawingPaths.size() > 1;
    const auto mode = static_cast<Mode>(m_mode->currentIndex());
    bool resized = false;
    // One shared setting: which drawings use it (by their own mode or checks), why, what to say
    // when none does, and the controls to enable. With one drawing the single Mode and checks decide.
    auto apply = [&](Scope& sc, const std::function<bool(const DrawingSettings&)>& uses, bool usedBySingle,
                     const QString& why, const QString& none, std::initializer_list<QWidget*> controls) {
        QStringList names;
        for (const QString& p : m_drawingPaths)
            if (uses(m_perDrawing[AppSettings::drawingKey(p)])) names << QFileInfo(p).completeBaseName();
        const bool used = multi ? !names.isEmpty() : usedBySingle;
        for (QWidget* w : controls) w->setEnabled(used);
        QString text;
        if (multi && names.isEmpty()) text = tr("Not used: %1").arg(none);
        else if (multi && names.size() < m_drawingPaths.size()) text = tr("Only for %1 (%2)").arg(names.join(QStringLiteral(", ")), why);
        sc.label->setText(text);
        if (sc.form->isRowVisible(sc.row) != !text.isEmpty()) {
            sc.form->setRowVisible(sc.row, !text.isEmpty());
            resized = true;
        }
    };
    apply(m_rotationScope, [](const DrawingSettings& d) { return d.mode == Mode::Relative; }, mode == Mode::Relative,
          tr("Relative mode"), tr("no drawing is graded in Relative mode"), {m_rotation, m_mirror});
    apply(m_locationScope, [](const DrawingSettings& d) { return d.mode == Mode::Absolute; }, mode == Mode::Absolute,
          tr("Absolute mode"), tr("no drawing is graded in Absolute mode"), {m_locationMode});
    apply(m_orthoScope, [](const DrawingSettings& d) { return d.mode == Mode::Orthographic; }, mode == Mode::Orthographic,
          tr("Orthographic mode"), tr("no drawing is graded in Orthographic mode"),
          {m_topK, m_gapPct, m_alignTol, m_alignSev, m_arrangeSev});
    apply(m_dimPrecScope, [](const DrawingSettings& d) { return d.checkDimensions; }, m_dims->isChecked(),
          tr("dimensions checked"), tr("no drawing has dimensions checked"), {m_dimPrec});
    apply(m_hatchPatternScope, [](const DrawingSettings& d) { return d.checkHatches; }, m_hatches->isChecked(),
          tr("hatches checked"), tr("no drawing has hatches checked"), {m_hatchPattern});
    if (resized) emit sizeChanged();
}

void SettingsPanel::setSolutionDrawings(const QStringList& paths) {
    m_drawingPaths = paths;
    rebuildDrawingRows();
    applyDrawingLayout();
}

// The table of solution drawings: name, mode, dimensions, hatches, Edit views….
void SettingsPanel::rebuildDrawingRows() {
    while (QLayoutItem* it = m_drawingsGrid->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    if (m_drawingPaths.size() < 2) return;
    const int headerRow = 0;
    const QStringList heads{tr("Drawing"), tr("Mode"), tr("Dimensions"), tr("Hatches"), QString()};
    for (int c = 0; c < heads.size(); ++c) {
        auto* h = new QLabel(heads[c], m_drawingsBox);
        h->setForegroundRole(QPalette::PlaceholderText);
        m_drawingsGrid->addWidget(h, headerRow, c);
    }
    for (int i = 0; i < static_cast<int>(m_drawingPaths.size()); ++i) {
        const QString path = m_drawingPaths[i];
        const QString key = AppSettings::drawingKey(path);
        if (!m_perDrawing.count(key)) {  // a new drawing starts from the current choices
            DrawingSettings ds;
            ds.mode = static_cast<Mode>(m_mode->currentIndex());
            ds.checkDimensions = m_dims->isChecked();
            ds.checkHatches = m_hatches->isChecked();
            m_perDrawing[key] = ds;
        }
        const DrawingSettings ds = m_perDrawing[key];
        auto* name = new QLabel(QFileInfo(path).fileName(), m_drawingsBox);
        name->setToolTip(QDir::toNativeSeparators(path));
        auto* mode = new QComboBox(m_drawingsBox);
        mode->addItems({tr("Absolute"), tr("Relative"), tr("Orthographic")});
        mode->setCurrentIndex(static_cast<int>(ds.mode));
        mode->setFocusPolicy(Qt::StrongFocus);
        mode->installEventFilter(this);
        auto* dims = new QCheckBox(m_drawingsBox);
        dims->setChecked(ds.checkDimensions);
        auto* hatches = new QCheckBox(m_drawingsBox);
        hatches->setChecked(ds.checkHatches);
        auto* views = new QPushButton(tr("Edit views…"), m_drawingsBox);
        views->setEnabled(ds.mode == Mode::Orthographic);
        m_drawingsGrid->addWidget(name, i + 1, 0);
        m_drawingsGrid->addWidget(mode, i + 1, 1);
        m_drawingsGrid->addWidget(dims, i + 1, 2, Qt::AlignHCenter);
        m_drawingsGrid->addWidget(hatches, i + 1, 3, Qt::AlignHCenter);
        m_drawingsGrid->addWidget(views, i + 1, 4);
        connect(mode, &QComboBox::currentIndexChanged, this, [this, key, views](int m) {
            m_perDrawing[key].mode = static_cast<Mode>(m);
            views->setEnabled(m == static_cast<int>(Mode::Orthographic));
            updateModeHelp();
            emit changed();
        });
        connect(dims, &QCheckBox::toggled, this, [this, key](bool on) {
            m_perDrawing[key].checkDimensions = on;
            updateScopes();
            emit changed();
        });
        connect(hatches, &QCheckBox::toggled, this, [this, key](bool on) {
            m_perDrawing[key].checkHatches = on;
            updateScopes();
            emit changed();
        });
        connect(views, &QPushButton::clicked, this, [this, path]() { emit editViewsRequested(path); });
    }
    m_drawingsGrid->setColumnStretch(0, 1);
}

// With several solution drawings the table replaces the single Mode and the two main
// checks; the precision and pattern sub-options then apply wherever those are checked,
// and updateScopes() says where.
void SettingsPanel::applyDrawingLayout() {
    const bool multi = m_drawingPaths.size() > 1;
    m_drawingsBox->setVisible(multi);
    m_gradingForm->setRowVisible(m_mode, !multi);
    m_gradingForm->setRowVisible(m_modeHelp, !multi);
    m_gradingForm->setRowVisible(m_editViewsRow, !multi);
    m_checksForm->setRowVisible(m_dims, !multi);
    m_checksForm->setRowVisible(m_hatches, !multi);
    m_dimPrec->setText(multi ? tr("Dimension precision must match (4.00 vs 4.000)") : tr("…and the precision they show (4.00 vs 4.000)"));
    m_hatchPattern->setText(multi ? tr("Hatch pattern must match") : tr("…and with the same hatch pattern"));
    updateModeHelp();  // and the "Only for …" notes
    emit sizeChanged();
}

// Drop-downs and number boxes take the mouse wheel only once clicked into; otherwise the
// wheel scrolls the settings, so scrolling past them never changes a value.
bool SettingsPanel::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::Wheel) {
        auto* w = qobject_cast<QWidget*>(o);
        if (w && !w->hasFocus()) {
            for (QWidget* p = parentWidget(); p; p = p->parentWidget())
                if (auto* area = qobject_cast<QAbstractScrollArea*>(p)) {
                    QCoreApplication::sendEvent(area->viewport(), e);
                    break;
                }
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void SettingsPanel::refreshAutoCad() {
    m_acadCombo->clear();
    const auto cur = m_acad->current();
    bool listed = false;
    for (const auto& i : m_acad->installs()) {
        m_acadCombo->addItem(i.name + (i.lt ? tr(" (LT)") : QString()), i.exe);
        listed = listed || i.exe == cur.exe;
    }
    if (!cur.exe.isEmpty() && !listed) m_acadCombo->addItem(cur.name, cur.exe);
    if (m_acadCombo->count() == 0) m_acadCombo->addItem(tr("(not found: use Browse)"), QString());
    const int idx = m_acadCombo->findData(cur.exe);
    if (idx >= 0) m_acadCombo->setCurrentIndex(idx);
    m_protect->blockSignals(true);
    m_protect->setChecked(m_acad->protectEvidence());
    m_protect->blockSignals(false);
}

void SettingsPanel::setSettings(const AppSettings& s) {
    const QSignalBlocker b(this);
    QList<QWidget*> kids = findChildren<QWidget*>();
    for (QWidget* w : kids) w->blockSignals(true);
    m_mode->setCurrentIndex(static_cast<int>(s.compare.mode));
    m_matchLt->setChecked(s.compare.matchLinetype);
    m_rotation->setChecked(s.compare.allowRotation);
    m_mirror->setChecked(s.compare.allowMirror);
    m_linTol->setText(QString::number(s.compare.linTol, 'g', 6));
    m_angTol->setText(QString::number(s.compare.angTol * kRadToDeg, 'g', 6));
    m_layerMatch->setChecked(s.compare.requireLayerMatch);
    m_extras->setCurrentIndex(s.compare.extras == ExtrasPolicy::Penalize ? 0 : 1);
    m_dims->setChecked(s.compare.checkDimensions);
    m_dimPrec->setChecked(s.compare.checkDimPrecision);
    m_dimPrec->setEnabled(s.compare.checkDimensions);
    m_strictCl->setChecked(s.compare.strictCenterlines);
    m_hatches->setChecked(s.compare.checkHatches);
    m_hatchPattern->setChecked(s.compare.checkHatchPattern);
    m_hatchPattern->setEnabled(s.compare.checkHatches);
    m_scaleMode->setCurrentIndex(!s.compare.detectScale ? 2 : s.compare.scalePenaltyErrors <= 0 ? 0 : 1);
    m_locationMode->setCurrentIndex(!s.compare.detectLocation ? 2 : s.compare.locationPenaltyErrors <= 0 ? 0 : 1);
    m_nearMiss->setText(QString::number(s.compare.nearMissRadius, 'g', 6));
    m_ignoreRe->setText(s.canon.layerIgnoreRegex);
    m_paper->setChecked(s.canon.usePaperSpace);
    m_skipDefpoints->setChecked(s.canon.skipDefpoints);
    m_skipHidden->setChecked(s.canon.skipHiddenLayers);
    m_ignoreHiddenRef->setChecked(s.canon.ignoreHiddenLayersInReference);
    m_hiddenPat->setText(s.canon.hiddenPatterns.join("; "));
    m_centerPat->setText(s.canon.centerPatterns.join("; "));
    m_phantomPat->setText(s.canon.phantomPatterns.join("; "));
    m_visiblePat->setText(s.canon.visiblePatterns.join("; "));
    m_topK->setValue(s.compare.orthoTopK);
    m_gapPct->setValue(s.compare.orthoGapFraction * 100);
    m_alignTol->setText(QString::number(s.compare.alignTol, 'g', 6));
    m_alignSev->setCurrentIndex(static_cast<int>(s.compare.alignmentSeverity));
    m_arrangeSev->setCurrentIndex(static_cast<int>(s.compare.arrangementSeverity));
    m_templates->clear();
    m_templates->addItems(s.duplicates.templateFiles);
    m_templateShare->setValue(s.duplicates.templateShareFraction * 100);
    m_smallEdit->setValue(s.duplicates.smallEditMinutes);
    m_identity->setText(s.identityRegex);
    m_timeout->setValue(s.loader.timeoutSec);
    m_oda->setText(s.loader.odaConverterPath);
    for (QWidget* w : kids) w->blockSignals(false);
    m_perDrawing = s.perDrawing;
    rebuildDrawingRows();
    applyDrawingLayout();
}

AppSettings SettingsPanel::settings() const {
    AppSettings s;
    s.compare.mode = static_cast<Mode>(m_mode->currentIndex());
    s.compare.matchLinetype = m_matchLt->isChecked();
    s.compare.allowRotation = m_rotation->isChecked();
    s.compare.allowMirror = m_mirror->isChecked();
    s.compare.linTol = sciValue(m_linTol, 1e-4);
    s.compare.angTol = sciValue(m_angTol, 1e-6 * kRadToDeg) / kRadToDeg;
    s.compare.requireLayerMatch = m_layerMatch->isChecked();
    s.compare.extras = m_extras->currentIndex() == 0 ? ExtrasPolicy::Penalize : ExtrasPolicy::ReportOnly;
    s.compare.checkDimensions = m_dims->isChecked();
    s.compare.checkDimPrecision = m_dimPrec->isChecked();  // kept as set; it applies only with checkDimensions
    s.compare.strictCenterlines = m_strictCl->isChecked();
    s.compare.checkHatches = m_hatches->isChecked();
    s.compare.checkHatchPattern = m_hatchPattern->isChecked();  // kept as set; it applies only with checkHatches
    s.compare.detectScale = m_scaleMode->currentIndex() != 2;  // Every time: no rescaling
    s.compare.scalePenaltyErrors = m_scaleMode->currentIndex() == 0 ? 0 : 1;
    s.compare.detectLocation = m_locationMode->currentIndex() != 2;  // Every time: graded where it was drawn
    s.compare.locationPenaltyErrors = m_locationMode->currentIndex() == 0 ? 0 : 1;
    s.compare.nearMissRadius = sciValue(m_nearMiss, 0);
    s.canon.layerIgnoreRegex = m_ignoreRe->text().trimmed();
    s.canon.usePaperSpace = m_paper->isChecked();
    s.canon.skipDefpoints = m_skipDefpoints->isChecked();
    s.canon.skipHiddenLayers = m_skipHidden->isChecked();
    s.canon.ignoreHiddenLayersInReference = m_ignoreHiddenRef->isChecked();
    s.canon.hiddenPatterns = splitPatterns(m_hiddenPat->text());
    s.canon.centerPatterns = splitPatterns(m_centerPat->text());
    s.canon.phantomPatterns = splitPatterns(m_phantomPat->text());
    s.canon.visiblePatterns = splitPatterns(m_visiblePat->text());
    s.compare.orthoTopK = m_topK->value();
    s.compare.orthoGapFraction = m_gapPct->value() / 100.0;
    s.compare.alignTol = sciValue(m_alignTol, 0);
    s.compare.alignmentSeverity = static_cast<Severity>(m_alignSev->currentIndex());
    s.compare.arrangementSeverity = static_cast<Severity>(m_arrangeSev->currentIndex());
    for (int i = 0; i < m_templates->count(); ++i) s.duplicates.templateFiles << m_templates->item(i)->text();
    s.duplicates.templateShareFraction = m_templateShare->value() / 100.0;
    s.duplicates.smallEditMinutes = m_smallEdit->value();
    s.identityRegex = m_identity->text().trimmed().isEmpty() ? AppSettings::defaultIdentityRegex() : m_identity->text().trimmed();
    s.loader.timeoutSec = m_timeout->value();
    s.loader.odaConverterPath = m_oda->text().trimmed();
    s.perDrawing = m_perDrawing;
    return s;
}

}  // namespace dcgui
