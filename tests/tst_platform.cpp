// The read-only copies opened in AutoCAD, and their cleanup (D97): only the app's own copies
// are removed, never a file the user saved next to one.

#include "Platform.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace dc::platform;

namespace {

QString write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(bytes);
    return path;
}

QString shaFolder(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(12));
}

int folderCount(const QString& dir) { return static_cast<int>(QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size()); }

}  // namespace

class TestPlatform : public QObject {
    Q_OBJECT
private slots:
    void copyIsReadOnlyAndReused();
    void cleanupKeepsTheUsersFiles();
    void cleanupRemovesEmptyFolders();
    void earlierFoldersAreCleared();
    void folderIsOutsideAppDataAndDocuments();
};

void TestPlatform::copyIsReadOnlyAndReused() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.filePath(QStringLiteral("DWG Checker/Temp"));
    const QByteArray bytes("AC1032 a student's drawing");
    const QString src = write(tmp.filePath(QStringLiteral("subs/smithjane_1_2_lab3.dwg")), bytes);
    QString err;
    const QString copy = makeEvidenceCopy(src, &err, root);
    QVERIFY2(!copy.isEmpty(), qPrintable(err));
    QCOMPARE(QFileInfo(copy).absolutePath(), QDir(root).absoluteFilePath(shaFolder(bytes)));
    QCOMPARE(QFileInfo(copy).fileName(), QStringLiteral("smithjane_1_2_lab3.dwg"));
    QFile c(copy);
    QVERIFY(c.open(QIODevice::ReadOnly));
    QCOMPARE(c.readAll(), bytes);
    c.close();
    QVERIFY(!QFileInfo(copy).isWritable());
    QCOMPARE(makeEvidenceCopy(src, &err, root), copy);  // the same file again: the same copy
    QCOMPARE(folderCount(root), 1);
    removeCopies(root);  // read-only files would outlive the temporary folder on Windows
}

void TestPlatform::cleanupKeepsTheUsersFiles() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.filePath(QStringLiteral("DWG Checker/Temp"));
    QString err;
    const QByteArray a("AC1032 first"), b("AC1032 second");
    const QString copyA = makeEvidenceCopy(write(tmp.filePath(QStringLiteral("a.dwg")), a), &err, root);
    const QString copyB = makeEvidenceCopy(write(tmp.filePath(QStringLiteral("b.dwg")), b), &err, root);
    QVERIFY(!copyA.isEmpty() && !copyB.isEmpty());
    // A marked-up drawing saved next to copy A (AutoCAD's Save As starts there), and a folder
    // that is not one of the app's.
    const QString saved = write(QFileInfo(copyA).absolutePath() + QStringLiteral("/graded.dwg"), "AC1032 marked up");
    const QString notes = write(root + QStringLiteral("/notes/todo.txt"), "mine");

    removeCopies(root);
    QVERIFY(!QFileInfo::exists(copyA));
    QVERIFY(QFileInfo::exists(saved));                         // the user's file stays
    QVERIFY(QFileInfo::exists(QFileInfo(copyA).absolutePath()));  // and so does its folder
    QVERIFY(!QFileInfo::exists(copyB));
    QVERIFY(!QFileInfo::exists(QFileInfo(copyB).absolutePath()));  // emptied: removed
    QVERIFY(QFileInfo::exists(notes));
    QVERIFY(QFileInfo::exists(root));  // not empty
}

void TestPlatform::cleanupRemovesEmptyFolders() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.filePath(QStringLiteral("DWG Checker/Temp"));
    QString err;
    QVERIFY(!makeEvidenceCopy(write(tmp.filePath(QStringLiteral("a.dwg")), "AC1032 a"), &err, root).isEmpty());
    removeCopies(root);
    QVERIFY(!QFileInfo::exists(root));                                    // nothing left in it
    QVERIFY(!QFileInfo::exists(tmp.filePath(QStringLiteral("DWG Checker"))));  // nor its parent
    QVERIFY(QFileInfo::exists(tmp.path()));
    removeCopies(root);  // nothing there: nothing happens
}

void TestPlatform::earlierFoldersAreCleared() {
    // 0.3.0 copied into Documents/DWG Checker/Opened in AutoCAD, with the same folder names.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString documents = tmp.filePath(QStringLiteral("Documents"));
    const QString old = documents + QStringLiteral("/DWG Checker/Opened in AutoCAD");
    QString err;
    QVERIFY(!makeEvidenceCopy(write(tmp.filePath(QStringLiteral("a.dwg")), "AC1032 a"), &err, old).isEmpty());
    write(documents + QStringLiteral("/report.docx"), "the user's");
    removeCopies(old);
    QVERIFY(!QFileInfo::exists(documents + QStringLiteral("/DWG Checker")));
    QVERIFY(QFileInfo::exists(documents + QStringLiteral("/report.docx")));
    // The real earlier folders are those two.
    const QStringList earlier = earlierEvidenceFolders();
    QCOMPARE(earlier.size(), 2);
    QVERIFY(earlier[0].endsWith(QStringLiteral("/DWG Checker/Opened in AutoCAD")));
    QVERIFY(earlier[1].endsWith(QStringLiteral("/DwgChecker-evidence")));
}

void TestPlatform::folderIsOutsideAppDataAndDocuments() {
    const QString f = QDir::cleanPath(evidenceFolder());
    QVERIFY2(f.endsWith(QStringLiteral("/DWG Checker/Temp")), qPrintable(f));
    QCOMPARE(QFileInfo(QFileInfo(f).absolutePath()).absolutePath(), QDir::cleanPath(QDir::homePath()));
    QVERIFY(!f.contains(QStringLiteral("AppData"), Qt::CaseInsensitive));
    QVERIFY(!f.contains(QStringLiteral("Documents"), Qt::CaseInsensitive));
}

QTEST_GUILESS_MAIN(TestPlatform)
#include "tst_platform.moc"
