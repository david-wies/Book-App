#pragma once

#include <QString>
#include <QList>

namespace bookhub::gui {

// Lightweight DTO used by the UI layer. engine/path fields stay in the DB
// and are fetched by TTSService when audio generation starts (Task 13).
struct VoiceEntry {
    int id{-1};
    QString name;
    bool isPreset; // derived from voices.type == 'preset'
};

} // namespace bookhub::gui
