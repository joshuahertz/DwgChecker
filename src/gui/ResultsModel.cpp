#include "ResultsModel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>

#include <algorithm>
#include <climits>

using namespace dc;

namespace dcgui {

namespace {
constexpr int kPad = 4;
constexpr int kMaxTooltipErrors = 20;
const QColor kErr(230, 230, 230);
const QColor kStrong(196, 40, 28), kWeak(196, 128, 0);

// Red at 0% through yellow at 50% to green at 100%: hue 0 to 120 degrees, light
// enough for the dark text drawn on it.
QColor correctnessColor(double percent) {
    const double t = std::clamp(percent / 100.0, 0.0, 1.0);
    return QColor::fromHsvF(static_cast<float>(t / 3.0), 0.45f, 1.0f);
}

QString summaryOf(const StudentRun& r) {
    if (!r.ok()) return QObject::tr("could not read file: %1").arg(r.loadError);
    return r.result.errorSummary();
}

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
}  // namespace

void ResultsModel::setBatch(std::shared_ptr<const BatchOutput> b) {
    beginResetModel();
    m_batch = std::move(b);
    m_flags.clear();
    if (m_batch)
        for (int i = 0; i < static_cast<int>(m_batch->students.size()); ++i) m_flags.push_back(copyFlagFor(*m_batch, i));
    endResetModel();
}

int ResultsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_batch) return 0;
    return static_cast<int>(m_batch->students.size());
}

QVariant ResultsModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal) return {};
    if (role == Qt::InitialSortOrderRole)
        return section == ColFlag ? static_cast<int>(Qt::DescendingOrder) : static_cast<int>(Qt::AscendingOrder);
    if (role != Qt::DisplayRole) return {};
    switch (section) {
    case ColName: return tr("Student");
    case ColAcad: return tr("AutoCAD");
    case ColFlag: return tr("Copy flag");
    case ColCorrectness: return tr("Correctness");
    case ColErrors: return tr("Errors");
    }
    return {};
}

QVariant ResultsModel::data(const QModelIndex& idx, int role) const {
    if (!m_batch || !idx.isValid()) return {};
    const int row = idx.row();
    const StudentRun& r = m_batch->students[row];
    const CopyFlag& f = m_flags[row];
    switch (role) {
    case StudentIndexRole: return row;
    case FlagLevelRole: return static_cast<int>(f.level);
    case LoadedRole: return r.ok();
    case FileNameRole: return QFileInfo(r.path).fileName();
    case Qt::ToolTipRole:
        switch (idx.column()) {
        case ColName: return QDir::toNativeSeparators(r.path);
        case ColAcad: return tr("Open this file in AutoCAD\n%1").arg(QDir::toNativeSeparators(r.path));
        case ColFlag: return f.tooltip.isEmpty() ? QVariant() : QVariant(f.tooltip);
        case ColErrors: return errorsTooltip(r);
        }
        return {};
    case SortRole:
        switch (idx.column()) {
        case ColName: return r.identity.name.toLower() + QFileInfo(r.path).fileName().toLower();
        case ColAcad: return QFileInfo(r.path).fileName().toLower();
        case ColFlag: return static_cast<int>(f.level) * 1000 + (f.pairIndex >= 0 ? static_cast<int>(m_batch->dups.pairs[f.pairIndex].score) : 0);
        case ColCorrectness: return r.ok() ? r.result.correctness : -1.0;
        case ColErrors: return errorCount(r);
        }
        return {};
    case Qt::DisplayRole:
        switch (idx.column()) {
        case ColName: return r.identity.name;
        case ColAcad: return tr("Open in AutoCAD");
        case ColFlag: return f.level == Strength::Info ? QString() : f.text;
        case ColCorrectness: return correctnessText(r);
        case ColErrors: return summaryOf(r);
        }
        return {};
    case Qt::BackgroundRole:
        if (idx.column() == ColCorrectness) return !r.ok() ? kErr : correctnessColor(r.result.correctness);
        return {};
    case Qt::ForegroundRole:
        if (idx.column() == ColFlag) return f.level == Strength::Strong ? kStrong : kWeak;
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
        const QVariant c = idx.data(Qt::ForegroundRole);
        p->setPen(selected ? fg : c.value<QColor>());
        p->drawText(inner, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
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
    case ColFlag: return QSize(240, wrapped(240));
    case ColCorrectness: return QSize(110, fm.height() + 2 * kPad);
    case ColErrors: return QSize(width, wrapped(width));
    }
    return QStyledItemDelegate::sizeHint(option, idx);
}

}  // namespace dcgui
