#include "badge_label.h"
#include "../style_tokens.h"

namespace bookhub::gui {

BadgeLabel::BadgeLabel(const QString &text, Kind kind, QWidget *parent)
    : QLabel(text, parent)
{
    applyStyle(kind);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    setAccessibleName(text);
}

void BadgeLabel::applyStyle(Kind kind)
{
    QString bg, fg, border;

    switch (kind) {
    case Kind::Language:
        bg     = ColorLangBadgeBg;
        fg     = ColorLangBadgeText;
        border = ColorLangBadgeBorder;
        break;
    case Kind::Source:
        bg     = ColorSrcBadgeBg;
        fg     = ColorSrcBadgeText;
        border = ColorSrcBadgeBorder;
        break;
    case Kind::Genre:
        bg     = ColorTagBg;
        fg     = ColorTagText;
        border = QStringLiteral("transparent");
        break;
    }

    setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: %4px;"
        "  padding: 2px 8px;"
        "  font-size: %5pt;"
        "}"
    ).arg(bg, fg, border)
     .arg(RadiusPill)
     .arg(FontSizeBadge));
}

} // namespace bookhub::gui
