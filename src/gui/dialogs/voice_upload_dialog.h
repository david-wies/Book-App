#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class QLabel;

namespace bookhub::gui {

class VoiceUploadDialog : public QDialog {
    Q_OBJECT

public:
    explicit VoiceUploadDialog(QWidget *parent = nullptr);

    QString selectedFilePath() const;
    QString voiceName() const;

private slots:
    void onChooseFileClicked();
    void onValidateClicked();
    void onFileSelected(const QString &filePath);
    void onVoiceNameChanged();

private:
    void buildUi();
    void validateFile(const QString &filePath);
    void updateValidateButton();

    QString m_selectedFile;
    bool m_isFileValid{false};
    bool m_isDurationValid{false};
    bool m_isFormatValid{false};

    QLineEdit *m_filePathDisplay{};
    QLineEdit *m_voiceNameEdit{};
    QPushButton *m_chooseFileBtn{};
    QPushButton *m_validateBtn{};
    QLabel *m_durationLabel{};
    QLabel *m_formatLabel{};
    QLabel *m_qualityLabel{};
};

} // namespace bookhub::gui
