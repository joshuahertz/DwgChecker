#pragma once
// Class-wide summary tab (D89, D90). With several solution drawings: a line about all of
// them, then a button per drawing, then the chosen drawing's figures. For that drawing: how
// the class scored (a line of figures and a bar chart), the errors by type and by location,
// and the solution drawn in colors: green where every student got it right, yellow for the
// most commonly missed parts, red for parts most of the class missed. A type of error
// clicked colors the drawing by that type; a location clicked is zoomed to.

#include "Batch.h"
#include "ClassSummary.h"

#include <QWidget>

#include <memory>
#include <vector>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QStackedWidget;
class QTableWidget;

namespace dcgui {

class DrawingView;

// Bars of how many graded files scored 0–10 %, 10–20 %, … 90–100 %.
class ScoreChart : public QWidget {
public:
    explicit ScoreChart(QWidget* parent = nullptr);
    void setScores(const std::vector<double>& scores);
    QSize sizeHint() const override { return {420, 130}; }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    int m_bins[10] = {};
};

class ClassSummaryTab : public QWidget {
    Q_OBJECT
public:
    explicit ClassSummaryTab(QWidget* parent = nullptr);
    void setBatch(std::shared_ptr<const dc::BatchOutput> b);

private:
    void showDrawing(int ref);
    void shade();  // the solution, colored by how often each part was missed (or had m_kind)

    std::shared_ptr<const dc::BatchOutput> m_batch;
    dc::DrawingSummary m_sum;
    std::vector<QString> m_partText;  // describePart for each part of the drawing, made once per drawing
    QString m_kind;  // the type of error the drawing is colored by; empty: all errors
    QStackedWidget* m_stack;
    QLabel* m_overall;               // all the drawings (several only)
    QWidget* m_drawingRow;           // "Solution drawing:" and a button per drawing (several only)
    QHBoxLayout* m_drawingButtons;
    QButtonGroup* m_drawingGroup;
    QLabel* m_stats;                 // the chosen drawing
    ScoreChart* m_chart;
    QTableWidget* m_kinds;           // errors by type
    QTableWidget* m_parts;           // errors by location
    QLabel* m_kindsTitle;
    QLabel* m_partsTitle;
    QLabel* m_shadeNote;
    DrawingView* m_view;
};

}  // namespace dcgui
