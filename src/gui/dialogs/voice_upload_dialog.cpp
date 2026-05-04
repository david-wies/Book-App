#include "voice_upload_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>

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

    m_fileLabel = new QLineEdit(this);
    m_fileLabel->setReadOnly(true);
    m_fileLabel->setPlaceholderText("no file selected");
    fileLayout->addWidget(m_fileLabel);
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
    if (!m_selectedFile.isEmpty() && !m_voiceNameEdit->text().isEmpty()) {
        accept();
    }
}

void VoiceUploadDialog::onFileSelected(const QString &filePath)
{
    m_selectedFile = filePath;
    QFileInfo fileInfo(filePath);
    m_fileLabel->setText(fileInfo.fileName());
    validateFile(filePath);
    m_validateBtn->setEnabled(!m_voiceNameEdit->text().isEmpty());
}

void VoiceUploadDialog::validateFile(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    QString suffix = fileInfo.suffix().toLower();

    // Format validation
    bool validFormat = (suffix == "wav" || suffix == "mp3" || suffix == "flac");
    if (validFormat) {
        m_formatLabel->setText("✓ Format: " + suffix.toUpper() + " (OK)");
        m_formatLabel->setStyleSheet("color: #16A34A; font-size: 11px; font-weight: bold;");
    } else {
        m_formatLabel->setText("✗ Format: invalid");
        m_formatLabel->setStyleSheet("color: #DC2626; font-size: 11px; font-weight: bold;");
    }

    // Duration validation (MVP: placeholder)
    int duration = getAudioFileDuration(filePath);
    if (duration >= 10 && duration <= 120) {
        m_durationLabel->setText(QString("✓ Duration: %1 s (OK)").arg(duration));
        m_durationLabel->setStyleSheet("color: #16A34A; font-size: 11px; font-weight: bold;");
    } else {
        m_durationLabel->setText(QString("✗ Duration: %1 s (must be 10–120)").arg(duration));
        m_durationLabel->setStyleSheet("color: #DC2626; font-size: 11px; font-weight: bold;");
    }

    // Quality: placeholder for MVP
    m_qualityLabel->setText("○ Quality: placeholder for Phase 4");
    m_qualityLabel->setStyleSheet("color: #6B7280; font-size: 11px;");
}

int VoiceUploadDialog::getAudioFileDuration(const QString &filePath) const
{
    // MVP placeholder: return a mock duration based on file size
    // In Phase 3+, use Qt Multimedia to actually read duration
    QFileInfo fileInfo(filePath);
    qint64 fileSize = fileInfo.size();
    // Rough estimate: ~200KB per second at standard bitrate
    return static_cast<int>((fileSize / 200000) + 1);
}

} // namespace bookhub::gui
