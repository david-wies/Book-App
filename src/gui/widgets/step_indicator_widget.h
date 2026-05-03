#pragma once

#include <QStringList>
#include <QWidget>

namespace bookhub::gui {

// Draws a row of numbered circles connected by a horizontal line.
// Completed steps show a green checkmark; the active step is accent-filled;
// future steps are muted outlines.
class StepIndicatorWidget : public QWidget {
    Q_OBJECT
public:
    explicit StepIndicatorWidget(int stepCount, QWidget *parent = nullptr);

    // 0-indexed. Values outside [0, stepCount-1] are clamped.
    void setCurrentStep(int step);
    int  currentStep() const { return m_currentStep; }

    void setStepCount(int count);
    int  stepCount() const { return m_stepCount; }

    void setStepLabels(const QStringList &labels);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int         m_stepCount{1};
    int         m_currentStep{0};
    QStringList m_labels;

    static constexpr int kCircleDiameter = 20;
    static constexpr int kLabelHeight    = 16;
    static constexpr int kVertPadding    = 6;
    static constexpr int kStepWidth      = 60;
};

} // namespace bookhub::gui
