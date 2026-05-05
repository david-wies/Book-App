#pragma once

#include <QString>
#include <QList>

namespace bookhub::gui {

// Lightweight DTO used by the UI layer. engine/path fields stay in the DB
// and are fetched by TTSService when audio generation starts (Task 13).
struct VoiceEntry {
    int id;
    QString name;
    bool isPreset; // derived from voices.type == 'preset'
};

// ---------------------------------------------------------------------------
// Internal free functions for voice and audiobook-status queries.
// Used by QueryWorker on the query thread and TestQueryWorker in tests.
// ---------------------------------------------------------------------------
namespace internal {
    QList<VoiceEntry> listVoices(const QString &connectionName);
    // type must be 'preset' or 'custom'; engine must be 'sherpa_onnx' or 'pocket_tts'.
    bool insertVoice(const QString &name, const QString &type, const QString &engine,
                     int *outNewId, const QString &connectionName);
    bool updateVoice(int voiceId, const QString &engine, const QString &connectionName);
    bool deleteVoice(int voiceId, const QString &connectionName);
    bool queryAudiobookStatus(const QString &bookId, bool &outIsReady,
                               const QString &connectionName);
    bool setAudiobookReady(const QString &bookId, const QString &connectionName);
} // namespace internal

} // namespace bookhub::gui
