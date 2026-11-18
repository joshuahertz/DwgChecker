#pragma once
// Creative mode's choices for one solution drawing (D106): the drawing on the left; on the right
// each kind of object in it, checked or not, with the solution's count and the count a student's
// drawing needs, then the drafting rules the solution allows. Selecting a kind shows its items
// in the drawing.

#include "Creative.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QTableWidget;

namespace dcgui {

class DrawingView;

class CreativeDialog : public QDialog {
    Q_OBJECT
public:
    // `s`: the settings the kinds follow (linetypes, hatch patterns, tolerance).
    CreativeDialog(dc::DrawingPtr ref, const QString& refPath, const dc::CompareSettings& s, const dc::CreativeChoices& initial,
                   QWidget* parent = nullptr);
    // The choices made. Kinds this drawing doesn't have now (with other settings, say) keep
    // what they had.
    dc::CreativeChoices choices() const;

private:
    void fill(const dc::CreativeChoices& c);
    void highlight();
    void updateTotal();
    int required(int row) const;

    dc::DrawingPtr m_ref;
    std::vector<dc::CreativeItems> m_items;
    dc::CreativeChoices m_initial;
    bool m_offerCenterlines = false;
    int m_corners = 0;  // the solution's corners: the rule is offered when it has some
    bool m_dark = true;
    // The settings that grade kinds together: "Linetypes must match" off, "Hatch pattern must match" off.
    bool m_mergeLinetypes = false, m_mergePatterns = false;
    DrawingView* m_view = nullptr;
    QTableWidget* m_table = nullptr;
    QCheckBox* m_centerlines = nullptr;
    QCheckBox* m_cornersMeet = nullptr;
    QLabel* m_total = nullptr;
};

}  // namespace dcgui
