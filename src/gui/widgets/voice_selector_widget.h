#pragma once

#include <QWidget>
#include <QString>
#include <QList>

class QListWidget;
class QPushButton;

namespace bookhub::gui {

struct VoiceEntry {
    int id;
    QString name;
    bool isPreset;
};

class VoiceSelectorWidget : public QWidget {
    Q_OBJECT

public:
    explicit VoiceSelectorWidget(QWidget *parent = nullptr);

    void setVoices(const QList<VoiceEntry> &voices);
    QString selectedVoiceName() const;
    int selectedVoiceId() const;

signals:
    void voiceSelected(int voiceId, const QString &voiceName);
    void voicePreviewRequested(int voiceId, const QString &voiceName);
    void uploadNewVoiceRequested();

private slots:
    void onVoiceItemClicked();
    void onPreviewButtonClicked();
    void onUploadClicked();

private:
    void buildUi();
    void populateVoiceList();
    void updatePresetSection();
    void updateCustomSection();

    QList<VoiceEntry> m_voices;
    int m_selectedVoiceId{-1};

    QListWidget *m_presetList{};
    QListWidget *m_customList{};
    QPushButton *m_uploadBtn{};
};

} // namespace bookhub::gui
