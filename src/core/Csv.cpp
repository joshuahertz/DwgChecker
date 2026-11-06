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
    if (si >= 0 && si < static_cast<int>(b.dups.fileNotes.size()))
        for (const QString& n : b.dups.fileNotes[si]) tips << QStringLiteral("[note] ") + n;
    f.tooltip = tips.join('\n');
    if (!best) return f;
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
    return QStringLiteral("%1%").arg(r.result.correctness, 0, 'f', 1);
}

QString csvEscape(const QString& s) {
    if (s.contains(',') || s.contains('"') || s.contains('\n') || s.contains('\r')) {
        QString t = s;
        t.replace('"', QStringLiteral("\"\""));
        return '"' + t + '"';
    }
    return s;
}

static QString header(const BatchOutput& b, const QString& refPath) {
    QString h;
    h += QStringLiteral("# DWG Checker %1, %2\n").arg(QStringLiteral(DWGCHECK_VERSION), QDateTime::currentDateTime().toString(Qt::ISODate));
    h += QStringLiteral("# parser: %1\n").arg(b.parser);
    h += QStringLiteral("# reference: %1 (sha256 %2)\n").arg(refPath, b.ref ? QString(b.ref->meta.sha256) : QString());
    h += QStringLiteral("# mode: %1\n").arg(modeName(b.mode));
    return h;
}

QString resultsCsv(const BatchOutput& b, const QString& refPath) {
    QString out = header(b, refPath);
    out += QStringLiteral("File,Copy flag,Correctness,Errors\n");
    for (int i = 0; i < static_cast<int>(b.students.size()); ++i) {
        const StudentRun& r = b.students[i];
        const CopyFlag f = copyFlagFor(b, i);
        const QString flag = f.level == Strength::Info ? QStringLiteral("none") : QStringLiteral("%1: %2").arg(f.levelText(), f.text);
        const QString errors = r.ok() ? r.result.errorStrings().join(QStringLiteral("; ")) : QStringLiteral("could not read file: ") + r.loadError;
        out += csvEscape(fileColumnText(r)) + ',' + csvEscape(flag) + ',' + csvEscape(correctnessText(r)) + ',' + csvEscape(errors) + '\n';
    }
    return out;
}

QString duplicatesCsv(const BatchOutput& b, const QString& refPath) {
    QString out = header(b, refPath);
    out += QStringLiteral("File A,File B,Level,Score,Signals\n");
    for (const auto& p : b.dups.pairs) {
        QStringList sig;
        for (const auto& s : p.evidence) sig << QStringLiteral("%1 (%2): %3").arg(s.label, strengthName(s.strength), s.detail);
        out += csvEscape(b.dupEntries[p.a].display) + ',' + csvEscape(b.dupEntries[p.b].display) + ',' + strengthName(p.level) + ',' +
               QString::number(p.score, 'f', 0) + ',' + csvEscape(sig.join(QStringLiteral("; "))) + '\n';
    }
    if (!b.dups.clusters.empty()) {
        out += QStringLiteral("\nCluster,Members\n");
        int k = 1;
        for (const auto& c : b.dups.clusters) {
            QStringList names;
            for (int i : c) names << b.dupEntries[i].display;
            out += QString::number(k++) + ',' + csvEscape(names.join(QStringLiteral("; "))) + '\n';
        }
    }
    return out;
}

}  // namespace dc
