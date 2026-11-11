#pragma once
// A window that shows one solution drawing in its own colors and linetypes, so that several
// drawings can be told apart (opened from the drawing names in step 2). It draws with the
// detail view's DrawingView: wheel zooms, dragging pans, hovering shows an item's layer.
// Not modal: several can be open while the location modes are chosen.

#include "Model.h"

#include <QDialog>

class QCheckBox;

namespace dcgui {

class DrawingView;

class DrawingPreviewDialog : public QDialog {
    Q_OBJECT
public:
    // `title`: "Drawing 2: Q10Problem2.dwg"; `summary`: what was read, as on the reference card.
    DrawingPreviewDialog(dc::DrawingPtr drawing, const QString& title, const QString& summary,
                         QWidget* parent = nullptr);

private:
    void build();  // (re)draws the drawing; keeps the zoom

    dc::DrawingPtr m_drawing;
    DrawingView* m_view = nullptr;
    QCheckBox* m_dark = nullptr;
};

}  // namespace dcgui
