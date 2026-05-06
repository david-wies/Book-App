#include "voice_upload_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QDateTime>

namespace bookhub::gui {

VoiceUploadDialog::VoiceUploadDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Upload Voice Sample");
    setModal(true);
    setMinimumWidth(400);
    buildUi();
}

QString VoiceUploadDialog::selectedFilePath() const
{
    return m_selectedFile;
}

QString VoiceUploadDialog::voiceName() const
{
    return m_voiceNameEdit->text();
}

void VoiceUploadDialog::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // File selection
    QLabel *fileLabel = new QLabel("Select a recording of your voice:", this);
    mainLayout->addWidget(fileLabel);

    QHBoxLayout *fileLayout = new QHBoxLayout();
    m_chooseFileBtn = new QPushButton("Choose file", this);
    connect(m_chooseFileBtn, &QPushButton::clicked, this, &VoiceUploadDialog::onChooseFileClicked);
    fileLayout->addWidget(m_chooseFileBtn);

    m_filePathDisplay = new QLineEdit(this);
    m_filePathDisplay->setReadOnly(true);
    m_filePathDisplay->setPlaceholderText("no file selected");
    fileLayout->addWidget(m_filePathDisplay);
    mainLayout->addLayout(fileLayout);

    QLabel *supportedLabel = new QLabel("Supported: .wav, .mp3, .flac", this);
    supportedLabel->setStyleSheet("color: #6B7280; font-size: 11px;");
    mainLayout->addWidget(supportedLabel);

    QLabel *durationHint = new QLabel("Duration: 10–120 seconds", this);
    durationHint->setStyleSheet("color: #6B7280; font-size: 11px;");
    mainLayout->addWidget(durationHint);

    // Voice name
    mainLayout->addSpacing(12);
    QLabel *nameLabel = new QLabel("Voice name:", this);
    mainLayout->addWidget(nameLabel);

    m_voiceNameEdit = new QLineEdit(this);
    m_voiceNameEdit->setPlaceholderText("e.g., My Voice");
    connect(m_voiceNameEdit, &QLineEdit::textChanged,
            this, &VoiceUploadDialog::onVoiceNameChanged);
    mainLayout->addWidget(m_voiceNameEdit);

    // Validate button
    m_validateBtn = new QPushButton("Validate & Upload", this);
    m_validateBtn->setEnabled(false);
    connect(m_validateBtn, &QPushButton::clicked, this, &VoiceUploadDialog::onValidateClicked);
    mainLayout->addWidget(m_validateBtn);

    // Validation feedback
    mainLayout->addSpacing(12);
    m_durationLabel = new QLabel("○ Duration: checking…", this);
    m_durationLabel->setStyleSheet("color: #6B7280; font-size: 11px;");
    mainLayout->addWidget(m_durationLabel);

    m_formatLabel = new QLabel("○ Format: checking…", this);
    m_formatLabel->setStyleSheet("color: #6B7280; font-size: 11px;");
    mainLayout->addWidget(m_formatLabel);

    m_qualityLabel = new QLabel("○ Quality: checking…", this);
    m_qualityLabel->setStyleSheet("color: #6B7280; font-size: 11px;");
    mainLayout->addWidget(m_qualityLabel);

    mainLayout->addStretch();
}

void VoiceUploadDialog::onChooseFileClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Select Voice Sample",
        QString(),
        "Audio files (*.wav *.mp3 *.flac);;All files (*)"
    );

    if (!fileName.isEmpty()) {
        onFileSelected(fileName);
    }
}

void VoiceUploadDialog::onValidateClicked()
{
    if (!m_selectedFile.isEmpty() && !m_voiceNameEdit->text().isEmpty() && m_isFileValid) {
        accept();
    } else {
        QString error;
        if (m_selectedFile.isEmpty()) {
            error = "Please select a file";
        } else if (m_voiceNameEdit->text().isEmpty()) {
            error = "Please enter a voice name";
        } else if (!m_isFormatValid) {
            error = "File format is not supported (use .wav, .mp3, or .flac)";
        } else if (!m_isDurationValid) {
            error = "Audio duration must be between 10 and 120 seconds";
        }
        if (!error.isEmpty()) {
            QMessageBox::warning(this, "Validation Failed", error);
        }
    }
}

void VoiceUploadDialog::onFileSelected(const QString &filePath)
{
    m_selectedFile = filePath;
    m_isFileValid = false;
    QFileInfo fileInfo(filePath);
    m_filePathDisplay->setText(fileInfo.fileName());
    validateFile(filePath);
    updateValidateButton();
}

void VoiceUploadDialog::onVoiceNameChanged()
{
    updateValidateButton();
}

void VoiceUploadDialog::updateValidateButton()
{
    m_validateBtn->setEnabled(!m_selectedFile.isEmpty() && m_isFileValid
                              && !m_voiceNameEdit->text().isEmpty());
}

void VoiceUploadDialog::validateFile(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    QString suffix = fileInfo.suffix().toLower();

    // Format validation
    m_isFormatValid = (suffix == "wav" || suffix == "mp3" || suffix == "flac");
    if (m_isFormatValid) {
        m_formatLabel->setText("✓ Format: " + suffix.toUpper() + " (OK)");
        m_formatLabel->setStyleSheet("color: #16A34A; font-size: 11px; font-weight: bold;");
    } else {
        m_formatLabel->setText("✗ Format: invalid");
        m_formatLabel->setStyleSheet("color: #DC2626; font-size: 11px; font-weight: bold;");
    }

    // Duration validation (MVP: not implemented — deferred to Phase 3)
    m_isDurationValid = true;
    m_durationLabel->setText("○ Duration: not checked in MVP");
    m_durationLabel->setStyleSheet("color: #6B7280; font-size: 11px;");

    // Quality: placeholder for MVP
    m_qualityLabel->setText("○ Quality: placeholder for Phase 4");
    m_qualityLabel->setStyleSheet("color: #6B7280; font-size: 11px;");
    
    // Update file validity flag
    m_isFileValid = m_isFormatValid && m_isDurationValid;
}

} // namespace bookhub::gui
