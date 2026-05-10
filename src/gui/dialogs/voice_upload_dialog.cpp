#include "voice_upload_dialog.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <limits>

namespace bookhub::gui {

namespace {

int wavDurationMs(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return -1;

    // NOTE: Reads a standard 44-byte PCM WAV header. Files that contain JUNK or
    // LIST chunks between 'fmt ' and 'data' shift the data-chunk offset beyond
    // byte 40, yielding an incorrect duration. Acceptable for MVP voice uploads
    // which are expected to be simple recorder output.
    const QByteArray header = file.read(44);
    if (header.size() < 44 || header.mid(0, 4) != "RIFF" || header.mid(8, 4) != "WAVE")
        return -1;

    auto readUInt16 = [&header](int offset) {
        return static_cast<quint16>(
            static_cast<quint32>(static_cast<uchar>(header[offset])) |
            (static_cast<quint32>(static_cast<uchar>(header[offset + 1])) << 8));
    };
    auto readUInt32 = [&header](int offset) {
        return static_cast<quint32>(static_cast<uchar>(header[offset])) |
               (static_cast<quint32>(static_cast<uchar>(header[offset + 1])) << 8) |
               (static_cast<quint32>(static_cast<uchar>(header[offset + 2])) << 16) |
               (static_cast<quint32>(static_cast<uchar>(header[offset + 3])) << 24);
    };

    const quint16 channels = readUInt16(22);
    const quint32 sampleRate = readUInt32(24);
    const quint16 bitsPerSample = readUInt16(34);
    const quint32 dataBytes = readUInt32(40);

    // Promote to quint64 before multiplying to prevent quint32 overflow for
    // high sample-rate / multi-channel files (e.g. 192 kHz stereo 24-bit).
    const quint64 bytesPerSecond = static_cast<quint64>(sampleRate) * channels * bitsPerSample / 8;
    if (bytesPerSecond == 0)
        return -1;

    const quint64 durationMs = (static_cast<quint64>(dataBytes) * 1000) / bytesPerSecond;
    if (durationMs > static_cast<quint64>(std::numeric_limits<int>::max()))
        return -1;
    return static_cast<int>(durationMs);
}

} // namespace

VoiceUploadDialog::VoiceUploadDialog(QWidget *parent) : QDialog(parent)
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
    m_voiceNameEdit->setObjectName(QStringLiteral("voiceNameEdit"));
    m_voiceNameEdit->setPlaceholderText("e.g., My Voice");
    connect(m_voiceNameEdit, &QLineEdit::textChanged, this, &VoiceUploadDialog::onVoiceNameChanged);
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
        this, "Select Voice Sample", QString(), "Audio files (*.wav *.mp3 *.flac);;All files (*)");

    if (!fileName.isEmpty()) {
        onFileSelected(fileName);
    }
}

void VoiceUploadDialog::onValidateClicked()
{
    // Button is only enabled when file is valid and name is non-empty (updateValidateButton).
    accept();
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
    m_validateBtn->setEnabled(!m_selectedFile.isEmpty() && m_isFileValid &&
                              !m_voiceNameEdit->text().isEmpty());
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

    if (suffix == QLatin1String("wav")) {
        const int durationMs = wavDurationMs(filePath);
        m_isDurationValid = durationMs >= 10'000 && durationMs <= 120'000;
        if (m_isDurationValid) {
            m_durationLabel->setText(
                QStringLiteral("✓ Duration: %1 seconds (OK)").arg(durationMs / 1000));
            m_durationLabel->setStyleSheet("color: #16A34A; font-size: 11px; font-weight: bold;");
        } else {
            m_durationLabel->setText(QStringLiteral("✗ Duration: must be 10–120 seconds"));
            m_durationLabel->setStyleSheet("color: #DC2626; font-size: 11px; font-weight: bold;");
        }
    } else {
        m_isDurationValid = m_isFormatValid;
        m_durationLabel->setText(QStringLiteral("○ Duration: checked after import"));
        m_durationLabel->setStyleSheet("color: #6B7280; font-size: 11px;");
    }

    // Quality: placeholder for MVP
    m_qualityLabel->setText("○ Quality: placeholder for Phase 4");
    m_qualityLabel->setStyleSheet("color: #6B7280; font-size: 11px;");

    // Update file validity flag
    m_isFileValid = m_isFormatValid && m_isDurationValid;
}

} // namespace bookhub::gui
