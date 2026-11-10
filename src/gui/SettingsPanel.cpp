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
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
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

// A label with an (i) icon after it; both show `tip` when hovered.
QWidget* withInfo(const QString& text, const QString& tip, QWidget* parent, bool dim = false) {
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    auto* l = new QLabel(text, w);
    if (dim) l->setForegroundRole(QPalette::PlaceholderText);
    auto* icon = new QLabel(w);
    const int sz = l->fontMetrics().height();
    icon->setPixmap(w->style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(sz, sz));
    icon->setCursor(Qt::WhatsThisCursor);
    l->setToolTip(tip);
    icon->setToolTip(tip);
    h->addWidget(l);
    h->addWidget(icon);
    h->addStretch();
    return w;
}

QString locationModeTip() {
    return QCoreApplication::translate(
        "SettingsPanel",
        "<b>Where the student's drawing has to be</b><br><br>"
        "<b>Absolute</b>: every item at the same coordinates as in the solution. A drawing that is right but "
        "displaced as a whole is handled by \"Wrong location counts as an error\".<br><br>"
        "<b>Relative</b>: anywhere, and turned or flipped too if rotation or mirroring is allowed. Sizes, and where "
        "the items are relative to each other, must match.<br><br>"
        "<b>Orthographic</b>: a drawing with several views (front, top, side…). Each view is found and compared on "
        "its own, wherever it is; then the views' alignment and arrangement are checked.");
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

    // ======================= Step 2 (placed in the main window) =======================
    // The location mode; with several solution drawings, the table of each one's mode and checks.
    m_modeStep = new QWidget(this);
    m_modeStep->hide();  // shown once MainWindow has put it in its step box
    auto* mLay = new QVBoxLayout(m_modeStep);
    mLay->setContentsMargins(0, 0, 0, 0);
    mLay->addWidget(withInfo(tr("Location mode"), locationModeTip(), m_modeStep, true));
    m_modeNote = new QLabel(tr("Choose how location is graded for this assignment."), m_modeStep);
    m_modeNote->setWordWrap(true);
    QFont noteFont = m_modeNote->font();
    noteFont.setBold(true);
    m_modeNote->setFont(noteFont);
    mLay->addWidget(m_modeNote);
    m_modeForm = new QFormLayout;
    mLay->addLayout(m_modeForm);
    m_mode = combo({tr("Absolute"), tr("Relative"), tr("Orthographic")});
    m_mode->setToolTip(locationModeTip());
    connect(m_mode, &QComboBox::activated, this, [this]() {
        m_modeChosen = true;
        updateModeNote();
        emit modeChoiceChanged();
    });
    m_modeForm->addRow(m_mode);
    m_modeHelp = new QLabel(this);
    m_modeHelp->setWordWrap(true);
    m_modeHelp->setForegroundRole(QPalette::PlaceholderText);
    m_modeForm->addRow(m_modeHelp);
    m_editViews = new QPushButton(tr("Edit views…"), this);
    m_editViews->setToolTip(tr("Confirm or fix how the solution drawing splits into views"));
    connect(m_editViews, &QPushButton::clicked, this, [this]() { emit editViewsRequested(QString()); });
    m_editViewsRow = indented(m_editViews, this);
    m_modeForm->addRow(m_editViewsRow);
    m_drawingsBox = new QWidget(m_modeStep);
    auto* dLay = new QVBoxLayout(m_drawingsBox);
    dLay->setContentsMargins(0, 0, 0, 0);
    auto* dHint = new QLabel(tr("Each drawing's location mode and checks; the settings below apply to all of them."),
                             m_drawingsBox);
    dHint->setForegroundRole(QPalette::PlaceholderText);
    dHint->setWordWrap(true);
    dLay->addWidget(dHint);
    m_drawingsGrid = new QGridLayout;
    dLay->addLayout(m_drawingsGrid);
    m_drawingsBox->setVisible(false);
    mLay->addWidget(m_drawingsBox);

    // ======================= Basic =======================
    // Matching rules: what must match the solution, and what may differ.
    auto* gRules = new QGroupBox(tr("Matching rules"), this);
    auto* fRules = new QFormLayout(gRules);
    m_rulesForm = fRules;
    m_locationMode = combo({tr("Once"), tr("Every time")});
    m_locationMode->setItemData(0, tr("Graded after moving the drawing back, with one error for the location"), Qt::ToolTipRole);
    m_locationMode->setItemData(1, tr("Not moved: every object away from its place is an error"), Qt::ToolTipRole);
    m_locationMode->setToolTip(tr("A drawing that is right but displaced as a whole (most of it matches once moved). "
                                  "Once: it is graded after moving it back, with one error. Every time: every object is wrong."));
    fRules->addRow(tr("Wrong location (in absolute mode) counts as an error"), m_locationMode);
    m_rotation = check(tr("Allow rotation (in relative mode)"));
    m_mirror = check(tr("Allow mirrored (in relative mode)"));
    m_mirror->setToolTip(tr("A drawing flipped over, as with the MIRROR command"));
    fRules->addRow(m_rotation);
    fRules->addRow(m_mirror);
    m_linTol = sciEdit(1e-6);
    m_linTol->setToolTip(tr("How far apart two points may be and still count as the same point"));
    fRules->addRow(tr("Tolerance (drawing units)"), m_linTol);
    m_extras = combo({tr("Count against the score"), tr("List only")});
    m_extras->setToolTip(tr("Items in the drawing that are not in the solution"));
    fRules->addRow(tr("Extra items"), m_extras);
    m_matchLt = check(tr("Linetypes must match (hidden, center, …)"));
    m_matchLt->setToolTip(tr("Each item must have the same kind of linetype as the solution item: visible (continuous), "
                             "hidden, center or phantom (the names are set under Advanced > Reading drawings)."));
    fRules->addRow(m_matchLt);
    m_matchColor = check(tr("Line colors must match"));
    m_matchColor->setToolTip(tr("Each item must have the solution item's color, after ByLayer and ByBlock: an AutoCAD "
                                "color number (ACI) or a true color."));
    fRules->addRow(m_matchColor);
    m_colorTol = new QSpinBox(this);
    m_colorTol->setRange(0, 100);
    m_colorTol->setSuffix(QStringLiteral(" %"));
    m_colorTol->setToolTip(tr("How different two colors may be: their RGB distance as a share of black to white. 0 % = "
                              "the same color. A slightly different red (250,5,5 against 255,0,0) is 2 %, red against "
                              "orange (255,127,0) 29 %, red against blue 82 %."));
    connect(m_colorTol, &QSpinBox::valueChanged, this, &SettingsPanel::changed);
    {
        auto* w = new QWidget(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        auto* l = new QLabel(tr("…allowing a color difference up to"), w);
        l->setToolTip(m_colorTol->toolTip());
        h->addWidget(l);
        h->addWidget(m_colorTol);
        h->addStretch();
        m_colorTolRow = indented(w, this);
        fRules->addRow(m_colorTolRow);
    }
    connect(m_matchColor, &QCheckBox::toggled, this, &SettingsPanel::updateScopes);
    m_byLayer = check(tr("Linetype and color must be set by layer"));
    m_byLayer->setToolTip(tr("Each item's color and linetype must come from its layer (ByLayer), not be set on the "
                             "object. Inside a block, ByBlock counts when the block itself is by layer."));
    fRules->addRow(m_byLayer);
    m_layerMatch = check(tr("Layers must match"));
    m_layerMatch->setToolTip(tr("Each item must be on a layer with the same name as the solution item's (letter case "
                                "ignored). Off: any layer will do."));
    fRules->addRow(m_layerMatch);
    m_dims = check(tr("Check dimensions"));
    m_dimPrec = check(tr("…and the precision they show (4.00 vs 4.000)"));
    m_dimPrec->setToolTip(tr("A dimension with the right value must also show the same decimal places (or fraction "
                             "precision) as the solution, after its style, overrides and zero suppression."));
    fRules->addRow(m_dims);
    m_dimPrecRow = indented(m_dimPrec, this);
    fRules->addRow(m_dimPrecRow);
    m_dimPrecScope = addScope(fRules, true);
    connect(m_dims, &QCheckBox::toggled, this, &SettingsPanel::updateScopes);
    m_hatches = check(tr("Check hatches (each hatched region is hatched)"));
    m_hatchPattern = check(tr("…and with the same hatch pattern"));
    fRules->addRow(m_hatches);
    m_hatchPatternRow = indented(m_hatchPattern, this);
    fRules->addRow(m_hatchPatternRow);
    m_hatchPatternScope = addScope(fRules, true);
    connect(m_hatches, &QCheckBox::toggled, this, &SettingsPanel::updateScopes);
    m_scaleMode = combo({tr("Never"), tr("Once"), tr("Every time")});
    m_scaleMode->setItemData(0, tr("Graded after undoing the scale, with no error for the scale itself"), Qt::ToolTipRole);
    m_scaleMode->setItemData(1, tr("Graded after undoing the scale, with one error for the scale"), Qt::ToolTipRole);
    m_scaleMode->setItemData(2, tr("Not rescaled: every object drawn at the wrong size is an error"), Qt::ToolTipRole);
    m_scaleMode->setToolTip(tr("A drawing at the wrong scale, e.g. in millimeters instead of inches, or twice too large. "
                               "Never / Once: it is graded after undoing the scale. Every time: every object is wrong."));
    fRules->addRow(tr("Wrong scale counts as an error"), m_scaleMode);
    outer->addWidget(gRules);

    // ======================= Advanced =======================
    m_advancedToggle = new QToolButton(this);
    m_advancedToggle->setText(tr("Advanced settings"));
    m_advancedToggle->setCheckable(true);
    m_advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedToggle->setAutoRaise(true);
    outer->addWidget(m_advancedToggle);
    m_advanced = new QWidget(this);
    auto* adv = new QVBoxLayout(m_advanced);
    adv->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_advanced);

    // Matching details.
    auto* gMatch = new QGroupBox(tr("More matching rules"), m_advanced);
    auto* fMatch = new QFormLayout(gMatch);
    m_angTol = sciEdit(1e-6);
    m_angTol->setToolTip(tr("How far apart two directions may be and still count as the same"));
    fMatch->addRow(tr("Angle tolerance (degrees)"), m_angTol);
    m_nearMiss = sciEdit(0);
    m_nearMiss->setToolTip(tr("When an item does not match, how far from the solution item it may be and still be "
                              "reported as one error about that item (\"is 4.010 long\", \"is shifted by …\") rather "
                              "than as a missing item plus an extra one. 0 = 10% of the solution's size. Items of the "
                              "right size are paired at any distance anyway (\"is shifted by …\")."));
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
    m_orthoBox = gOrtho;  // shown only when a drawing is graded in Orthographic mode
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

    // Cheating detection, including the starter files handed out to students.
    auto* gCopy = new QGroupBox(tr("Cheating detection"), m_advanced);
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
    auto* gTools = new QGroupBox(tr("Viewing submissions in AutoCAD"), m_advanced);
    auto* fTools = new QFormLayout(gTools);
    m_acadCombo = new QComboBox(this);
    auto* acadRow = new QWidget(this);
    auto* ah = new QHBoxLayout(acadRow);
    ah->setContentsMargins(0, 0, 0, 0);
    ah->addWidget(m_acadCombo, 1);
    auto* locate = new QPushButton(tr("Browse…"), acadRow);
    ah->addWidget(locate);
    fTools->addRow(tr("AutoCAD"), acadRow);
    m_protect = check(tr("Open as read-only (protect the evidence)"));
    fTools->addRow(m_protect);
    m_timeout = new QSpinBox(this);
    m_timeout->setRange(5, 3600);
    m_timeout->setSuffix(tr(" s"));
    m_timeout->setToolTip(tr("If reading one submission takes longer than this, that file is listed as unreadable and "
                             "the batch goes on."));
    connect(m_timeout, &QSpinBox::valueChanged, this, &SettingsPanel::changed);
    fRead->addRow(tr("Give up reading a file after"), m_timeout);
    m_oda = line();
    m_oda->setPlaceholderText(tr("optional fallback for unreadable files"));
    m_oda->setToolTip(tr("ODA File Converter is a free tool from the Open Design Alliance (a separate download). "
                         "When dwgread cannot read a submission, the checker reads a copy rewritten by it instead. "
                         "Leave this empty unless some files fail to load."));
    fRead->addRow(tr("ODA File Converter"), withBrowse(m_oda, tr("Locate ODA File Converter"), this));
    connect(locate, &QPushButton::clicked, this, [this]() {
        if (m_acad->locate(this)) refreshAutoCad();
    });
    connect(m_acadCombo, &QComboBox::activated, this, [this](int i) { m_acad->setCurrentExe(m_acadCombo->itemData(i).toString()); });
    connect(m_protect, &QCheckBox::toggled, this, [this](bool on) { m_acad->setProtectEvidence(on); });

    for (QGroupBox* g : {gMatch, gRead, gOrtho, gCopy, gTools}) adv->addWidget(g);

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
    outer->addStretch();
    // In a narrow column a long label goes above its field instead of squeezing it.
    for (QFormLayout* f : findChildren<QFormLayout*>()) f->setRowWrapPolicy(QFormLayout::WrapLongRows);
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
    case Mode::Absolute: m_modeHelp->setText(tr("Every item must be at the same coordinates as in the solution.")); break;
    case Mode::Relative:
        m_modeHelp->setText(tr("The drawing may sit anywhere (and be rotated or mirrored if allowed below); sizes and "
                               "the positions of items relative to each other must match."));
        break;
    case Mode::Orthographic:
        m_modeHelp->setText(tr("A multi-view drawing: each view is found and compared on its own, then the views' "
                               "alignment and arrangement are checked. Use \"Edit views…\" to confirm the solution's views."));
        break;
    }
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

DrawingContent SettingsPanel::contentOf(const QString& key) const {
    const auto it = m_contents.find(key);
    return it == m_contents.end() ? DrawingContent{} : it->second;
}

void SettingsPanel::setDrawingContents(const std::map<QString, DrawingContent>& byKey) {
    m_contents = byKey;
    updateScopes();
}

void SettingsPanel::updateScopes() {
    const bool multi = m_drawingPaths.size() > 1;
    const auto mode = static_cast<Mode>(m_mode->currentIndex());
    const DrawingContent one = m_drawingPaths.isEmpty() ? DrawingContent{} : contentOf(AppSettings::drawingKey(m_drawingPaths.front()));
    bool resized = false;
    auto showRow = [&](QFormLayout* f, QWidget* row, bool on) {
        if (f->isRowVisible(row) == on) return;
        f->setRowVisible(row, on);
        resized = true;
    };
    // The one drawing's own mode and checks; with several, the table has them.
    showRow(m_modeForm, m_mode, !multi);
    showRow(m_modeForm, m_modeHelp, !multi);
    showRow(m_modeForm, m_editViewsRow, !multi && mode == Mode::Orthographic);
    showRow(m_rulesForm, m_colorTolRow, m_matchColor->isChecked());
    showRow(m_rulesForm, m_dims, !multi && one.dims);
    showRow(m_rulesForm, m_hatches, !multi && one.hatches);

    // A shared setting that only some drawings use (by their mode, checks and contents): shown
    // only when one does, with "Only for …" when not all do. With one drawing, it decides.
    using Uses = std::function<bool(const DrawingSettings&, const DrawingContent&)>;
    auto scope = [&](Scope& sc, const Uses& uses, bool usedBySingle, const QString& why) {
        QStringList names;
        for (const QString& p : m_drawingPaths) {
            const QString key = AppSettings::drawingKey(p);
            if (uses(m_perDrawing[key], contentOf(key))) names << QFileInfo(p).completeBaseName();
        }
        const bool used = multi ? !names.isEmpty() : usedBySingle;
        const bool some = used && multi && names.size() < m_drawingPaths.size();
        sc.label->setText(some ? tr("Only for %1 (%2)").arg(names.join(QStringLiteral(", ")), why) : QString());
        showRow(sc.form, sc.row, some);
        return used;
    };
    // Rotation and wrong location say their mode in their labels; shown when a drawing uses it.
    auto anyMode = [&](Mode m) {
        if (!multi) return mode == m;
        for (const QString& p : m_drawingPaths)
            if (m_perDrawing[AppSettings::drawingKey(p)].mode == m) return true;
        return false;
    };
    const bool relative = anyMode(Mode::Relative);
    showRow(m_rulesForm, m_rotation, relative);
    showRow(m_rulesForm, m_mirror, relative);
    showRow(m_rulesForm, m_locationMode, anyMode(Mode::Absolute));
    const bool ortho = scope(
        m_orthoScope, [](const DrawingSettings& d, const DrawingContent&) { return d.mode == Mode::Orthographic; },
        mode == Mode::Orthographic, tr("Orthographic mode"));
    if (m_orthoBox->isHidden() == ortho) {
        m_orthoBox->setVisible(ortho);
        resized = true;
    }
    const bool prec = scope(
        m_dimPrecScope, [](const DrawingSettings& d, const DrawingContent& c) { return d.checkDimensions && c.dims; },
        m_dims->isChecked() && one.dims, tr("dimensions checked"));
    showRow(m_rulesForm, m_dimPrecRow, prec);
    const bool pattern = scope(
        m_hatchPatternScope, [](const DrawingSettings& d, const DrawingContent& c) { return d.checkHatches && c.hatches; },
        m_hatches->isChecked() && one.hatches, tr("hatches checked"));
    showRow(m_rulesForm, m_hatchPatternRow, pattern);

    // The table: a drawing's Dims. / Hatches box only when it has some, Edit views… only in
    // Orthographic mode; a column with nothing left goes away.
    bool anyDims = false, anyHatches = false;
    for (const DrawingRow& row : m_rows) {
        const DrawingContent c = contentOf(row.key);
        row.dims->setVisible(c.dims);
        row.hatches->setVisible(c.hatches);
        row.views->setVisible(m_perDrawing[row.key].mode == Mode::Orthographic);
        anyDims = anyDims || c.dims;
        anyHatches = anyHatches || c.hatches;
    }
    if (m_headDims) m_headDims->setVisible(anyDims);
    if (m_headHatches) m_headHatches->setVisible(anyHatches);
    if (resized) emit sizeChanged();
}

bool SettingsPanel::modeChosen() const {
    if (m_drawingPaths.size() < 2) return m_modeChosen;
    for (const QString& p : m_drawingPaths)
        if (!m_chosenKeys.contains(AppSettings::drawingKey(p))) return false;
    return true;
}

void SettingsPanel::setModeChosen(bool chosen) {
    m_modeChosen = chosen;
    m_chosenKeys.clear();
    if (chosen)
        for (const QString& p : m_drawingPaths) m_chosenKeys.insert(AppSettings::drawingKey(p));
    updateModeNote();
    emit modeChoiceChanged();
}

void SettingsPanel::updateModeNote() {
    const bool multi = m_drawingPaths.size() > 1;
    m_modeNote->setText(multi ? tr("Choose the location mode of each drawing for this assignment.")
                              : tr("Choose the location mode for this assignment."));
    m_modeNote->setVisible(!m_drawingPaths.isEmpty() && !modeChosen());
}

void SettingsPanel::setSolutionDrawings(const QStringList& paths) {
    m_drawingPaths = paths;
    m_contents.clear();  // not read yet
    m_modeChosen = false;
    m_chosenKeys.clear();
    rebuildDrawingRows();
    applyDrawingLayout();
}

// The table of solution drawings: name, location mode, dimensions, hatches, Edit views….
void SettingsPanel::rebuildDrawingRows() {
    m_rows.clear();
    m_headDims = m_headHatches = nullptr;
    while (QLayoutItem* it = m_drawingsGrid->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    if (m_drawingPaths.size() < 2) return;
    auto head = [this](const QString& text) {
        auto* h = new QLabel(text, m_drawingsBox);
        h->setForegroundRole(QPalette::PlaceholderText);
        return h;
    };
    m_drawingsGrid->addWidget(head(tr("Drawing")), 0, 0);
    m_drawingsGrid->addWidget(head(tr("Location mode")), 0, 1);
    m_headDims = head(tr("Dims."));
    m_headDims->setToolTip(tr("Check dimensions"));
    m_drawingsGrid->addWidget(m_headDims, 0, 2, Qt::AlignHCenter);
    m_headHatches = head(tr("Hatches"));
    m_headHatches->setToolTip(tr("Check hatches"));
    m_drawingsGrid->addWidget(m_headHatches, 0, 3, Qt::AlignHCenter);
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
        auto* name = new QLabel(m_drawingsBox);
        name->setText(name->fontMetrics().elidedText(QFileInfo(path).fileName(), Qt::ElideMiddle, 170));
        name->setToolTip(QDir::toNativeSeparators(path));
        auto* mode = new QComboBox(m_drawingsBox);
        mode->addItems({tr("Absolute"), tr("Relative"), tr("Orthographic")});
        mode->setCurrentIndex(static_cast<int>(ds.mode));
        mode->setFocusPolicy(Qt::StrongFocus);
        mode->installEventFilter(this);
        auto* dims = new QCheckBox(m_drawingsBox);
        dims->setChecked(ds.checkDimensions);
        dims->setToolTip(tr("Check dimensions"));
        auto* hatches = new QCheckBox(m_drawingsBox);
        hatches->setChecked(ds.checkHatches);
        hatches->setToolTip(tr("Check hatches"));
        auto* views = new QPushButton(tr("Edit views…"), m_drawingsBox);
        m_drawingsGrid->addWidget(name, i + 1, 0);
        m_drawingsGrid->addWidget(mode, i + 1, 1);
        m_drawingsGrid->addWidget(dims, i + 1, 2, Qt::AlignHCenter);
        m_drawingsGrid->addWidget(hatches, i + 1, 3, Qt::AlignHCenter);
        m_drawingsGrid->addWidget(views, i + 1, 4);
        m_rows.push_back({key, dims, hatches, views});
        connect(mode, &QComboBox::currentIndexChanged, this, [this, key](int m) {
            m_perDrawing[key].mode = static_cast<Mode>(m);
            updateModeHelp();  // and what applies now
            emit changed();
        });
        connect(mode, &QComboBox::activated, this, [this, key]() {
            m_chosenKeys.insert(key);
            updateModeNote();
            emit modeChoiceChanged();
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

// With several solution drawings the table replaces the single Location mode and the two
// main checks; the precision and pattern sub-options then apply wherever those are checked,
// and updateScopes() says where.
void SettingsPanel::applyDrawingLayout() {
    const bool multi = m_drawingPaths.size() > 1;
    m_drawingsBox->setVisible(multi);
    m_dimPrec->setText(multi ? tr("Dimension precision must match (4.00 vs 4.000)") : tr("…and the precision they show (4.00 vs 4.000)"));
    m_hatchPattern->setText(multi ? tr("Hatch pattern must match") : tr("…and with the same hatch pattern"));
    updateModeHelp();  // and which settings apply
    updateModeNote();
    emit sizeChanged();
}

// Drop-downs and number boxes take the mouse wheel only once clicked into; otherwise the
// wheel scrolls the settings, so scrolling past them never changes a value.
bool SettingsPanel::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::Wheel) {
        auto* w = qobject_cast<QWidget*>(o);
        if (w && !w->hasFocus()) {
            for (QWidget* p = w->parentWidget(); p; p = p->parentWidget())
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
    kids += m_modeStep->findChildren<QWidget*>();  // step 2 may live in the main window
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
    m_strictCl->setChecked(s.compare.strictCenterlines);
    m_hatches->setChecked(s.compare.checkHatches);
    m_hatchPattern->setChecked(s.compare.checkHatchPattern);
    m_scaleMode->setCurrentIndex(!s.compare.detectScale ? 2 : s.compare.scalePenaltyErrors <= 0 ? 0 : 1);
    m_locationMode->setCurrentIndex(s.compare.detectLocation ? 0 : 1);
    m_matchColor->setChecked(s.compare.matchColor);
    m_colorTol->setValue(static_cast<int>(std::lround(s.compare.colorTolerance)));
    m_byLayer->setChecked(s.compare.requireByLayer);
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
    s.compare.linTol = sciValue(m_linTol, 1e-6);
    s.compare.angTol = sciValue(m_angTol, 1e-6) / kRadToDeg;
    s.compare.requireLayerMatch = m_layerMatch->isChecked();
    s.compare.extras = m_extras->currentIndex() == 0 ? ExtrasPolicy::Penalize : ExtrasPolicy::ReportOnly;
    s.compare.checkDimensions = m_dims->isChecked();
    s.compare.checkDimPrecision = m_dimPrec->isChecked();  // kept as set; it applies only with checkDimensions
    s.compare.strictCenterlines = m_strictCl->isChecked();
    s.compare.checkHatches = m_hatches->isChecked();
    s.compare.checkHatchPattern = m_hatchPattern->isChecked();  // kept as set; it applies only with checkHatches
    s.compare.detectScale = m_scaleMode->currentIndex() != 2;  // Every time: no rescaling
    s.compare.scalePenaltyErrors = m_scaleMode->currentIndex() == 0 ? 0 : 1;
    s.compare.detectLocation = m_locationMode->currentIndex() == 0;  // Every time: graded where it was drawn
    s.compare.locationPenaltyErrors = 1;                               // Once
    s.compare.matchColor = m_matchColor->isChecked();
    s.compare.colorTolerance = m_colorTol->value();
    s.compare.requireByLayer = m_byLayer->isChecked();
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
