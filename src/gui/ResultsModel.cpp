#include "ResultsModel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <climits>

using namespace dc;

namespace dcgui {

namespace {
constexpr int kPad = 4;
constexpr int kMaxTooltipErrors = 20;
const QColor kErr(230, 230, 230);
const QColor kStrong(204, 36, 28), kWeak(230, 150, 0);
constexpr int kFlagIcon = 16;

// Red at 0% through yellow at 50% to green at 100%: hue 0 to 120 degrees, light
// enough for the dark text drawn on it.
QColor correctnessColor(double percent) {
    const double t = std::clamp(percent / 100.0, 0.0, 1.0);
    return QColor::fromHsvF(static_cast<float>(t / 3.0), 0.45f, 1.0f);
}

QString percentText(double v) { return QStringLiteral("%1%").arg(v, 0, 'f', 1); }

int errorCount(const StudentRun& r) {
    if (!r.ok()) return INT_MAX;  // unreadable files sort after every graded one
    int n = 0;
    for (const auto& e : r.result.errors) n += !e.label.isEmpty();
    return n;
}

QString errorsTooltip(const StudentRun& r) {
    if (!r.ok()) return QObject::tr("could not read file: %1").arg(r.loadError);
    QStringList lines = r.result.errorStrings();
    if (lines.isEmpty()) return QObject::tr("No errors. Click the row for the detail view.");
    if (lines.size() > kMaxTooltipErrors) {
        const auto more = lines.size() - kMaxTooltipErrors;
        lines = lines.mid(0, kMaxTooltipErrors);
        lines << QObject::tr("… and %1 more").arg(more);
    }
    return lines.join('\n') + QObject::tr("\n\nClick the row for the detail view.");
}

QString flagTooltip(const CopyFlag& f) {
    QString t = f.level == Strength::Info ? QString() : f.text;
    if (!f.tooltip.isEmpty()) t += (t.isEmpty() ? QString() : QStringLiteral("\n\n")) + f.tooltip;
    return t;
}
}  // namespace

void ResultsModel::setBatch(std::shared_ptr<const BatchOutput> b) {
    beginResetModel();
    m_batch = std::move(b);
    m_flags.clear();
    m_groupFlags.clear();
    m_subs.clear();
    if (m_batch) {
        for (int i = 0; i < static_cast<int>(m_batch->students.size()); ++i) m_flags.push_back(copyFlagFor(*m_batch, i));
        for (const StudentGroup& g : m_batch->groups) {
            // The strongest flag among the student's files; every file's evidence in the tooltip.
            CopyFlag best;
            QStringList tips;
            double bestScore = -1;
            for (int f : g.files) {
                const CopyFlag& cf = m_flags[f];
                const double score = cf.pairIndex >= 0 ? m_batch->dups.pairs[cf.pairIndex].score : 0;
                if (cf.level > best.level || (cf.level == best.level && score > bestScore)) {
                    best = cf;
                    bestScore = score;
                }
                if (!cf.tooltip.isEmpty())
                    tips << (g.files.size() > 1 ? m_batch->students[f].identity.fileName + QStringLiteral(":\n") : QString()) + cf.tooltip;
            }
            best.tooltip = tips.join(QStringLiteral("\n\n"));
            m_groupFlags.push_back(best);
            // Subrows: one per solution drawing, then the extra files.
            std::vector<Sub> subs;
            if (m_batch->multipleRefs() || g.files.size() > 1) {
                for (int k = 0; k < static_cast<int>(g.byRef.size()); ++k) subs.push_back({g.byRef[k], k});
                for (int f : g.files)
                    if (!m_batch->students[f].matched()) subs.push_back({f, -1});
            }
            m_subs.push_back(std::move(subs));
        }
    }
    endResetModel();
}

QModelIndex ResultsModel::index(int row, int column, const QModelIndex& parent) const {
    if (!m_batch || row < 0 || column < 0 || column >= ColCount) return {};
    if (!parent.isValid()) {
        if (row >= static_cast<int>(m_batch->groups.size())) return {};
        return createIndex(row, column, quintptr(0));
    }
    if (parent.internalId() != 0) return {};  // subrows have no children
    const int g = parent.row();
    if (g >= static_cast<int>(m_subs.size()) || row >= static_cast<int>(m_subs[g].size())) return {};
    return createIndex(row, column, quintptr(g + 1));
}

QModelIndex ResultsModel::parent(const QModelIndex& child) const {
    if (!child.isValid() || child.internalId() == 0) return {};
    return createIndex(static_cast<int>(child.internalId()) - 1, 0, quintptr(0));
}

int ResultsModel::rowCount(const QModelIndex& parent) const {
    if (!m_batch) return 0;
    if (!parent.isValid()) return static_cast<int>(m_batch->groups.size());
    if (parent.internalId() != 0 || parent.column() != 0) return 0;
    return static_cast<int>(m_subs[parent.row()].size());
}

QVariant ResultsModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal) return {};
    if (role == Qt::InitialSortOrderRole)
        return section == ColFlag ? static_cast<int>(Qt::DescendingOrder) : static_cast<int>(Qt::AscendingOrder);
    if (role == Qt::ToolTipRole && section == ColFlag)
        return tr("Possible duplicate (copying): red = strong evidence, amber = weaker. Hover over an icon for the details.");
    if (role != Qt::DisplayRole) return {};
    switch (section) {
    case ColName: return tr("Student");
    case ColAcad: return tr("AutoCAD");
    case ColFlag: return tr("Poss. dup.");
    case ColCorrectness: return tr("Correctness");
    case ColErrors: return tr("Errors");
    }
    return {};
}

QVariant ResultsModel::data(const QModelIndex& idx, int role) const {
    if (!m_batch || !idx.isValid()) return {};
    if (idx.internalId() == 0) return groupData(idx.row(), idx.column(), role);
    const int g = static_cast<int>(idx.internalId()) - 1;
    const Sub& s = m_subs[g][idx.row()];
    return s.file >= 0 ? fileData(s.file, idx.column(), role, true) : noFileData(g, s.ref, idx.column(), role);
}

QVariant ResultsModel::groupData(int gi, int column, int role) const {
    const StudentGroup& g = m_batch->groups[gi];
    if (m_subs[gi].empty()) {  // a single file: the student row is that file's row
        if (role == HasChildrenRole) return false;
        return fileData(g.files.front(), column, role, false);
    }
    const CopyFlag& f = m_groupFlags[gi];
    const bool multi = m_batch->multipleRefs();
    auto refName = [&](int k) { return QFileInfo(m_batch->refPaths.value(k)).completeBaseName(); };
    auto combinedErrors = [&]() {
        QStringList parts;
        if (multi) {
            for (int k = 0; k < static_cast<int>(g.byRef.size()); ++k) {
                const int fi = g.byRef[k];
                parts << QStringLiteral("%1: %2").arg(refName(k), fi >= 0 ? m_batch->students[fi].result.errorSummary() : tr("no file"));
            }
        } else if (!g.byRef.empty() && g.byRef[0] >= 0) {
            parts << m_batch->students[g.byRef[0]].result.errorSummary();
        } else {
            parts << tr("no readable file");
        }
        int extra = 0;
        for (int fi : g.files) extra += !m_batch->students[fi].matched();
        QString t = parts.join(QStringLiteral("  ·  "));
        if (extra > 0) t += tr("; %1 extra file(s)").arg(extra);
        return t;
    };
    switch (role) {
    case StudentIndexRole: return -1;
    case HasChildrenRole: return true;
    case FlagLevelRole: return static_cast<int>(f.level);
    case LoadedRole: return true;
    case FileNameRole: {
        if (!multi) return tr("%1 files").arg(g.files.size());
        int submitted = 0;
        for (int fi : g.byRef) submitted += fi >= 0;
        return tr("%1 of %2 drawings submitted").arg(submitted).arg(g.byRef.size());
    }
    case Qt::ToolTipRole:
        switch (column) {
        case ColName: return tr("Click to show or hide this student's files");
        case ColFlag: {
            const QString t = flagTooltip(f);
            return t.isEmpty() ? QVariant() : QVariant(t);
        }
        case ColCorrectness: return multi ? tr("The mean over the %1 solution drawings; a drawing without a file counts 0%").arg(g.byRef.size()) : QVariant();
        case ColErrors: return combinedErrors();
        }
        return {};
    case SortRole:
        switch (column) {
        case ColName:
        case ColAcad: return g.identity.name.toLower() + g.key.toLower();
        case ColFlag: return static_cast<int>(f.level) * 1000 + (f.pairIndex >= 0 ? static_cast<int>(m_batch->dups.pairs[f.pairIndex].score) : 0);
        case ColCorrectness: return g.correctness;
        case ColErrors: {
            int n = 0;
            for (int fi : g.byRef) n += fi >= 0 ? std::min(errorCount(m_batch->students[fi]), 1000) : 1000;
            return n;
        }
        }
        return {};
    case Qt::DisplayRole:
        switch (column) {
        case ColName: return g.identity.name;
        case ColCorrectness: return percentText(g.correctness);
        case ColErrors: return combinedErrors();
        default: return QString();
        }
    case Qt::BackgroundRole:
        if (column == ColCorrectness) return correctnessColor(g.correctness);
        return {};
    case Qt::ForegroundRole:
        if (column == ColFlag) return f.level == Strength::Strong ? kStrong : kWeak;
        return {};
    }
    return {};
}

QVariant ResultsModel::fileData(int fi, int column, int role, bool subrow) const {
    const StudentRun& r = m_batch->students[fi];
    const CopyFlag& f = m_flags[fi];
    const bool multi = m_batch->multipleRefs();
    auto refName = [&](int k) { return QFileInfo(m_batch->refPaths.value(k)).fileName(); };
    auto errorsText = [&]() {
        if (!r.ok()) return tr("could not read file: %1").arg(r.loadError);
        if (!r.matched())
            return tr("not matched to a solution drawing; closest: %1 (%2)").arg(refName(r.closestRef), percentText(r.result.correctness));
        return r.result.errorSummary();
    };
    switch (role) {
    case StudentIndexRole: return fi;
    case HasChildrenRole: return false;
    case FlagLevelRole: return static_cast<int>(f.level);
    case LoadedRole: return r.ok();
    case FileNameRole: return r.identity.fileName;
    case Qt::ToolTipRole:
        switch (column) {
        case ColName: return QDir::toNativeSeparators(r.path);
        case ColAcad: return tr("Open this file in AutoCAD\n%1").arg(QDir::toNativeSeparators(r.path));
        case ColFlag: {
            const QString t = flagTooltip(f);
            return t.isEmpty() ? QVariant() : QVariant(t);
        }
        case ColErrors: return r.ok() && !r.matched() ? QVariant(errorsText()) : QVariant(errorsTooltip(r));
        }
        return {};
    case SortRole:
        switch (column) {
        case ColName:
            if (subrow) return r.matched() ? r.refIndex : 1000 + fi;  // subrows stay in solution-drawing order
            return r.identity.name.toLower() + r.identity.fileName.toLower();
        case ColAcad: return r.identity.fileName.toLower();
        case ColFlag: return static_cast<int>(f.level) * 1000 + (f.pairIndex >= 0 ? static_cast<int>(m_batch->dups.pairs[f.pairIndex].score) : 0);
        case ColCorrectness: return r.ok() && r.matched() ? r.result.correctness : -1.0;
        case ColErrors: return errorCount(r);
        }
        return {};
    case Qt::DisplayRole:
        switch (column) {
        case ColName:
            if (!subrow) return r.identity.name;
            if (!r.ok()) return tr("could not be read");
            if (!r.matched()) return tr("extra file (not graded)");
            return multi ? refName(r.refIndex) : tr("graded file");
        case ColAcad: return tr("Open in AutoCAD");
        case ColCorrectness: return correctnessText(r);
        case ColErrors: return errorsText();
        default: return QString();  // the flag is an icon; the details are in the tooltip
        }
    case Qt::BackgroundRole:
        if (column == ColCorrectness) return !r.ok() || !r.matched() ? kErr : correctnessColor(r.result.correctness);
        return {};
    case Qt::ForegroundRole:
        if (column == ColFlag) return f.level == Strength::Strong ? kStrong : kWeak;
        return {};
    }
    return {};
}

QVariant ResultsModel::noFileData(int, int ref, int column, int role) const {
    switch (role) {
    case StudentIndexRole: return -1;
    case HasChildrenRole: return false;
    case FlagLevelRole: return static_cast<int>(Strength::Info);
    case LoadedRole: return false;
    case FileNameRole: return tr("no file submitted for this drawing");
    case SortRole:
        switch (column) {
        case ColName: return ref;
        case ColCorrectness: return 0.0;
        case ColErrors: return INT_MAX - 1;
        default: return 0;
        }
    case Qt::DisplayRole:
        switch (column) {
        case ColName: return QFileInfo(m_batch->refPaths.value(ref)).fileName();
        case ColCorrectness: return percentText(0);
        case ColErrors: return tr("no file matched this solution drawing");
        default: return QString();
        }
    case Qt::BackgroundRole:
        if (column == ColCorrectness) return correctnessColor(0);
        return {};
    }
    return {};
}

// ---------------------------------------------------------------------------

void ResultsDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& idx) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, idx);
    const QString text = opt.text;
    opt.text.clear();
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, p, opt.widget);
    p->save();
    const QFontMetrics fm(opt.font);
    const QRect inner = opt.rect.adjusted(kPad, kPad, -kPad, -kPad);
    const bool selected = opt.state & QStyle::State_Selected;
    const QColor fg = selected ? opt.palette.highlightedText().color() : opt.palette.text().color();
    switch (idx.column()) {
    case ColName: {
        QFont bold = opt.font;
        bold.setBold(true);
        p->setFont(bold);
        p->setPen(fg);
        p->drawText(QRect(inner.left(), inner.top(), inner.width(), fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(bold).elidedText(text, Qt::ElideRight, inner.width()));
        p->setFont(opt.font);
        p->setPen(selected ? fg : opt.palette.color(QPalette::PlaceholderText));
        p->drawText(QRect(inner.left(), inner.top() + fm.height(), inner.width(), fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(idx.data(FileNameRole).toString(), Qt::ElideMiddle, inner.width()));
        break;
    }
    case ColAcad: {
        QFont link = opt.font;
        link.setUnderline(true);
        p->setFont(link);
        p->setPen(selected ? fg : opt.palette.link().color());
        p->drawText(inner, Qt::AlignLeft | Qt::AlignTop, fm.elidedText(text, Qt::ElideRight, inner.width()));
        break;
    }
    case ColFlag: {
        if (idx.data(FlagLevelRole).toInt() == static_cast<int>(Strength::Info)) break;
        // A warning triangle with "!", centered at the top of the cell.
        const QColor c = idx.data(Qt::ForegroundRole).value<QColor>();
        const QRectF box(inner.center().x() - kFlagIcon / 2.0, inner.top(), kFlagIcon, kFlagIcon);
        QPainterPath tri;
        tri.moveTo(box.center().x(), box.top() + 0.5);
        tri.lineTo(box.right() - 0.5, box.bottom() - 0.5);
        tri.lineTo(box.left() + 0.5, box.bottom() - 0.5);
        tri.closeSubpath();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(c.darker(130), 1));
        p->setBrush(c);
        p->drawPath(tri);
        QFont bold = opt.font;
        bold.setBold(true);
        bold.setPixelSize(kFlagIcon - 5);
        p->setFont(bold);
        p->setPen(Qt::white);
        p->drawText(box.adjusted(0, 3, 0, 0), Qt::AlignCenter, QStringLiteral("!"));
        break;
    }
    case ColCorrectness: {
        QFont bold = opt.font;
        bold.setBold(true);
        p->setFont(bold);
        p->setPen(selected ? fg : QColor(20, 20, 20));
        p->drawText(inner, Qt::AlignCenter, text);
        break;
    }
    case ColErrors:
        p->setPen(fg);
        p->drawText(inner, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
        break;
    }
    p->restore();
}

QSize ResultsDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& idx) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, idx);
    const QFontMetrics fm(opt.font);
    const int width = std::max(50, opt.rect.width() > 0 ? opt.rect.width() : 400);
    auto wrapped = [&](int w) {
        const QRect r = fm.boundingRect(QRect(0, 0, std::max(1, w - 2 * kPad), 100000), Qt::TextWordWrap, opt.text);
        return std::max(r.height(), fm.height()) + 2 * kPad;
    };
    switch (idx.column()) {
    case ColName: return QSize(260, 2 * fm.height() + 2 * kPad);
    case ColAcad: return QSize(fm.horizontalAdvance(opt.text) + 2 * kPad + 8, fm.height() + 2 * kPad);
    case ColFlag: return QSize(kFlagIcon + 2 * kPad, std::max(kFlagIcon, fm.height()) + 2 * kPad);
    case ColCorrectness: return QSize(110, fm.height() + 2 * kPad);
    case ColErrors: return QSize(width, wrapped(width));
    }
    return QStyledItemDelegate::sizeHint(option, idx);
}

}  // namespace dcgui
