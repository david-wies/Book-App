#include "book_card_delegate.h"
#include "../style_tokens.h"

#include <QPainter>
#include <QStyleOptionViewItem>
#include <QModelIndex>
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QFontMetrics>
#include <QRect>
#include <QPainterPath>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// Layout constants for list-mode rows
// ---------------------------------------------------------------------------

static constexpr int kThumbLeft   = SpacingMD;
static constexpr int kThumbTop    = (BookCardRowHeight - CoverThumbH) / 2;
static constexpr int kTextLeft    = kThumbLeft + CoverThumbW + SpacingMD;
static constexpr int kBtnWidth    = 80;
static constexpr int kBtnHeight   = 24;
static constexpr int kBtnSpacing  = SpacingXS;
static constexpr int kBtnRight    = SpacingMD; // margin from right edge

// ---------------------------------------------------------------------------

BookCardDelegate::BookCardDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{}

void BookCardDelegate::setViewMode(ViewMode mode)
{
    m_viewMode = mode;
}

// ---------------------------------------------------------------------------
// Button geometry helpers — all relative to rowRect.origin
// ---------------------------------------------------------------------------

QRect BookCardDelegate::detailsButtonRect(const QRect &r)
{
    const int x = r.right() - kBtnRight - kBtnWidth;
    const int y = r.top() + (BookCardRowHeight - 3 * kBtnHeight - 2 * kBtnSpacing) / 2;
    return {x, y, kBtnWidth, kBtnHeight};
}

QRect BookCardDelegate::actionButtonRect(const QRect &r)
{
    QRect d = detailsButtonRect(r);
    return {d.x(), d.bottom() + kBtnSpacing + 1, kBtnWidth, kBtnHeight};
}

QRect BookCardDelegate::removeButtonRect(const QRect &r)
{
    QRect a = actionButtonRect(r);
    return {a.x(), a.bottom() + kBtnSpacing + 1, kBtnWidth, kBtnHeight};
}

// ---------------------------------------------------------------------------

std::pair<QString, QString> BookCardDelegate::statusDisplay(const QString &status)
{
    if (status == QLatin1String("downloading") || status == QLatin1String("converting"))
        return {QStringLiteral("⟳ ") + status, ColorWarning};
    if (status == QLatin1String("downloaded"))
        return {QStringLiteral("✓ downloaded"), ColorSuccess};
    if (status == QLatin1String("audiobook_ready"))
        return {QStringLiteral("♪ audiobook ready"), ColorAccent};
    if (status == QLatin1String("error"))
        return {QStringLiteral("⚠ error"), ColorError};
    // saved (default)
    return {QStringLiteral("☆ saved"), ColorTextMuted};
}

// ---------------------------------------------------------------------------

void BookCardDelegate::paint(QPainter *painter,
                             const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    painter->save();

    if (m_viewMode == ViewMode::Grid)
        paintGridCard(painter, option, index);
    else
        paintListRow(painter, option, index);

    painter->restore();
}

void BookCardDelegate::paintListRow(QPainter *painter,
                                    const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    const QRect &r = option.rect;

    // Card background
    const bool selected = option.state & QStyle::State_Selected;
    painter->fillRect(r, selected ? QColor(ColorBackground).darker(103) : QColor(ColorSurface));

    // Bottom separator
    painter->setPen(QColor(ColorBorder));
    painter->drawLine(r.left(), r.bottom(), r.right(), r.bottom());

    // Cover placeholder (grey rounded rect)
    const QRect thumbRect(r.left() + kThumbLeft, r.top() + kThumbTop,
                          CoverThumbW, CoverThumbH);
    painter->setBrush(QColor(ColorBorder));
    painter->setPen(Qt::NoPen);
    QPainterPath path;
    path.addRoundedRect(thumbRect, RadiusSM, RadiusSM);
    painter->drawPath(path);

    // Title
    const QString title  = index.data(LibraryRole::Title).toString();
    const QString author = index.data(LibraryRole::Author).toString();
    const QString lang   = index.data(LibraryRole::Language).toString();
    const QString src    = index.data(LibraryRole::SourceName).toString();
    const QString status = index.data(LibraryRole::Status).toString();

    const int textRight = detailsButtonRect(r).left() - SpacingSM;

    // Title (bold, 14pt)
    QFont titleFont = option.font;
    titleFont.setPointSize(FontSizeSubtitle);
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(QColor(ColorTextPrimary));
    const QRect titleRect(kTextLeft, r.top() + SpacingSM, textRight - kTextLeft, 20);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width()));

    // Author (12pt, muted)
    QFont bodyFont = option.font;
    bodyFont.setPointSize(FontSizeBody);
    painter->setFont(bodyFont);
    painter->setPen(QColor(ColorTextMuted));
    const QRect authorRect(kTextLeft, titleRect.bottom() + SpacingXS, textRight - kTextLeft, 18);
    painter->drawText(authorRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(author, Qt::ElideRight, authorRect.width()));

    // Badge row: language pill + source pill + status label
    QFont badgeFont = option.font;
    badgeFont.setPointSize(FontSizeBadge);
    painter->setFont(badgeFont);

    int badgeX = kTextLeft;
    const int badgeY = authorRect.bottom() + SpacingXS;
    const int badgeH = 18;
    const QFontMetrics fm(badgeFont);

    auto drawBadge = [&](const QString &text, const char *bg,
                         const char *fg, const char *border) {
        const int w = fm.horizontalAdvance(text) + 16; // 8px padding each side
        const QRect br(badgeX, badgeY, w, badgeH);
        painter->setBrush(QColor(bg));
        painter->setPen(QColor(border));
        QPainterPath bp;
        bp.addRoundedRect(br, RadiusPill, RadiusPill);
        painter->drawPath(bp);
        painter->setPen(QColor(fg));
        painter->drawText(br, Qt::AlignCenter, text);
        badgeX += w + SpacingXS;
    };

    if (!lang.isEmpty())
        drawBadge(lang, ColorLangBadgeBg, ColorLangBadgeText, ColorLangBadgeBorder);
    if (!src.isEmpty())
        drawBadge(src, ColorSrcBadgeBg, ColorSrcBadgeText, ColorSrcBadgeBorder);

    // Status text after badges
    auto [statusText, statusColor] = statusDisplay(status);
    painter->setPen(QColor(statusColor));
    painter->setFont(badgeFont);
    const QRect statusRect(badgeX + SpacingXS, badgeY, textRight - badgeX - SpacingXS, badgeH);
    painter->drawText(statusRect, Qt::AlignLeft | Qt::AlignVCenter, statusText);

    // Action buttons
    auto drawBtn = [&](const QRect &btnRect, const QString &label) {
        painter->setBrush(QColor(ColorBackground));
        painter->setPen(QColor(ColorBorder));
        QPainterPath bp;
        bp.addRoundedRect(btnRect, RadiusSM, RadiusSM);
        painter->drawPath(bp);
        QFont btnFont = option.font;
        btnFont.setPointSize(FontSizeMeta);
        painter->setFont(btnFont);
        painter->setPen(QColor(ColorTextPrimary));
        painter->drawText(btnRect, Qt::AlignCenter, label);
    };

    drawBtn(detailsButtonRect(r), QStringLiteral("Details"));

    // The action button label depends on the current status
    const QString actionLabel = (status == QLatin1String("audiobook_ready"))
                                ? QStringLiteral("Audiobook")
                                : QStringLiteral("Download");
    drawBtn(actionButtonRect(r), actionLabel);
    drawBtn(removeButtonRect(r), QStringLiteral("Remove"));
}

void BookCardDelegate::paintGridCard(QPainter *painter,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    const QRect &r = option.rect;

    // Card background with rounded corners and border
    painter->setBrush(QColor(ColorSurface));
    painter->setPen(QColor(ColorBorder));
    QPainterPath path;
    path.addRoundedRect(r.adjusted(SpacingXS, SpacingXS, -SpacingXS, -SpacingXS),
                        RadiusMD, RadiusMD);
    painter->drawPath(path);

    // Cover placeholder occupying upper ~60% of card
    const int coverH = static_cast<int>(r.height() * 0.60);
    const QRect coverRect(r.left() + SpacingXS + SpacingSM,
                          r.top() + SpacingXS + SpacingSM,
                          r.width() - 2 * (SpacingXS + SpacingSM),
                          coverH - SpacingSM);
    painter->setBrush(QColor(ColorBorder));
    painter->setPen(Qt::NoPen);
    QPainterPath cp;
    cp.addRoundedRect(coverRect, RadiusSM, RadiusSM);
    painter->drawPath(cp);

    // Title
    const QString title  = index.data(LibraryRole::Title).toString();
    const QString author = index.data(LibraryRole::Author).toString();

    QFont titleFont = option.font;
    titleFont.setPointSize(FontSizeMeta);
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(QColor(ColorTextPrimary));

    const int textY = r.top() + SpacingXS + coverH + SpacingXS;
    const QRect titleRect(r.left() + SpacingSM, textY,
                          r.width() - 2 * SpacingSM, 16);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::TextWordWrap,
                      painter->fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width()));

    QFont bodyFont = option.font;
    bodyFont.setPointSize(FontSizeMeta);
    painter->setFont(bodyFont);
    painter->setPen(QColor(ColorTextMuted));
    const QRect authorRect(r.left() + SpacingSM, titleRect.bottom() + 2,
                           r.width() - 2 * SpacingSM, 14);
    painter->drawText(authorRect, Qt::AlignLeft,
                      painter->fontMetrics().elidedText(author, Qt::ElideRight, authorRect.width()));
}

QSize BookCardDelegate::sizeHint(const QStyleOptionViewItem & /*option*/,
                                 const QModelIndex & /*index*/) const
{
    if (m_viewMode == ViewMode::Grid)
        return {BookCardGridW, BookCardGridH};
    return {0, BookCardRowHeight};
}

bool BookCardDelegate::editorEvent(QEvent *event,
                                   QAbstractItemModel * /*model*/,
                                   const QStyleOptionViewItem &option,
                                   const QModelIndex &index)
{
    if (m_viewMode == ViewMode::Grid)
        return false; // grid mode has no per-cell buttons yet

    if (event->type() != QEvent::MouseButtonRelease)
        return false;

    auto *me = static_cast<QMouseEvent *>(event);
    const QPoint pos = me->pos();
    const QRect &r   = option.rect;

    const int itemId  = index.data(LibraryRole::LibraryItemId).toInt();
    const QString bId = index.data(LibraryRole::BookId).toString();

    if (detailsButtonRect(r).contains(pos)) {
        emit detailsRequested(itemId, bId);
        return true;
    }
    if (actionButtonRect(r).contains(pos)) {
        emit downloadRequested(itemId, bId);
        return true;
    }
    if (removeButtonRect(r).contains(pos)) {
        emit removeRequested(itemId, bId);
        return true;
    }
    return false;
}

} // namespace bookhub::gui
