#include "pocket_tts_service.h"

#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <utility>

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
    static const QSet<QString> kTags{
        QStringLiteral("en"),
        QStringLiteral("fr"),
        QStringLiteral("de"),
        QStringLiteral("it"),
        QStringLiteral("pt"),
        QStringLiteral("es"),
    };
    return kTags;
}

} // namespace

PocketTTSService::PocketTTSService(const QString &language,
                                   QString referenceAudioPath,
                                   QObject *parent)
    : TTSService(parent),
      m_language(language.toLower()),
      m_referenceAudioPath(std::move(referenceAudioPath))
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
    // Check for any file without listing the whole directory (O(1) vs O(n)).
    QDirIterator it(modelDir(), QDir::Files | QDir::NoDotAndDotDot);
    return it.hasNext();
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
    // TODO (Task 22 + Task 14): run PocketTTS inference with m_referenceAudioPath as the
    // conditioning signal. Emit previewGenerated(voiceId, wavBytes) and return.
#ifdef BOOKHUB_HAVE_GPU
    // TODO (Task 22): append the matching execution provider before creating the session:
    //   CUDA    → AppendExecutionProvider_CUDA(OrtCUDAProviderOptions{})
    //   CoreML  → AppendExecutionProvider_CoreML(0)
    //   DML     → AppendExecutionProvider_DML(0)
    //   ROCm    → AppendExecutionProvider_ROCm(OrtROCMProviderOptions{})
    //   OpenVINO→ AppendExecutionProvider_OpenVINO(OrtOpenVINOProviderOptions{})
#endif
    Q_UNUSED(voiceId)
    // Prevent fall-through to the failure emit below once synthesis is wired in.
    return;
#endif
    // Library not linked or synthesis not yet implemented — emit empty data so the dialog
    // can show duration 0:00 and let the user retry once models are ready.
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
    // Emit generationProgress() updates and generationCompleted(true, outputPath). Return.
#ifdef BOOKHUB_HAVE_GPU
    // TODO (Task 22): append the matching execution provider before creating the session:
    //   CUDA    → AppendExecutionProvider_CUDA(OrtCUDAProviderOptions{})
    //   CoreML  → AppendExecutionProvider_CoreML(0)
    //   DML     → AppendExecutionProvider_DML(0)
    //   ROCm    → AppendExecutionProvider_ROCm(OrtROCMProviderOptions{})
    //   OpenVINO→ AppendExecutionProvider_OpenVINO(OrtOpenVINOProviderOptions{})
#endif
    Q_UNUSED(voiceId)
    // Prevent fall-through to the failure emit below once synthesis is wired in.
    return;
#endif
    // Library not linked or synthesis not yet implemented — emit failure.
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
