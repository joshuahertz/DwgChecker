#include "Csv.h"

#include <QDateTime>
#include <QFileInfo>

namespace dc {

QString CopyFlag::levelText() const {
    switch (level) {
    case Strength::Strong: return QStringLiteral("strong");
    case Strength::Medium:
    case Strength::Weak: return QStringLiteral("weak");
    case Strength::Info: return QStringLiteral("none");
    }
    return {};
}

CopyFlag copyFlagFor(const BatchOutput& b, int si) {
    CopyFlag f;
    QStringList tips;
    const PairResult* best = nullptr;
    for (int k = 0; k < static_cast<int>(b.dups.pairs.size()); ++k) {
        const PairResult& p = b.dups.pairs[k];
        if (p.a != si && p.b != si) continue;
        const int other = p.a == si ? p.b : p.a;
        const QString partner = b.dupEntries[other].display;
        for (const auto& s : p.evidence) tips << QStringLiteral("[%1] %2 with %3: %4").arg(strengthName(s.strength), s.label, partner, s.detail);
        if (!p.flagged()) continue;
        if (!best || p.level > best->level || (p.level == best->level && p.score > best->score)) {
            best = &p;
            f.pairIndex = k;
        }
    }
    // Evidence from the student's own files (several account names, D77): it flags the file when
    // no pair has stronger evidence.
    const Signal* own = nullptr;
    double ownScore = 0;
    if (si >= 0 && si < static_cast<int>(b.dups.fileSignals.size()))
        for (const Signal& s : b.dups.fileSignals[si]) {
            tips << QStringLiteral("[%1] %2").arg(strengthName(s.strength), s.detail);
            if (!own || s.strength > own->strength) own = &s;
            ownScore += s.weight;
        }
    if (si >= 0 && si < static_cast<int>(b.dups.fileNotes.size()))
        for (const QString& n : b.dups.fileNotes[si]) tips << QStringLiteral("[note] ") + n;
    f.tooltip = tips.join('\n');
    if (own && own->strength >= Strength::Weak && (!best || own->strength > best->level)) {
        f.level = own->strength >= Strength::Strong ? Strength::Strong : Strength::Weak;
        f.text = (f.level == Strength::Strong ? QStringLiteral("⚠ ") : QString()) + own->label;
        f.pairIndex = -1;
        f.score = std::min(100.0, ownScore);  // as its row in the Possible cheating table
        return f;
    }
    if (!best) return f;
    f.score = best->score;
    f.level = best->level >= Strength::Strong ? Strength::Strong : Strength::Weak;
    // Partners sharing the same strongest signal.
    const QString label = best->strongestLabel();
    QStringList partners;
    for (const auto& p : b.dups.pairs) {
        if ((p.a != si && p.b != si) || p.level != best->level || p.strongestLabel() != label) continue;
        partners << b.dupEntries[p.a == si ? p.b : p.a].display;
    }
    partners.removeDuplicates();
    const QString prefix = f.level == Strength::Strong ? QStringLiteral("⚠ ") : QString();
    f.text = QStringLiteral("%1%2 as %3").arg(prefix, label, partners.join(", "));
    return f;
}

QString fileColumnText(const StudentRun& r) {
    const QString fn = QFileInfo(r.path).fileName();
    return r.identity.parsed ? QStringLiteral("%1 (%2)").arg(r.identity.name, fn) : fn;
}

QString correctnessText(const StudentRun& r) {
    if (!r.ok()) return QStringLiteral("error");
    if (!r.matched()) return QStringLiteral("not graded");  // an extra file
    return QStringLiteral("%1%").arg(r.result.correctness, 0, 'f', 1);
}

QString csvEscape(const QString& s) {
    QString t = s;
    // A cell a spreadsheet would run as a formula (a file, layer or login name starting with
    // = + - @) gets a leading apostrophe; numbers stay numbers (D76).
    if (!t.isEmpty() && QStringLiteral("=+-@\t\r").contains(t.front())) {
        bool number = false;
        t.toDouble(&number);
        if (!number) t.prepend(QLatin1Char('\''));
    }
    if (t.contains(',') || t.contains('"') || t.contains('\n') || t.contains('\r')) {
        t.replace('"', QStringLiteral("\"\""));
        return '"' + t + '"';
    }
    return t;
}

static QString header(const BatchOutput& b) {
    QString h;
    h += QStringLiteral("# DWG Checker %1, %2\n").arg(QStringLiteral(DWGCHECK_VERSION), QDateTime::currentDateTime().toString(Qt::ISODate));
    h += QStringLiteral("# parser: %1\n").arg(b.parser);
    for (int k = 0; k < static_cast<int>(b.refs.size()); ++k)
        h += QStringLiteral("# reference: %1 (sha256 %2)\n").arg(b.refPaths.value(k), QString(b.refs[k]->meta.sha256));
    if (b.multipleRefs()) {
        QStringList modes;
        for (int k = 0; k < static_cast<int>(b.refModes.size()); ++k)
            modes << QStringLiteral("%1 %2").arg(QFileInfo(b.refPaths.value(k)).fileName(), modeName(b.refModes[k]));
        h += QStringLiteral("# mode: %1\n").arg(modes.join(QStringLiteral(", ")));
    } else {
        h += QStringLiteral("# mode: %1\n").arg(modeName(b.mode));
    }
    return h;
}

QString resultsCsv(const BatchOutput& b) {
    QString out = header(b);
    const bool multi = b.multipleRefs();
    out += multi ? QStringLiteral("Student,Solution drawing,File,Possible cheating,Correctness,Errors\n")
                 : QStringLiteral("File,Possible cheating,Correctness,Errors\n");
    auto flagOf = [&](int i) {
        const CopyFlag f = copyFlagFor(b, i);
        return f.level == Strength::Info ? QStringLiteral("none") : QStringLiteral("%1: %2").arg(f.levelText(), f.text);
    };
    auto errorsOf = [&](const StudentRun& r) {
        if (!r.ok()) return QStringLiteral("could not read file: ") + r.loadError;
        if (!r.matched())
            return QStringLiteral("extra file, not matched to a solution drawing (closest: %1, %2)")
                .arg(QFileInfo(b.refPaths.value(r.closestRef)).fileName(), QStringLiteral("%1%").arg(r.result.correctness, 0, 'f', 1));
        return r.result.errorStrings().join(QStringLiteral("; "));
    };
    for (const StudentGroup& g : b.groups) {
        if (!multi) {  // one row per file, as with a single solution drawing
            for (int i : g.files) {
                const StudentRun& r = b.students[i];
                out += csvEscape(fileColumnText(r)) + ',' + csvEscape(flagOf(i)) + ',' + csvEscape(correctnessText(r)) + ',' +
                       csvEscape(errorsOf(r)) + '\n';
            }
            continue;
        }
        const QString who = csvEscape(g.identity.name);
        for (int k = 0; k < static_cast<int>(b.refs.size()); ++k) {
            const QString part = csvEscape(QFileInfo(b.refPaths.value(k)).fileName());
            const int i = k < static_cast<int>(g.byRef.size()) ? g.byRef[k] : -1;
            if (i < 0) {
                out += who + ',' + part + ",,," + QStringLiteral("0.0%,") + csvEscape(QStringLiteral("no file matched this solution drawing")) + '\n';
                continue;
            }
            const StudentRun& r = b.students[i];
            out += who + ',' + part + ',' + csvEscape(r.identity.fileName) + ',' + csvEscape(flagOf(i)) + ',' +
                   csvEscape(correctnessText(r)) + ',' + csvEscape(errorsOf(r)) + '\n';
        }
        for (int i : g.files) {
            const StudentRun& r = b.students[i];
            if (r.matched()) continue;
            out += who + QStringLiteral(",(extra file),") + csvEscape(r.identity.fileName) + ',' + csvEscape(flagOf(i)) + ',' +
                   csvEscape(correctnessText(r)) + ',' + csvEscape(errorsOf(r)) + '\n';
        }
        out += who + QStringLiteral(",(all drawings),,,") + QStringLiteral("%1%").arg(g.correctness, 0, 'f', 1) + QStringLiteral(",\n");
    }
    return out;
}

// A row per group of students linked by evidence, as in the Possible cheating tab but with
// every student and file, and informational groups too; one item per line in a cell (D82).
QString duplicatesCsv(const BatchOutput& b) {
    QString out = header(b);
    out += QStringLiteral("Students involved,Files involved,Level,Score,Signals,Details\n");
    const auto& E = b.dupEntries;
    for (const EvidenceGroup& g : groupEvidence(b.dups, E, true)) {
        QStringList students = g.students, files, lines, details;
        if (!g.with.isEmpty()) students << QStringLiteral("with ") + g.with;
        for (const int i : g.files) files << evidenceFileName(E[i]);
        for (const auto& l : g.lines) lines << l.text;
        auto detail = [&](const QString& where, const Signal& s) {
            details << QStringLiteral("%1: %2 (%3, weight %4): %5")
                           .arg(where, s.shown(),
                                strengthName(s.strength), QString::number(s.weight, 'f', 0), s.detail);
        };
        for (const int k : g.pairs) {
            const PairResult& p = b.dups.pairs[k];
            const QString where = QStringLiteral("%1 / %2").arg(evidenceFileName(E[p.a]), evidenceFileName(E[p.b]));
            for (const Signal& s : p.evidence) detail(where, s);
        }
        for (const Signal& s : g.ownSignals) detail(QStringLiteral("own files"), s);
        out += csvEscape(students.join('\n')) + ',' + csvEscape(files.join('\n')) + ',' + strengthName(g.level) + ',' +
               QString::number(g.score, 'f', 0) + ',' + csvEscape(lines.join('\n')) + ',' + csvEscape(details.join('\n')) + '\n';
    }
    if (!b.dups.notes.isEmpty()) {  // probable templates, account names shared by much of the class
        out += QStringLiteral("\nNote\n");
        for (const QString& n : b.dups.notes) out += csvEscape(n) + '\n';
    }
    return out;
}

}  // namespace dc
