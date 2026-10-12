#include "whisper.h"
#include "logger.h"

namespace arena {

bool Transcriber::isAvailable()
{
#ifdef ARENA_ENABLE_WHISPER
    return true;
#else
    return false;
#endif
}

QList<WhisperModelInfo> Transcriber::models()
{
    return {
        {QStringLiteral("tiny"), QStringLiteral("tiny (fastest)"), 75 * 1024 * 1024},
        {QStringLiteral("base"), QStringLiteral("base"), 142 * 1024 * 1024},
        {QStringLiteral("small"), QStringLiteral("small (most accurate)"), 466 * 1024 * 1024},
    };
}

bool Transcriber::transcribe(const QString &audioPath, const Options &,
                             QList<TranscriptSegment> &out, QString *error,
                             std::function<void(float)> onProgress)
{
    Q_UNUSED(audioPath)
    Q_UNUSED(onProgress)
    out.clear();
    if (!isAvailable()) {
        if (error)
            *error = QStringLiteral("whisper.cpp is not compiled into this build (enable ARENA_ENABLE_WHISPER in M7)");
        return false;
    }
#ifdef ARENA_ENABLE_WHISPER
    // M7: load on-demand model, run decode on a worker thread, emit progress.
    logWarn(QStringLiteral("whisper.cpp integration pending (M7)"));
    return false;
#else
    return false;
#endif
}

} // namespace arena
