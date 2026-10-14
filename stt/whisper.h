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
    QList<WordSpan> words; // word-level timestamps (whisper.cpp token data)
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

    // Where ggml-<id>.bin models live (per-user, on-demand download).
    static QString modelsDir();
    // True when the model file exists and matches the known size + SHA-256.
    static bool modelIsReady(const QString &id);
    // Download (or re-verify) the model; blocking, for worker threads.
    // onProgress(fraction 0..1, bytesDone, bytesTotal) fires while the file
    // streams to disk; total is 0 when the server did not advertise a size.
    static bool ensureModel(const QString &id, QString *error,
                            std::function<void(float, qint64, qint64)> onProgress = nullptr);

    struct Options {
        QString model = QStringLiteral("base");
        QString language; // empty = auto-detect
        bool wordTimestamps = true;
    };

    // Blocking call intended for worker threads. Accepts any audio the
    // media pipeline can decode (wav, mp4, ...). Returns false with *error
    // set when the engine is not compiled in, the model cannot be
    // downloaded/verified, or the decode fails. *detectedLanguage (optional)
    // receives the BCP-47/ISO code whisper settled on. onProgress reports
    // model-download progress (0..1); decoding itself has no C-API progress
    // hook, so the callback is invoked once with 1.0 on completion.
    static bool transcribe(const QString &audioPath, const Options &opts,
                           QList<TranscriptSegment> &out, QString *error,
                           std::function<void(float)> onProgress = nullptr,
                           QString *detectedLanguage = nullptr);
};

} // namespace arena
