#include "PreviewDialog.h"

#include "DrawingView.h"

#include <QCheckBox>
#include <QGraphicsItem>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

using namespace dc;

namespace dcgui {

namespace {
const char* const kDarkKey = "preview/darkBackground";
}

DrawingPreviewDialog::DrawingPreviewDialog(DrawingPtr drawing, const QString& title, const QString& summary,
                                           QWidget* parent)
    : QDialog(parent), m_drawing(std::move(drawing)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);
    resize(900, 650);
    auto* lay = new QVBoxLayout(this);
    auto* head = new QLabel(QStringLiteral("<b>%1</b><br>%2").arg(title.toHtmlEscaped(), summary.toHtmlEscaped()), this);
    head->setWordWrap(true);
    lay->addWidget(head);
    auto* hint = new QLabel(tr("Each item in its own color and linetype, as in AutoCAD; hover over one for its layer. "
                               "Wheel: zoom, drag: pan."),
                            this);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    lay->addWidget(hint);

    m_view = new DrawingView(this);
    lay->addWidget(m_view, 1);

    auto* buttons = new QHBoxLayout;
    auto* all = new QPushButton(tr("Zoom to all"), this);
    m_dark = new QCheckBox(tr("Dark background, as in AutoCAD"), this);
    m_dark->setToolTip(tr("Black-and-white items (ACI 7) show white on the dark background and black on the white one"));
    m_dark->setChecked(QSettings().value(kDarkKey, true).toBool());
    auto* close = new QPushButton(tr("Close"), this);
    buttons->addWidget(all);
    buttons->addWidget(m_dark);
    buttons->addStretch();
    buttons->addWidget(close);
    lay->addLayout(buttons);
    connect(all, &QPushButton::clicked, m_view, &DrawingView::zoomToAll);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_dark, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(kDarkKey, on);
        build();
    });

    build();
    m_view->zoomToAll();  // and stays fitted until the user zooms or pans
    m_view->setFocus(Qt::OtherFocusReason);
}

void DrawingPreviewDialog::build() {
    const bool dark = m_dark->isChecked();
    m_view->clearDrawing();  // the view's zoom and pan stay
    m_view->setBackgroundBrush(dark ? kModelSpaceDark : kModelSpaceLight);
    for (const Primitive& p : m_drawing->prims) {
        // Fills under lines, text and dimension values on top, as AutoCAD usually draws them.
        const int z = p.kind == PrimKind::Hatch ? 0 : p.kind == PrimKind::Text || p.kind == PrimKind::Dimension ? 2 : 1;
        if (QGraphicsItem* it = m_view->addPrimitive(p, drawingColor(p, dark), 1.5)) it->setZValue(z);
    }
}

}  // namespace dcgui
