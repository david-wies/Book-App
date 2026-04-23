#include "search_result_delegate.h"
#include "../style_tokens.h"

#include <QPainter>
#include <QStyleOptionViewItem>
#include <QModelIndex>
#include <QEvent>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QRect>
#include <QPainterPath>
#include <QStringList>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// Row-level layout constants (all relative to row rect)
// ---------------------------------------------------------------------------

static constexpr int kRowHeight  = 72;
static constexpr int kThumbW     = 48;
static constexpr int kThumbH     = 64;
static constexpr int kThumbLeft  = SpacingMD;
static constexpr int kThumbTop   = (kRowHeight - kThumbH) / 2;
static constexpr int kTextLeft   = kThumbLeft + kThumbW + SpacingMD;
static constexpr int kBtnW       = 90;
static constexpr int kBtnH       = 26;
static constexpr int kBtnRight   = SpacingMD;

// ---------------------------------------------------------------------------

SearchResultDelegate::SearchResultDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{}

QRect SearchResultDelegate::libraryButtonRect(const QRect &r)
{
    return {r.right() - kBtnRight - kBtnW,
            r.top() + (kRowHeight - kBtnH) / 2,
            kBtnW, kBtnH};
}

// ---------------------------------------------------------------------------

QSize SearchResultDelegate::sizeHint(const QStyleOptionViewItem & /*option*/,
                                     const QModelIndex & /*index*/) const
{
    return {0, kRowHeight};
}

void SearchResultDelegate::paint(QPainter *painter,
                                 const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    painter->save();

    const QRect &r = option.rect;

    // Row background + separator
    const bool selected = option.state & QStyle::State_Selected;
    painter->fillRect(r, selected ? QColor(ColorBackground).darker(103)
                                  : QColor(ColorSurface));
    painter->setPen(QColor(ColorBorder));
    painter->drawLine(r.left(), r.bottom(), r.right(), r.bottom());

    // Cover placeholder
    const QRect thumbRect(r.left() + kThumbLeft,
                          r.top() + kThumbTop,
                          kThumbW, kThumbH);
    painter->setBrush(QColor(ColorBorder));
    painter->setPen(Qt::NoPen);
    QPainterPath tp;
    tp.addRoundedRect(thumbRect, RadiusSM, RadiusSM);
    painter->drawPath(tp);

    // Data
    const QString title  = index.data(SearchRole::Title).toString();
    const QString author = index.data(SearchRole::Author).toString();
    const int year       = index.data(SearchRole::PublishYear).toInt();
    const QStringList languages = index.data(SearchRole::Languages).toStringList();
    const QStringList sources   = index.data(SearchRole::Sources).toStringList();
    const QStringList formats   = index.data(SearchRole::Formats).toStringList();
    const bool inLibrary        = index.data(SearchRole::InLibrary).toBool();

    const QRect btnRect = libraryButtonRect(r);
    const int textRight = btnRect.left() - SpacingSM;

    // Title (bold, 13pt)
    QFont titleFont = option.font;
    titleFont.setPointSize(13);
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(QColor(ColorTextPrimary));
    const QRect titleRect(kTextLeft, r.top() + SpacingXS + 2, textRight - kTextLeft, 20);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width()));

    // Author + year (12pt, muted)
    QFont bodyFont = option.font;
    bodyFont.setPointSize(FontSizeBody);
    painter->setFont(bodyFont);
    painter->setPen(QColor(ColorTextMuted));
    const QString authorYear = year > 0
        ? QStringLiteral("%1 · %2").arg(author).arg(year)
        : author;
    const QRect authorRect(kTextLeft, titleRect.bottom() + 2, textRight - kTextLeft, 18);
    painter->drawText(authorRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(authorYear, Qt::ElideRight, authorRect.width()));

    // Badge row
    QFont badgeFont = option.font;
    badgeFont.setPointSize(FontSizeBadge);
    painter->setFont(badgeFont);
    const QFontMetrics fm(badgeFont);

    int badgeX     = kTextLeft;
    const int badgeY = authorRect.bottom() + SpacingXS;
    const int badgeH = 16;

    auto drawBadge = [&](const QString &text,
                         const char *bg, const char *fg, const char *border) {
        const int w = fm.horizontalAdvance(text) + 12;
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

    for (const QString &lang : languages)
        drawBadge(lang, ColorLangBadgeBg, ColorLangBadgeText, ColorLangBadgeBorder);
    for (const QString &src : sources)
        drawBadge(src, ColorSrcBadgeBg, ColorSrcBadgeText, ColorSrcBadgeBorder);

    // Format tags (small grey)
    if (!formats.isEmpty()) {
        // Bullet separator between badges and format tags
        if (!languages.isEmpty() || !sources.isEmpty()) {
            painter->setPen(QColor(ColorTextMuted));
            painter->drawText(QRect(badgeX, badgeY, 10, badgeH),
                              Qt::AlignCenter, QStringLiteral("·"));
            badgeX += 10 + SpacingXS;
        }
        painter->setPen(QColor(ColorTextMuted));
        const QString fmtText = formats.join(QLatin1String(", "));
        painter->drawText(QRect(badgeX, badgeY, textRight - badgeX, badgeH),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          fm.elidedText(fmtText, Qt::ElideRight, textRight - badgeX));
    }

    // "+ Library" / "✓ In Library" button
    {
        const QString btnLabel = inLibrary
            ? QStringLiteral("✓ In Library")
            : QStringLiteral("+ Library");
        const char *btnBg  = inLibrary ? ColorSurface  : ColorAccent;
        const char *btnFg  = inLibrary ? ColorSuccess   : "#FFFFFF";
        const char *btnBdr = inLibrary ? ColorSuccess   : ColorAccent;

        painter->setBrush(QColor(btnBg));
        painter->setPen(QColor(btnBdr));
        QPainterPath bp;
        bp.addRoundedRect(btnRect, RadiusSM, RadiusSM);
        painter->drawPath(bp);

        QFont btnFont = option.font;
        btnFont.setPointSize(FontSizeMeta);
        painter->setFont(btnFont);
        painter->setPen(QColor(btnFg));
        painter->drawText(btnRect, Qt::AlignCenter, btnLabel);
    }

    painter->restore();
}

bool SearchResultDelegate::editorEvent(QEvent *event,
                                       QAbstractItemModel * /*model*/,
                                       const QStyleOptionViewItem &option,
                                       const QModelIndex &index)
{
    if (event->type() != QEvent::MouseButtonRelease)
        return false;

    auto *me = static_cast<QMouseEvent *>(event);
    const QRect btnRect = libraryButtonRect(option.rect);

    if (btnRect.contains(me->pos())) {
        const QString bookId = index.data(SearchRole::BookId).toString();
        const bool inLibrary = index.data(SearchRole::InLibrary).toBool();
        if (!inLibrary)
            emit addToLibraryRequested(bookId);
        return true;
    }

    // Any click outside the button is treated as a row selection / details request.
    if (option.rect.contains(me->pos())) {
        const QString bookId = index.data(SearchRole::BookId).toString();
        if (!bookId.isEmpty())
            emit detailsRequested(bookId);
    }

    return false;
}

} // namespace bookhub::gui
