#pragma once

#include <QLabel>
#include <QString>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// BadgeLabel — a pill-shaped QLabel used for language and source tags.
// Colour scheme follows spec Section 8.2.
// ---------------------------------------------------------------------------

class BadgeLabel : public QLabel {
    Q_OBJECT
public:
    enum class Kind {
        Language,  // blue palette
        Source,    // green palette
        Genre,     // neutral grey palette
    };

    explicit BadgeLabel(const QString &text, Kind kind = Kind::Language,
                        QWidget *parent = nullptr);

private:
    void applyStyle(Kind kind);
};

} // namespace bookhub::gui
