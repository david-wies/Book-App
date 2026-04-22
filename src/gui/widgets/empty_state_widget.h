#pragma once

#include <QWidget>
#include <QIcon>
#include <QString>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// EmptyStateWidget — reusable centred placeholder shown when a list has no
// items (spec Section 8.4). Contains an icon, headline, body text, and an
// optional CTA button.
// ---------------------------------------------------------------------------

class EmptyStateWidget : public QWidget {
    Q_OBJECT
public:
    explicit EmptyStateWidget(const QIcon &icon,
                              const QString &headline,
                              const QString &body,
                              const QString &ctaLabel = {},
                              QWidget *parent = nullptr);

signals:
    // Emitted when the CTA button is clicked (only connected when ctaLabel is non-empty).
    void ctaClicked();
};

} // namespace bookhub::gui
