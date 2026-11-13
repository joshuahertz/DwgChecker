#pragma once
// Small helpers shared by the results tabs.

#include <QColor>
#include <QTableWidget>

namespace dcgui {

// A table row in a light tint, with dark text: a dark theme's white text cannot be read on the
// light tints (D84, D90).
inline void tintRow(QTableWidget* t, int row, const QColor& background) {
    for (int col = 0; col < t->columnCount(); ++col)
        if (QTableWidgetItem* it = t->item(row, col)) {
            it->setBackground(background);
            it->setForeground(QColor(20, 20, 20));
        }
}

}  // namespace dcgui
