#pragma once
// Creative mode (D106): the student designs their own part, so sizes and places are free. The
// solution drawing says which kinds of objects must be there and how many ("hidden lines",
// "radius dimensions"), as the instructor chose (CreativeChoices), and two drafting rules may be
// asked for: every circle has its centerlines, and corners meet.

#include "Comparator.h"

#include <vector>

namespace dc {

// The kind an object counts as: shape and linetype for geometry ("visible line", "hidden line",
// "centerline", "circle", "hidden circle"), the type of a dimension, hatching by pattern. These
// are the kinds the choices are made for, whatever the settings (D108). The settings may then
// grade some together: its group is the kind it is graded under, the shape alone with
// "Linetypes must match" off ("line"), and any hatch with "Hatch pattern must match" off. No
// key: not counted (text, unsupported objects).
struct CreativeKind {
    QString key;     // also its name: "hidden line", "radius dimension", "hatch (ANSI31)"
    QString plural;  // "hidden lines"
    int order = 0;   // where it is listed: geometry, then dimensions, then hatches
    QString group, groupPlural;  // as graded: "line", "lines"
};
CreativeKind creativeKind(const Primitive& p, const CompareSettings& s);
// "circle" -> "circles", "hatch (ANSI31)" -> "hatches (ANSI31)".
QString pluralKind(const QString& singular);
// "1 circle", "3 hidden lines".
QString countOf(int n, const QString& one, const QString& many);
inline QString countOf(int n, const CreativeKind& k) { return countOf(n, k.key, k.plural); }

// A drawing's kinds, in the order they are listed, each with its items and how many it has.
// Centerlines are counted by axis: the collinear pieces of one (a center mark's two halves, or
// one centerline through two holes) are one. A center mark's small cross is not counted.
struct CreativeItems {
    CreativeKind kind;
    std::vector<int> prims;
    int count = 0;
};
std::vector<CreativeItems> creativeItems(const Drawing& d, const CompareSettings& s);

// What is graded, with the choices: each group of checked kinds (a kind on its own, unless the
// settings grade kinds together), needing the sum of their counts.
struct CreativeNeed {
    QString key, plural;     // the group: "line", "lines"
    int order = 0;
    int need = 0;
    std::vector<int> prims;  // the solution's objects of its checked kinds
    bool merged = false;     // it joins kinds the choices tell apart ("Linetypes must match" off...)
};
std::vector<CreativeNeed> creativeNeeds(const std::vector<CreativeItems>& items, const CreativeChoices& ch);

// The rules are offered only where the solution itself has what they are about.
bool hasCenterlines(const Drawing& d);           // center-linetype lines
int countCorners(const Drawing& d, double tol);  // places where the ends of two or more outlines meet

// What a solution drawing asks in Creative mode, with the settings: its kinds, and whether it
// offers the rules. A batch works it out once per solution drawing (D108).
struct CreativeSolution {
    std::vector<CreativeItems> items;
    bool centerlines = false;  // it has centerlines
    bool corners = false;      // it has corners
};
CreativeSolution analyzeCreative(const Drawing& ref, const CompareSettings& s);

// Every circle, and every arc of 180 degrees or more (a fillet is less), drawn in a visible,
// hidden or other linetype, needs center-linetype lines through its center (within the larger
// of the tolerance and 1% of its radius) in two directions at least 45 degrees apart, each
// reaching past it on both sides; the pieces along one direction count together (AutoCAD's
// center mark leaves a gap at the center). A center-linetype circle or arc whose path runs
// through the center (a bolt circle) is the direction along it there, reaching past when it
// goes on beyond the circle's radius each way (D108).
struct CenterlineProblem {
    int prim = -1;
    std::vector<int> lines;  // the centerlines through its center there are
    QString text;
};
std::vector<CenterlineProblem> centerlineProblems(const Drawing& d, double linTol);

// Corners that don't meet: an end of an outline (a line, arc, elliptical arc or open spline in a
// visible, hidden or other linetype) that touches nothing, within 1% of the drawing's size of
// another outline (a gap), or of where it crosses one (a line that runs past it). Ends farther
// from everything are open ends, not judged. A hidden line continuing a visible one may stop
// short of it (the drafting convention). A center mark's small cross is not an edge. An end is
// not judged against what the object's other end is attached to: a short stub hanging off an
// edge is an open end (D108). A spline whose ends meet within the tolerance is closed. The bad
// ends near each other make one corner.
struct CornerProblem {
    Vec2 at;
    double worst = 0;        // the largest gap, or overshoot
    bool overshoot = false;  // every end there runs past
    std::vector<int> prims;  // the outlines that meet there
};
std::vector<CornerProblem> cornerProblems(const Drawing& d, double linTol);

// Distance from q to the object (lines, arcs, circles, ellipses and splines; others: none, 1e300).
double distanceToPrimitive(const Primitive& p, Vec2 q);

// The comparison in Creative mode: each kind the choices check needs at least its count (extras
// never count against the student), its items following the solution's layer, color and
// lineweight where all of the solution's items of that kind agree, and "set by layer" when asked
// for. The score is the items counted, up to each kind's count, out of those required, with
// each rule broken (a circle, a corner) one more. The solution's analysis comes from
// s.creativeSolution when a batch has set it.
CompareResult compareCreative(const Drawing& ref, const Drawing& stu, const CompareSettings& s);

}  // namespace dc
