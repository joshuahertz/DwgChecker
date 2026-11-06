// Orthographic mode (M4b) on the ortho_* corpus files.

#include "Comparator.h"
#include "Ortho.h"
#include "TestUtil.h"

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

QTEST_GUILESS_MAIN(TestOrtho)
#include "tst_ortho.moc"
