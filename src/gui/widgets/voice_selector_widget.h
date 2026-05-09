#pragma once

#include "../services/tts_types.h"
#include <QWidget>

class QListWidget;
class QPushButton;

namespace bookhub::gui {

class VoiceSelectorWidget : public QWidget {
    Q_OBJECT

public:
    explicit VoiceSelectorWidget(QWidget *parent = nullptr);

    void setVoices(const QList<VoiceEntry> &voices);
    QString selectedVoiceName() const;
    int selectedVoiceId() const;

signals:
    void voiceSelected(int voiceId, const QString &voiceName);
    void uploadNewVoiceRequested();

private slots:
    void onUploadClicked();

private:
    void buildUi();
    void populateVoiceList();

    QList<VoiceEntry> m_voices;
    int m_selectedVoiceId{-1};

    QListWidget *m_presetList{};
    QListWidget *m_customList{};
    QPushButton *m_uploadBtn{};
};

} // namespace bookhub::gui
