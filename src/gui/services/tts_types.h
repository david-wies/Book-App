#pragma once

#include <QString>
#include <QList>

namespace bookhub::gui {

struct VoiceEntry {
    int id;
    QString name;
    bool isPreset;
};

// ---------------------------------------------------------------------------
// Internal free functions for voice and audiobook-status queries.
// Used by QueryWorker on the query thread and TestQueryWorker in tests.
// ---------------------------------------------------------------------------
namespace internal {
    QList<VoiceEntry> listVoices(const QString &connectionName);
    bool insertVoice(const QString &voiceName, const QString &voiceType, bool isPreset,
                     int *outNewId, const QString &connectionName);
    bool updateVoice(int voiceId, const QString &voiceType, const QString &connectionName);
    bool deleteVoice(int voiceId, const QString &connectionName);
    bool queryAudiobookStatus(const QString &bookId, bool &outIsReady,
                               const QString &connectionName);
    bool setAudiobookReady(const QString &bookId, const QString &connectionName);
} // namespace internal

} // namespace bookhub::gui
