#include "mini_audio_player_widget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>

namespace bookhub::gui {

MiniAudioPlayerWidget::MiniAudioPlayerWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void MiniAudioPlayerWidget::buildUi()
{
    m_playPauseBtn = new QPushButton(this);
    m_playPauseBtn->setText("▶");
    m_playPauseBtn->setMaximumWidth(40);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &MiniAudioPlayerWidget::onPlayPauseClicked);

    m_scrubber = new QSlider(Qt::Horizontal, this);
    m_scrubber->setRange(0, 0);
    m_scrubber->setFocusPolicy(Qt::StrongFocus);
    connect(m_scrubber, &QSlider::sliderMoved, this, &MiniAudioPlayerWidget::onSliderMoved);
    connect(m_scrubber, &QSlider::sliderPressed, this, &MiniAudioPlayerWidget::onSliderPressed);
    connect(m_scrubber, &QSlider::sliderReleased, this, &MiniAudioPlayerWidget::onSliderReleased);

    m_timeLabel = new QLabel("0:00 / 0:00", this);
    m_timeLabel->setMaximumWidth(80);
    m_timeLabel->setStyleSheet("QLabel { color: #6B7280; font-size: 11px; }");

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_playPauseBtn);
    layout->addWidget(m_scrubber);
    layout->addWidget(m_timeLabel);

    setMaximumHeight(32);
}

void MiniAudioPlayerWidget::setDuration(qint64 durationMs)
{
    m_duration = durationMs;
    if (durationMs > 0) {
        m_scrubber->setRange(0, static_cast<int>(durationMs / 100));
    }
    updateTimeLabel();
}

void MiniAudioPlayerWidget::setCurrentTime(qint64 currentTimeMs)
{
    m_currentTime = currentTimeMs;
    if (!m_sliderPressed && m_duration > 0) {
        m_scrubber->blockSignals(true);
        m_scrubber->setValue(static_cast<int>(currentTimeMs / 100));
        m_scrubber->blockSignals(false);
    }
    updateTimeLabel();
}

bool MiniAudioPlayerWidget::isPlaying() const
{
    return m_isPlaying;
}

void MiniAudioPlayerWidget::setPlaying(bool playing)
{
    m_isPlaying = playing;
    m_playPauseBtn->setText(playing ? "⏸" : "▶");
}

void MiniAudioPlayerWidget::onPlayPauseClicked()
{
    m_isPlaying = !m_isPlaying;
    m_playPauseBtn->setText(m_isPlaying ? "⏸" : "▶");
    if (m_isPlaying) {
        emit playClicked();
    } else {
        emit pauseClicked();
    }
}

void MiniAudioPlayerWidget::onSliderMoved(int position)
{
    m_currentTime = static_cast<qint64>(position) * 100;
    updateTimeLabel();
}

void MiniAudioPlayerWidget::onSliderPressed()
{
    m_sliderPressed = true;
}

void MiniAudioPlayerWidget::onSliderReleased()
{
    m_sliderPressed = false;
    m_currentTime = static_cast<qint64>(m_scrubber->value()) * 100;
    emit seekRequested(m_currentTime);
}

void MiniAudioPlayerWidget::updateTimeLabel()
{
    int currentSecs = static_cast<int>(m_currentTime / 1000);
    int totalSecs = static_cast<int>(m_duration / 1000);

    int curMins = currentSecs / 60;
    int curSecs = currentSecs % 60;
    int totMins = totalSecs / 60;
    int totSecs = totalSecs % 60;

    QString timeStr = QString("%1:%2 / %3:%4")
        .arg(curMins)
        .arg(curSecs, 2, 10, QLatin1Char('0'))
        .arg(totMins)
        .arg(totSecs, 2, 10, QLatin1Char('0'));

    m_timeLabel->setText(timeStr);
}

} // namespace bookhub::gui
