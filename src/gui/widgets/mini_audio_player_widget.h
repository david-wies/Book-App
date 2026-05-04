#pragma once

#include <QWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>

namespace bookhub::gui {

class MiniAudioPlayerWidget : public QWidget {
    Q_OBJECT

public:
    explicit MiniAudioPlayerWidget(QWidget *parent = nullptr);

    void setDuration(qint64 durationMs);
    void setCurrentTime(qint64 currentTimeMs);
    bool isPlaying() const;

signals:
    void playClicked();
    void pauseClicked();
    void seekRequested(qint64 positionMs);

private slots:
    void onPlayPauseClicked();
    void onSliderMoved(int position);
    void onSliderPressed();
    void onSliderReleased();

private:
    void buildUi();
    void updateTimeLabel();

    bool m_isPlaying{false};
    qint64 m_duration{0};
    qint64 m_currentTime{0};
    bool m_sliderPressed{false};

    QPushButton *m_playPauseBtn{};
    QSlider *m_scrubber{};
    QLabel *m_timeLabel{};
};

} // namespace bookhub::gui
