#include "empty_state_widget.h"
#include "../style_tokens.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace bookhub::gui {

EmptyStateWidget::EmptyStateWidget(const QIcon &icon,
                                   const QString &headline,
                                   const QString &body,
                                   const QString &ctaLabel,
                                   QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(SpacingMD);
    layout->setContentsMargins(SpacingLG, SpacingXL, SpacingLG, SpacingXL);

    // Icon label — 80×80 px placeholder area
    auto *iconLabel = new QLabel(this);
    iconLabel->setAlignment(Qt::AlignCenter);
    if (!icon.isNull()) {
        iconLabel->setPixmap(icon.pixmap(80, 80));
    } else {
        // Fallback: render a simple grey rectangle so the layout is consistent
        // even when no icon resource is available at this stage of development.
        iconLabel->setFixedSize(80, 80);
        iconLabel->setStyleSheet(QStringLiteral(
            "background-color: %1; border-radius: %2px;"
        ).arg(ColorBorder).arg(RadiusMD));
    }
    layout->addWidget(iconLabel, 0, Qt::AlignHCenter);

    auto *headlineLabel = new QLabel(headline, this);
    headlineLabel->setAlignment(Qt::AlignCenter);
    headlineLabel->setStyleSheet(QStringLiteral(
        "font-size: %1pt; color: %2;"
    ).arg(FontSizeSubtitle).arg(ColorTextMuted));
    layout->addWidget(headlineLabel);

    if (!body.isEmpty()) {
        auto *bodyLabel = new QLabel(body, this);
        bodyLabel->setAlignment(Qt::AlignCenter);
        bodyLabel->setWordWrap(true);
        bodyLabel->setStyleSheet(QStringLiteral(
            "font-size: %1pt; color: %2;"
        ).arg(FontSizeBody).arg(ColorTextMuted));
        layout->addWidget(bodyLabel);
    }

    if (!ctaLabel.isEmpty()) {
        auto *cta = new QPushButton(ctaLabel, this);
        cta->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background-color: %1;"
            "  color: white;"
            "  border: none;"
            "  border-radius: %2px;"
            "  padding: 8px 24px;"
            "  font-size: %3pt;"
            "}"
            "QPushButton:hover { background-color: %4; }"
        ).arg(ColorAccent)
         .arg(RadiusMD)
         .arg(FontSizeBody)
         .arg(ColorAccentHover));
        connect(cta, &QPushButton::clicked, this, &EmptyStateWidget::ctaClicked);
        layout->addWidget(cta, 0, Qt::AlignHCenter);
    }
}

} // namespace bookhub::gui
