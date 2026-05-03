#include "step_indicator_widget.h"

#include "../style_tokens.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

namespace bookhub::gui {

StepIndicatorWidget::StepIndicatorWidget(int stepCount, QWidget *parent)
    : QWidget(parent)
    , m_stepCount(qMax(1, stepCount))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Compute directly to avoid virtual dispatch in constructor.
    setMinimumHeight(2 * kVertPadding + kCircleDiameter);
}

void StepIndicatorWidget::setCurrentStep(int step)
{
    m_currentStep = qBound(0, step, m_stepCount - 1);
    update();
}

void StepIndicatorWidget::setStepCount(int count)
{
    m_stepCount = qMax(1, count);
    m_currentStep = qBound(0, m_currentStep, m_stepCount - 1);
    setMinimumHeight(sizeHint().height());
    updateGeometry();
    update();
}

void StepIndicatorWidget::setStepLabels(const QStringList &labels)
{
    m_labels = labels;
    update();
}

QSize StepIndicatorWidget::sizeHint() const
{
    const bool hasLabels = !m_labels.isEmpty();
    const int h = kVertPadding + kCircleDiameter + kVertPadding
                + (hasLabels ? kLabelHeight : 0);
    return {m_stepCount * 60, h};
}

QSize StepIndicatorWidget::minimumSizeHint() const
{
    return sizeHint();
}

void StepIndicatorWidget::paintEvent(QPaintEvent *)
{
    if (m_stepCount <= 0)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor accent(ColorAccent);
    const QColor muted(ColorTextMuted);
    const QColor success(ColorSuccess);
    const QColor surfaceColor(ColorSurface);

    const bool hasLabels = !m_labels.isEmpty();
    const int circleY = kVertPadding + kCircleDiameter / 2;

    // Distribute circles evenly across the full width.
    auto circleX = [&](int i) -> int {
        if (m_stepCount == 1)
            return width() / 2;
        return (i * (width() - kCircleDiameter)) / (m_stepCount - 1) + kCircleDiameter / 2;
    };

    // Draw connector lines first (behind circles).
    const int lineY = circleY;
    const int halfD = kCircleDiameter / 2;
    for (int i = 0; i < m_stepCount - 1; ++i) {
        const int x1 = circleX(i) + halfD;
        const int x2 = circleX(i + 1) - halfD;
        const bool completed = (i < m_currentStep);
        p.setPen(QPen(completed ? accent : muted, 2));
        p.drawLine(x1, lineY, x2, lineY);
    }

    const int labelY = kVertPadding + kCircleDiameter + 2;

    // Draw circles.
    QFont checkFont = font();
    checkFont.setPointSize(qMax(7, kCircleDiameter / 2 - 1));
    checkFont.setBold(true);

    for (int i = 0; i < m_stepCount; ++i) {
        const int cx = circleX(i);
        const int cy = circleY;
        const QRect circleRect(cx - halfD, cy - halfD, kCircleDiameter, kCircleDiameter);

        const bool completed = (i < m_currentStep);
        const bool active    = (i == m_currentStep);

        if (completed) {
            p.setBrush(success);
            p.setPen(Qt::NoPen);
            p.drawEllipse(circleRect);

            p.setFont(checkFont);
            p.setPen(surfaceColor);
            p.drawText(circleRect, Qt::AlignCenter, QStringLiteral("✓"));
        } else if (active) {
            p.setBrush(accent);
            p.setPen(Qt::NoPen);
            p.drawEllipse(circleRect);

            // Step number (1-based) in white.
            p.setFont(checkFont);
            p.setPen(surfaceColor);
            p.drawText(circleRect, Qt::AlignCenter, QString::number(i + 1));
        } else {
            p.setBrush(surfaceColor);
            p.setPen(QPen(muted, 2));
            p.drawEllipse(circleRect);

            p.setFont(checkFont);
            p.setPen(muted);
            p.drawText(circleRect, Qt::AlignCenter, QString::number(i + 1));
        }

        // Optional label below circle.
        if (hasLabels && i < m_labels.size()) {
            const QRect labelRect(cx - 40, labelY, 80, kLabelHeight);

            QFont labelFont = font();
            labelFont.setPointSize(qMax(7, FontSizeMeta - 1));
            p.setFont(labelFont);
            p.setPen(active ? accent : muted);
            p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop,
                       m_labels.at(i));
        }
    }
}

} // namespace bookhub::gui
