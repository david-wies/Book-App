#include <QtTest>

#include "gui/widgets/step_indicator_widget.h"

using bookhub::gui::StepIndicatorWidget;

class StepIndicatorWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void initialState_currentStepIsZero();
    void setCurrentStep_clampsBelowZero();
    void setCurrentStep_clampsAboveMax();
    void setCurrentStep_storesValue();
    void setStepCount_updatesMax();
    void setStepCount_clampsCurrentStep();
    void sizeHint_widthScalesWithStepCount();
    void sizeHint_heightIncludesLabelRow();
    void setStepLabels_doesNotCrash();
    void setStepLabels_fewerLabelsThanSteps_doesNotCrash();
};

void StepIndicatorWidgetTest::initialState_currentStepIsZero()
{
    StepIndicatorWidget w(3);
    QCOMPARE(w.currentStep(), 0);
    QCOMPARE(w.stepCount(), 3);
}

void StepIndicatorWidgetTest::setCurrentStep_clampsBelowZero()
{
    StepIndicatorWidget w(3);
    w.setCurrentStep(-5);
    QCOMPARE(w.currentStep(), 0);
}

void StepIndicatorWidgetTest::setCurrentStep_clampsAboveMax()
{
    StepIndicatorWidget w(3);
    w.setCurrentStep(99);
    QCOMPARE(w.currentStep(), 2);
}

void StepIndicatorWidgetTest::setCurrentStep_storesValue()
{
    StepIndicatorWidget w(3);
    w.setCurrentStep(1);
    QCOMPARE(w.currentStep(), 1);
    w.setCurrentStep(2);
    QCOMPARE(w.currentStep(), 2);
}

void StepIndicatorWidgetTest::setStepCount_updatesMax()
{
    StepIndicatorWidget w(3);
    w.setStepCount(5);
    QCOMPARE(w.stepCount(), 5);
    w.setCurrentStep(4);
    QCOMPARE(w.currentStep(), 4);
}

void StepIndicatorWidgetTest::setStepCount_clampsCurrentStep()
{
    StepIndicatorWidget w(5);
    w.setCurrentStep(4);
    QCOMPARE(w.currentStep(), 4);
    w.setStepCount(3);
    QCOMPARE(w.currentStep(), 2); // clamped to new max
}

void StepIndicatorWidgetTest::sizeHint_widthScalesWithStepCount()
{
    StepIndicatorWidget w2(2);
    StepIndicatorWidget w5(5);
    QVERIFY(w5.sizeHint().width() > w2.sizeHint().width());
}

void StepIndicatorWidgetTest::sizeHint_heightIncludesLabelRow()
{
    StepIndicatorWidget w(3);
    const int heightWithout = w.sizeHint().height();
    w.setStepLabels({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    QVERIFY(w.sizeHint().height() > heightWithout);
    QVERIFY(w.minimumHeight() > heightWithout);
}

void StepIndicatorWidgetTest::setStepLabels_doesNotCrash()
{
    StepIndicatorWidget w(3);
    w.setStepLabels({QStringLiteral("Language"),
                     QStringLiteral("Format"),
                     QStringLiteral("Source")});
    // Verify painting does not crash by forcing a paint event.
    w.resize(300, 48);
    w.show();
    w.hide();
}

void StepIndicatorWidgetTest::setStepLabels_fewerLabelsThanSteps_doesNotCrash()
{
    StepIndicatorWidget w(4);
    // Only 2 labels for 4 steps — the label guard must prevent out-of-bounds draw.
    w.setStepLabels({QStringLiteral("Language"), QStringLiteral("Format")});
    w.resize(400, 48);
    w.show();
    w.hide();
}

QTEST_MAIN(StepIndicatorWidgetTest)
#include "test_step_indicator_widget.moc"
