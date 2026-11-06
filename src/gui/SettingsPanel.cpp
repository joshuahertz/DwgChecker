#include "SettingsPanel.h"

#include "AutoCadController.h"
#include "Geometry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
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
    // Grading: how the drawing is compared.
    auto* gGrading = new QGroupBox(tr("Grading"), this);
    auto* fGrading = new QFormLayout(gGrading);
    m_mode = combo({tr("Absolute"), tr("Relative"), tr("Orthographic")});
    fGrading->addRow(tr("Mode"), m_mode);
    m_modeHelp = new QLabel(this);
    m_modeHelp->setWordWrap(true);
    m_modeHelp->setForegroundRole(QPalette::PlaceholderText);
    fGrading->addRow(m_modeHelp);
    m_rotation = check(tr("Allow rotation"));
    m_mirror = check(tr("Allow a mirrored (flipped) drawing"));
    m_rotation->setToolTip(tr("Relative mode only"));
    m_mirror->setToolTip(tr("Relative mode only"));
    fGrading->addRow(m_rotation);
    fGrading->addRow(m_mirror);
    m_linTol = sciEdit(1e-4);
    m_linTol->setToolTip(tr("How far apart two points may be and still count as the same point"));
    fGrading->addRow(tr("Tolerance (drawing units)"), m_linTol);
    m_extras = combo({tr("Count against the score"), tr("List only")});
    m_extras->setToolTip(tr("Items in the drawing that are not in the reference"));
    fGrading->addRow(tr("Extra items"), m_extras);

    // What to check.
    auto* gChecks = new QGroupBox(tr("What to check"), this);
    auto* fChecks = new QFormLayout(gChecks);
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
    m_dimPrec->setEnabled(false);
    connect(m_dims, &QCheckBox::toggled, m_dimPrec, &QWidget::setEnabled);
    m_hatches = check(tr("Check hatches (each hatched region is hatched)"));
    m_hatchPattern = check(tr("…and with the same hatch pattern"));
    fChecks->addRow(m_hatches);
    fChecks->addRow(indented(m_hatchPattern, this));
    m_hatchPattern->setEnabled(false);
    connect(m_hatches, &QCheckBox::toggled, m_hatchPattern, &QWidget::setEnabled);
    m_detectScale = check(tr("Detect drawings at the wrong scale (e.g. millimeters instead of inches)"));
    m_detectScale->setToolTip(tr("When less than half of the reference matches, try uniform scale factors. "
                                 "A drawing that matches once rescaled gets one scale error instead of every item wrong."));
    m_scalePenalty = new QSpinBox(this);
    m_scalePenalty->setRange(0, 100);
    m_scalePenalty->setSuffix(tr(" error(s)"));
    connect(m_scalePenalty, &QSpinBox::valueChanged, this, &SettingsPanel::changed);
    auto* penRow = new QWidget(this);
    auto* penLay = new QHBoxLayout(penRow);
    penLay->setContentsMargins(20, 0, 0, 0);
    penLay->addWidget(new QLabel(tr("Wrong scale counts as"), penRow));
    penLay->addWidget(m_scalePenalty);
    penLay->addStretch();
    fChecks->addRow(m_detectScale);
    fChecks->addRow(penRow);
    connect(m_detectScale, &QCheckBox::toggled, m_scalePenalty, &QWidget::setEnabled);

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
    m_strictCl = check(tr("Centerlines must have the same length"));
    fMatch->addRow(m_strictCl);

    // Reading drawings.
    auto* gRead = new QGroupBox(tr("Reading drawings"), m_advanced);
    auto* fRead = new QFormLayout(gRead);
    m_paper = check(tr("Use paper space instead of model space"));
    m_skipDefpoints = check(tr("Skip the Defpoints layer"));
    m_skipHidden = check(tr("Skip entities on frozen/off layers"));
    for (QCheckBox* c : {m_paper, m_skipDefpoints, m_skipHidden}) fRead->addRow(c);
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
    auto* fOrtho = new QFormLayout(gOrtho);
    m_topK = new QSpinBox(this);
    m_topK->setRange(1, 20);
    m_topK->setToolTip(tr("Candidate placements tried for each view"));
    connect(m_topK, &QSpinBox::valueChanged, this, &SettingsPanel::changed);
    fOrtho->addRow(tr("Placements tried per view"), m_topK);
    m_gapPct = new QDoubleSpinBox(this);
    m_gapPct->setRange(0.1, 20);
    m_gapPct->setSuffix(QStringLiteral(" %"));
    m_gapPct->setDecimals(1);
    m_gapPct->setToolTip(tr("Items closer than this (as a share of the drawing size) belong to the same view"));
    connect(m_gapPct, &QDoubleSpinBox::valueChanged, this, &SettingsPanel::changed);
    fOrtho->addRow(tr("Gap between views"), m_gapPct);
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
    connect(m_timeout, &QSpinBox::valueChanged, this, &SettingsPanel::changed);
    fTools->addRow(tr("Time limit per file"), m_timeout);
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
    });
    connect(m_mode, &QComboBox::currentIndexChanged, this, &SettingsPanel::updateModeHelp);

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
    m_rotation->setEnabled(mode == Mode::Relative);
    m_mirror->setEnabled(mode == Mode::Relative);
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
    m_detectScale->setChecked(s.compare.detectScale);
    m_scalePenalty->setValue(s.compare.scalePenaltyErrors);
    m_scalePenalty->setEnabled(s.compare.detectScale);
    m_nearMiss->setText(QString::number(s.compare.nearMissRadius, 'g', 6));
    m_ignoreRe->setText(s.canon.layerIgnoreRegex);
    m_paper->setChecked(s.canon.usePaperSpace);
    m_skipDefpoints->setChecked(s.canon.skipDefpoints);
    m_skipHidden->setChecked(s.canon.skipHiddenLayers);
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
    updateModeHelp();
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
    s.compare.detectScale = m_detectScale->isChecked();
    s.compare.scalePenaltyErrors = m_scalePenalty->value();
    s.compare.nearMissRadius = sciValue(m_nearMiss, 0);
    s.canon.layerIgnoreRegex = m_ignoreRe->text().trimmed();
    s.canon.usePaperSpace = m_paper->isChecked();
    s.canon.skipDefpoints = m_skipDefpoints->isChecked();
    s.canon.skipHiddenLayers = m_skipHidden->isChecked();
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
    return s;
}

}  // namespace dcgui
