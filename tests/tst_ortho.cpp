// Orthographic mode (M4b) on the ortho_* corpus files.

#include "Comparator.h"
#include "Ortho.h"
#include "TestUtil.h"

#include <algorithm>

#include <QTemporaryDir>
#include <QtTest>

using namespace dc;
using namespace dct;

namespace {

CompareSettings ortho() {
    CompareSettings s;
    s.mode = Mode::Orthographic;
    return s;
}
bool anyError(const CompareResult& r, const QString& needle) {
    for (const auto& e : r.errors)
        if (e.text.contains(needle)) return true;
    return false;
}
QString dumpErrors(const CompareResult& r) { return r.errorStrings().join(" | "); }

}  // namespace

class TestOrtho : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void segmentation();
    void selfPasses();
    void spacingIsFree();
    void misalignedTopView();
    void firstAngleArrangement();
    void hiddenDrawnAsVisible();
    void projectionLinesLeftIn();
    void similarViewsJointResolution();
    void sidecarRoundTrip();
    void sidecarRejectsOtherReference();
    void severityOff();
    void unassignedPrimitivesJoinNearestView();
    void rebindAfterSettingsChange();
    void scaledDrawing();
    void ignoredView();
    void ignoredFrameKeepsRealErrors();
    void ignoredCopyOfAGradedView();

private:
    std::shared_ptr<Drawing> ref;
    ViewSet views;
};

void TestOrtho::initTestCase() {
    ref = loadFixture("ortho_ref");
    QVERIFY(ref);
    views = segmentViews(*ref, 0.02);
}

void TestOrtho::segmentation() {
    QCOMPARE(views.views.size(), size_t(3));
    QVERIFY(views.indexOfName(ViewName::Front) >= 0);
    QVERIFY(views.indexOfName(ViewName::Top) >= 0);
    QVERIFY(views.indexOfName(ViewName::Right) >= 0);
    const ViewDef& front = views.views[views.indexOfName(ViewName::Front)];
    const ViewDef& top = views.views[views.indexOfName(ViewName::Top)];
    QVERIFY(front.bbox.contains({1.5, 0.75}));
    QVERIFY(top.bbox.contains({1.5, 3.5}));
    // The hole circle and the centerlines belong to the top view; the front centerline to the front.
    for (int i = 0; i < static_cast<int>(ref->prims.size()); ++i) {
        const Primitive& p = ref->prims[i];
        if (p.kind == PrimKind::Circle) QCOMPARE(views.primView[i], views.indexOfName(ViewName::Top));
        if (p.kind == PrimKind::Line && p.ltClass == LtClass::Center && dist(p.midpoint(), {1, 0.75}) < 1e-9)
            QCOMPARE(views.primView[i], views.indexOfName(ViewName::Front));
    }
}

void TestOrtho::selfPasses() {
    const CompareResult r = compareDrawings(*ref, *ref, ortho(), &views);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.views.size(), size_t(3));
    for (const auto& v : r.views) QVERIFY(v.pass);
}

void TestOrtho::spacingIsFree() {
    auto d = loadFixture("ortho_spacing");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, ortho(), &views);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    const ViewResult& top = r.views[views.indexOfName(ViewName::Top)];
    QVERIFY(std::fabs(top.T.ty + 0.75) < 1e-9);
}

void TestOrtho::misalignedTopView() {
    auto d = loadFixture("ortho_misaligned");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, ortho(), &views);
    QCOMPARE(r.correctness, 100.0);  // geometry is fine
    QVERIFY(!r.pass);                 // but the projection alignment check fails
    QVERIFY2(anyError(r, QStringLiteral("Top view is not aligned with the Front view: shifted 0.250 right")),
             qPrintable(dumpErrors(r)));
    QVERIFY2(r.errorSummary().contains(QStringLiteral("misaligned view")), qPrintable(r.errorSummary()));
}

void TestOrtho::firstAngleArrangement() {
    auto d = loadFixture("ortho_firstangle");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, ortho(), &views);
    QCOMPARE(r.correctness, 100.0);
    QVERIFY(!r.pass);
    QVERIFY2(anyError(r, QStringLiteral("Top view is below the Front view")), qPrintable(dumpErrors(r)));
    QVERIFY2(anyError(r, QStringLiteral("Right view is left of the Front view")), qPrintable(dumpErrors(r)));
}

void TestOrtho::hiddenDrawnAsVisible() {
    auto d = loadFixture("ortho_hidden_visible");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, ortho(), &views);
    QVERIFY(!r.pass);
    QVERIFY2(anyError(r, QStringLiteral("Front view: hidden edge 1.500 long at (0.625, 0.75) drawn as visible line")),
             qPrintable(dumpErrors(r)));
    for (const auto& e : r.errors)
        if (e.cat == ErrCat::Linetype) QCOMPARE(e.view, views.indexOfName(ViewName::Front));
}

void TestOrtho::projectionLinesLeftIn() {
    auto d = loadFixture("ortho_projlines");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, ortho(), &views);
    QVERIFY(!r.pass);
    QVERIFY2(anyError(r, QStringLiteral("is part of a longer line")), qPrintable(dumpErrors(r)));
    // All views are still located (the identity placement wins).
    for (const auto& v : r.views) QVERIFY(v.found);
}

void TestOrtho::similarViewsJointResolution() {
    auto sref = loadFixture("ortho_similar");
    auto stu = loadFixture("ortho_similar_spaced");
    QVERIFY(sref && stu);
    const ViewSet vs = segmentViews(*sref, 0.02);
    QCOMPARE(vs.views.size(), size_t(3));
    const CompareResult r = compareDrawings(*sref, *stu, ortho(), &vs);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    const ViewResult& front = r.views[vs.indexOfName(ViewName::Front)];
    const ViewResult& top = r.views[vs.indexOfName(ViewName::Top)];
    QVERIFY(std::fabs(front.T.ty) < 1e-9);         // front stays at the origin
    QVERIFY(std::fabs(top.T.ty + 0.6) < 1e-9);     // top moved up by 0.6 in the student file
}

void TestOrtho::sidecarRoundTrip() {
    QTemporaryDir dir;
    const QString path = dir.filePath("ref.dwg.views.json");
    ViewSet edited = views;
    // Instructor renames a view and moves a primitive; the sidecar keeps both.
    const int top = edited.indexOfName(ViewName::Top);
    edited.views[top].name = ViewName::Bottom;
    QString err;
    QVERIFY2(saveSidecar(path, edited, *ref, &err), qPrintable(err));
    ViewSet loaded;
    QVERIFY2(loadSidecar(path, *ref, &loaded, &err), qPrintable(err));
    QCOMPARE(loaded.views.size(), edited.views.size());
    QCOMPARE(loaded.views[top].name, ViewName::Bottom);
    QCOMPARE(loaded.primView, edited.primView);
    QCOMPARE(sidecarPath("C:/x/ref.dwg"), QStringLiteral("C:/x/ref.dwg.views.json"));
}

void TestOrtho::sidecarRejectsOtherReference() {
    QTemporaryDir dir;
    const QString path = dir.filePath("v.json");
    QString err;
    QVERIFY(saveSidecar(path, views, *ref, &err));
    auto other = loadFixture("ortho_spacing");
    QVERIFY(other);
    ViewSet loaded;
    QVERIFY(!loadSidecar(path, *other, &loaded, &err));
    QVERIFY(err.contains("SHA-256"));
}

void TestOrtho::severityOff() {
    auto d = loadFixture("ortho_misaligned");
    QVERIFY(d);
    CompareSettings s = ortho();
    s.alignmentSeverity = Severity::Off;
    QVERIFY(compareDrawings(*ref, *d, s, &views).pass);
    s.alignmentSeverity = Severity::Warn;
    const CompareResult r = compareDrawings(*ref, *d, s, &views);
    QVERIFY(r.pass);
    QVERIFY(anyError(r, QStringLiteral("warning: Top view is not aligned")));
    QVERIFY2(r.errorSummary().startsWith(QStringLiteral("no errors; ")), qPrintable(r.errorSummary()));  // a warning only
}

void TestOrtho::unassignedPrimitivesJoinNearestView() {
    ViewSet vs = views;
    // Drop the assignment of the circle and every centerline, and make the list one short.
    for (int i = 0; i < static_cast<int>(ref->prims.size()); ++i)
        if (ref->prims[i].kind == PrimKind::Circle || ref->prims[i].ltClass == LtClass::Center) vs.primView[i] = -1;
    vs.primView.resize(ref->prims.size() - 1);
    vs.rebuild(*ref);
    QCOMPARE(vs.primView, views.primView);  // everything is back in its view
    const CompareResult r = compareDrawings(*ref, *ref, ortho(), &vs);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestOrtho::rebindAfterSettingsChange() {
    CanonSettings cs;
    cs.layerIgnoreRegex = "^CENTER$";  // a different primitive list than the one the views were made for
    auto noCenter = loadFixture("ortho_ref", cs);
    QVERIFY(noCenter);
    QVERIFY(noCenter->prims.size() < ref->prims.size());
    const ViewSet vs = views.rebindTo(*noCenter);
    QCOMPARE(vs.primView.size(), noCenter->prims.size());
    QCOMPARE(vs.views.size(), views.views.size());
    for (int i = 0; i < static_cast<int>(noCenter->prims.size()); ++i) {
        const int orig = views.primIds.indexOf(noCenter->prims[i].sourceId);
        QVERIFY(orig >= 0);
        QCOMPARE(vs.primView[i], views.primView[orig]);
    }
}

void TestOrtho::scaledDrawing() {
    // All three views drawn 25.4 times too large (same INSUNITS): a pure scale error.
    const auto d = transformedCopy(*ref, Transform2D::scaling(25.4));
    const CompareResult r = compareDrawings(*ref, *d, ortho(), &views);
    QVERIFY(r.scaled);
    QVERIFY(std::fabs(r.scaleFactor - 25.4) < 1e-9);
    QCOMPARE(r.matched, r.refCount);
    QVERIFY(!r.pass);
    for (const auto& v : r.views) QVERIFY(v.found && v.pass);
    for (const auto& c : r.checks) QVERIFY(!c.applicable || c.passed);
}

void TestOrtho::ignoredView() {
    // The Top view named Ignored: never graded, so a misaligned top view passes, and so does
    // a drawing without it; the student's copy of it is not extra.
    ViewSet vs = views;
    const int top = vs.indexOfName(ViewName::Top);
    QVERIFY(top >= 0);
    vs.views[top].name = ViewName::Ignored;
    const std::vector<int> graded = comparableIndices(*ref, ortho());
    int topCount = 0;  // the top view's graded items
    for (int i : vs.views[top].prims) topCount += std::count(graded.begin(), graded.end(), i) > 0;
    QVERIFY(topCount > 0);

    const CompareResult self = compareDrawings(*ref, *ref, ortho(), &vs);
    QVERIFY2(self.pass, qPrintable(dumpErrors(self)));
    QVERIFY(self.views[top].ignored);
    QCOMPARE(self.refCount, static_cast<int>(graded.size()) - topCount);
    QCOMPARE(self.penalizedExtras, 0);

    auto misaligned = loadFixture("ortho_misaligned");
    QVERIFY(misaligned);
    const CompareResult m = compareDrawings(*ref, *misaligned, ortho(), &vs);
    QVERIFY2(m.pass, qPrintable(dumpErrors(m)));
    QVERIFY(!anyError(m, QStringLiteral("Top view")));

    Drawing noTop = *ref;  // the student left the top view out
    std::vector<Primitive> kept;
    for (int i = 0; i < static_cast<int>(ref->prims.size()); ++i)
        if (vs.primView[i] != top) kept.push_back(ref->prims[i]);
    noTop.prims = kept;
    const CompareResult n = compareDrawings(*ref, noTop, ortho(), &vs);
    QVERIFY2(n.pass, qPrintable(dumpErrors(n)));
    QVERIFY(!anyError(n, QStringLiteral("not found")));
}

namespace {
Primitive plainLine(Vec2 a, Vec2 b, const QString& id) {
    Primitive p;
    p.kind = PrimKind::Line;
    p.p0 = a;
    p.p1 = b;
    p.sourceType = QStringLiteral("LINE");
    p.sourceId = id;
    return p;
}
}  // namespace

void TestOrtho::ignoredFrameKeepsRealErrors() {
    // A sheet border well outside the drawing, named Ignored: its box encloses every view. The
    // margin beats the near-miss distance (10% of the framed drawing's diagonal).
    Drawing framed = *ref;
    const BBox b = ref->bbox();
    const double m = std::max(3.0, 0.5 * b.diagonal());
    const Vec2 c0{b.x0 - m, b.y0 - m}, c1{b.x1 + m, b.y0 - m}, c2{b.x1 + m, b.y1 + m}, c3{b.x0 - m, b.y1 + m};
    const int first = static_cast<int>(framed.prims.size());
    framed.prims.push_back(plainLine(c0, c1, QStringLiteral("F1")));
    framed.prims.push_back(plainLine(c1, c2, QStringLiteral("F2")));
    framed.prims.push_back(plainLine(c2, c3, QStringLiteral("F3")));
    framed.prims.push_back(plainLine(c3, c0, QStringLiteral("F4")));
    ViewSet vs = views;
    ViewDef frame;
    frame.id = static_cast<int>(vs.views.size());
    frame.name = ViewName::Ignored;
    vs.views.push_back(frame);
    const int fv = static_cast<int>(vs.views.size()) - 1;
    vs.primView.resize(framed.prims.size(), -1);
    for (int i = first; i < static_cast<int>(framed.prims.size()); ++i) vs.primView[i] = fv;
    vs.rebuild(framed);

    const CompareResult self = compareDrawings(framed, framed, ortho(), &vs);
    QVERIFY2(self.pass, qPrintable(dumpErrors(self)));

    // An extra line just below the Front view, inside the border: a real extra, not ignored.
    const BBox front = vs.views[vs.indexOfName(ViewName::Front)].bbox;
    Drawing stray = framed;
    stray.prims.push_back(plainLine({front.x0, front.y0 - 0.05}, {front.x0 + 0.4, front.y0 - 0.05}, QStringLiteral("X1")));
    const CompareResult near = compareDrawings(framed, stray, ortho(), &vs);
    QCOMPARE(near.penalizedExtras, 1);
    QVERIFY2(anyError(near, QStringLiteral("extra line")), qPrintable(dumpErrors(near)));

    // Something in the border's corner, far from every view (title block notes): ignored.
    Drawing notes = framed;
    notes.prims.push_back(plainLine({c1.x - 0.6, c1.y + 0.2}, {c1.x - 0.2, c1.y + 0.2}, QStringLiteral("X2")));
    const CompareResult far = compareDrawings(framed, notes, ortho(), &vs);
    QVERIFY2(far.pass, qPrintable(dumpErrors(far)));

    // A reference item without a view goes to the nearest graded view, not into the border.
    Drawing more = framed;
    more.prims.push_back(plainLine({front.x0, front.y0 - 0.05}, {front.x0 + 0.4, front.y0 - 0.05}, QStringLiteral("R1")));
    ViewSet vs2 = vs;
    vs2.primView.resize(more.prims.size(), -1);
    vs2.rebuild(more);
    QVERIFY(vs2.primView.back() != fv);
}

void TestOrtho::ignoredCopyOfAGradedView() {
    // A solution with the part twice: the same geometry again with its dimensions, named
    // Ignored and listed first, and a clean copy, the graded Front view. The student draws
    // the part once. Both views claimed it equally, the first in the list won, and the
    // drawing scored 0 against its own solution; the graded view must take it.
    auto shape = [](Vec2 o, const QString& tag) {  // an L: it maps onto itself only unmoved
        const std::vector<Vec2> pts = {{0, 0}, {4, 0}, {4, 1}, {1.5, 1}, {1.5, 3}, {0, 3}};
        std::vector<Primitive> out;
        for (size_t k = 0; k < pts.size(); ++k) {
            const Vec2 a = pts[k], b = pts[(k + 1) % pts.size()];
            out.push_back(plainLine({o.x + a.x, o.y + a.y}, {o.x + b.x, o.y + b.y}, tag + QString::number(k)));
        }
        return out;
    };
    const int n = 6;
    Drawing two;
    for (const Primitive& p : shape({0, -10}, QStringLiteral("I"))) two.prims.push_back(p);  // the dimensioned copy
    for (const Primitive& p : shape({0, 0}, QStringLiteral("F"))) two.prims.push_back(p);    // the graded one
    ViewSet vs;
    ViewDef ignored;
    ignored.id = 0;
    ignored.name = ViewName::Ignored;
    ViewDef front;
    front.id = 1;
    front.name = ViewName::Front;
    vs.views = {ignored, front};
    vs.primView.assign(two.prims.size(), 0);
    for (int i = n; i < 2 * n; ++i) vs.primView[i] = 1;
    vs.rebuild(two);
    QCOMPARE(vs.views[1].prims.size(), size_t(n));

    Drawing once;  // the student's single copy, somewhere else on the sheet
    for (const Primitive& p : shape({20, 5}, QStringLiteral("S"))) once.prims.push_back(p);
    const CompareResult r = compareDrawings(two, once, ortho(), &vs);
    QVERIFY2(r.views[1].found, qPrintable(dumpErrors(r)));
    QCOMPARE(r.refCount, n);
    QCOMPARE(r.matched, n);
    QCOMPARE(r.penalizedExtras, 0);
    QCOMPARE(r.correctness, 100.0);

    Drawing both = once;  // with the dimensioned copy too: one view each, nothing extra
    for (const Primitive& p : shape({20, -5}, QStringLiteral("T"))) both.prims.push_back(p);
    const CompareResult b = compareDrawings(two, both, ortho(), &vs);
    QVERIFY2(b.views[0].found && b.views[1].found, qPrintable(dumpErrors(b)));
    QCOMPARE(b.matched, n);
    QCOMPARE(b.penalizedExtras, 0);
}

QTEST_GUILESS_MAIN(TestOrtho)
#include "tst_ortho.moc"
