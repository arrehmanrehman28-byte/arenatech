#pragma once
// Local speech-to-text via whisper.cpp. Transcription NEVER goes through
// the chat gateway: only the resulting text is handed to AI features.
#include <QString>
#include <QList>
#include <functional>

namespace arena {

struct WordSpan {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
};

struct TranscriptSegment {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    QList<WordSpan> words; // word-level timestamps (whisper.cpp)
};

struct WhisperModelInfo {
    QString id;      // "tiny" | "base" | "small"
    QString label;   // display name (translated at the UI layer)
    qint64 approxBytes = 0;
};

class Transcriber {
public:
    static bool isAvailable(); // false until ARENA_ENABLE_WHISPER (M7)
    static QList<WhisperModelInfo> models();

    struct Options {
        QString model = QStringLiteral("base");
        QString language; // empty = auto-detect
        bool wordTimestamps = true;
    };

    // Blocking call intended for worker threads. Returns false with *error
    // set when the engine is not compiled in or the decode fails.
    static bool transcribe(const QString &audioPath, const Options &opts,
                           QList<TranscriptSegment> &out, QString *error,
                           std::function<void(float progress)> onProgress = nullptr);
};

} // namespace arena
