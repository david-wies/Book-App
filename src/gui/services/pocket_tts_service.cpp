#include "pocket_tts_service.h"

#include <QDir>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#ifdef BOOKHUB_HAVE_POCKET_TTS
// When PocketTTS.cpp is compiled in, include its header here.
// Actual synthesis implementation will be added once model packaging
// is available (Task 22) and Task 14 wires up the reference audio upload.
// #include <pocket_tts/pocket_tts.h>
#endif

namespace bookhub::gui {

namespace {

// The six language tags supported by PocketTTS.cpp for voice cloning.
// Stored as a QSet for O(1) lookup. Tags are lower-cased before lookup.
const QSet<QString> &supportedTags()
{
    static const QSet<QString> tags{
        QStringLiteral("en"),
        QStringLiteral("fr"),
        QStringLiteral("de"),
        QStringLiteral("it"),
        QStringLiteral("pt"),
        QStringLiteral("es"),
    };
    return tags;
}

} // namespace

PocketTTSService::PocketTTSService(const QString &language,
                                   const QString &referenceAudioPath,
                                   QObject *parent)
    : TTSService(parent), m_language(language.toLower()), m_referenceAudioPath(referenceAudioPath)
{}

PocketTTSService::~PocketTTSService()
{
    cancel();
}

bool PocketTTSService::languageSupported(const QString &language) noexcept
{
    return supportedTags().contains(language.toLower());
}

bool PocketTTSService::libraryAvailable() noexcept
{
#ifdef BOOKHUB_HAVE_POCKET_TTS
    return true;
#else
    return false;
#endif
}

bool PocketTTSService::modelsAvailable()
{
    const QDir dir(modelDir());
    return dir.exists() && !dir.entryList(QDir::Files).isEmpty();
}

QString PocketTTSService::modelDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/models/pocket-tts");
}

bool PocketTTSService::checkAvailabilityAndEmitFailure(int voiceId, bool isPreview)
{
    if (!languageSupported(m_language) || !modelsAvailable()) {
        if (isPreview) {
            QTimer::singleShot(
                0, this, [this, voiceId] { emit previewGenerated(voiceId, QByteArray{}); });
        } else {
            QTimer::singleShot(0, this, [this] { emit generationCompleted(false, {}); });
        }
        return false;
    }
    return true;
}

void PocketTTSService::generatePreview(int voiceId, const QString &voiceName, const QString &text)
{
    Q_UNUSED(voiceName)
    Q_UNUSED(text)

    if (!checkAvailabilityAndEmitFailure(voiceId, /*isPreview=*/true))
        return;

#ifdef BOOKHUB_HAVE_POCKET_TTS
    // TODO (Task 22 + Task 14): run PocketTTS inference with m_referenceAudioPath
    // as the conditioning signal and emit previewGenerated with the result WAV bytes.
    Q_UNUSED(voiceId)
#endif

    // Library not linked yet — emit empty data.
    QTimer::singleShot(0, this, [this, voiceId] { emit previewGenerated(voiceId, QByteArray{}); });
}

void PocketTTSService::generateAudiobook(int voiceId,
                                         const QString &voiceName,
                                         const QString &text,
                                         const QString &outputPath)
{
    Q_UNUSED(voiceName)
    Q_UNUSED(text)
    Q_UNUSED(outputPath)
    cancel();

    if (!checkAvailabilityAndEmitFailure(voiceId, /*isPreview=*/false))
        return;

#ifdef BOOKHUB_HAVE_POCKET_TTS
    // TODO (Task 22 + Task 14): run PocketTTS full-document synthesis.
    Q_UNUSED(voiceId)
#endif

    QTimer::singleShot(0, this, [this] { emit generationCompleted(false, {}); });
}

void PocketTTSService::cancel()
{
    if (m_generationTimer) {
        m_generationTimer->stop();
        m_generationTimer->deleteLater();
        m_generationTimer = nullptr;
    }
}

} // namespace bookhub::gui
