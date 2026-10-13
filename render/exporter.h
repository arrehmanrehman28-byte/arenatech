#pragma once
// M4 export boundary. Renders the project through the deterministic M3
// compositor and encodes it to a container (MP4/WebM) with a chosen
// encoder (hardware candidates first, software fallback). `exportProject`
// is synchronous and blocking — the app runs it on worker threads via its
// export queue so the UI never freezes (spec).
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "compositor.h"

namespace arena {

struct ExportSettings {
    QString outputPath;
    QString container = QStringLiteral("mp4");  // "mp4" | "webm"
    QString videoCodec = QStringLiteral("h264"); // "h264" | "hevc" | "av1" | "vp9"
    QString audioCodec;                        // "" = no audio; "aac" | "opus"
    double crf = 23.0;                          // quality: lower = better/bigger
    double audioBitrate = 192000.0;             // bits/s
    int scalePercent = 100;                     // output size vs project
    double fps = 0.0;                           // 0 = project fps
    bool hwEncode = true;                       // allow nvenc/qsv/amf candidates
};

// Heuristic size/time estimate for the UI (documented approximation, not a
// promise): video bytes scale with resolution * fps * duration * quality.
struct ExportEstimate {
    qint64 videoBytes = 0;
    qint64 audioBytes = 0;
    double estEncodeSeconds = 0.0;
    qint64 totalBytes() const { return videoBytes + audioBytes; }
};

struct ExportJobResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    qint64 framesWritten = 0;
    double durationSec = 0.0;
    qint64 bytesWritten = 0;
    QString encoder; // the encoder actually used (UI + tests)
};

struct Exporter {
public:
    using Progress = std::function<void(qint64 done, qint64 total)>;
    using CancelToken = std::function<bool()>;
    // Clip audio in [inFrame, outFrame) (project fps frames; the implementer
    // maps frames to source seconds), interleaved float32 in [-1, 1].
    // Returns true when the source has audio (false = silent clip).
    using AudioFetcher = std::function<bool(const QString &source, qint64 inFrame,
                                            qint64 outFrame, int sampleRate, int channels,
                                            QVector<float> *out)>;

    // True when the FFmpeg-backed encoder pipeline is compiled in.
    static bool isAvailable();

    // --- Presets (pure) ------------------------------------------------------
    static QStringList presetNames();
    static ExportSettings preset(const QString &name, const Project &p);
    static ExportEstimate estimate(const ExportSettings &s, const Project &p);

    // --- Encoder selection (pure; injectable for tests) -----------------------
    // Candidate chains, hardware candidates first (ordered by preference).
    static QStringList videoEncoderChain(const QString &requested);
    static QStringList audioEncoderChain(const QString &requested);
    // First candidate in `available` (HW candidates only when hwEnabled).
    // Empty string = nothing usable.
    static QString selectVideoEncoder(const QString &requested,
                                      const QStringList &available,
                                      bool hwEnabled = true);
    static QString selectAudioEncoder(const QString &requested,
                                      const QStringList &available);

    // --- Export (blocking; worker thread) --------------------------------------
    static ExportJobResult exportProject(const Project &p, const ExportSettings &s,
                                         const Compositor::FrameFetcher &fetch,
                                         const AudioFetcher &audio,
                                         const Progress &progress = {},
                                         const CancelToken &cancel = {});
};

} // namespace arena
