// M4 export: pure logic (presets, estimates, encoder selection) and the
// honest availability boundary. The blocking encode pipeline lives in
// ffmpeg_export.cpp (compiled only with FFmpeg).
#include "exporter.h"

#include <QtMath>

#include <algorithm>

namespace arena {

bool Exporter::isAvailable()
{
#if ARENA_HAS_FFMPEG
    return true;
#else
    return false;
#endif
}

// --- Encoder chains -----------------------------------------------------------
// Hardware candidates first (nvenc = NVIDIA, qsv = Intel, amf = AMD), then
// the software fallbacks. A candidate that is not in the build simply never
// appears in the availability list, so selection degrades gracefully.

QStringList Exporter::videoEncoderChain(const QString &requested)
{
    if (requested == QStringLiteral("h264"))
        return {QStringLiteral("h264_nvenc"), QStringLiteral("h264_qsv"),
                QStringLiteral("h264_amf"), QStringLiteral("libx264")};
    if (requested == QStringLiteral("hevc"))
        return {QStringLiteral("hevc_nvenc"), QStringLiteral("hevc_qsv"),
                QStringLiteral("hevc_amf"), QStringLiteral("libx265")};
    if (requested == QStringLiteral("av1"))
        return {QStringLiteral("av1_nvenc"), QStringLiteral("av1_amf"),
                QStringLiteral("libsvtav1"), QStringLiteral("libaom"),
                QStringLiteral("librav1e")};
    if (requested == QStringLiteral("vp9"))
        return {QStringLiteral("libvpx-vp9")};
    return {};
}

QStringList Exporter::audioEncoderChain(const QString &requested)
{
    if (requested == QStringLiteral("aac"))
        return {QStringLiteral("aac")};
    if (requested == QStringLiteral("opus"))
        return {QStringLiteral("libopus")};
    return {};
}

QString Exporter::selectVideoEncoder(const QString &requested,
                                     const QStringList &available, bool hwEnabled)
{
    const static QStringList kHw = {QStringLiteral("h264_nvenc"),
                                    QStringLiteral("h264_qsv"),
                                    QStringLiteral("h264_amf"),
                                    QStringLiteral("hevc_nvenc"),
                                    QStringLiteral("hevc_qsv"),
                                    QStringLiteral("hevc_amf"),
                                    QStringLiteral("av1_nvenc"),
                                    QStringLiteral("av1_amf")};
    const QStringList chain = videoEncoderChain(requested);
    for (const QString &name : chain)
        if (hwEnabled || !kHw.contains(name))
            if (available.contains(name))
                return name;
    return {};
}

QString Exporter::selectAudioEncoder(const QString &requested,
                                     const QStringList &available)
{
    const QStringList chain = audioEncoderChain(requested);
    for (const QString &name : chain)
        if (available.contains(name))
            return name;
    return {};
}

// --- Presets --------------------------------------------------------------------

QStringList Exporter::presetNames()
{
    return {QStringLiteral("mp4-h264-high"),
            QStringLiteral("mp4-h264-balanced"),
            QStringLiteral("mp4-h264-web-720"),
            QStringLiteral("mp4-hevc-archive"),
            QStringLiteral("webm-vp9")};
}

ExportSettings Exporter::preset(const QString &name, const Project &p)
{
    ExportSettings s;
    if (name == QStringLiteral("mp4-h264-high")) {
        s = {QString(), QStringLiteral("mp4"), QStringLiteral("h264"),
             QStringLiteral("aac"), 18.0, 320000.0, 100, p.fps > 0 ? p.fps : 30.0, true};
    } else if (name == QStringLiteral("mp4-h264-balanced")) {
        s = {QString(), QStringLiteral("mp4"), QStringLiteral("h264"),
             QStringLiteral("aac"), 23.0, 192000.0, 100, p.fps > 0 ? p.fps : 30.0, true};
    } else if (name == QStringLiteral("mp4-h264-web-720")) {
        const int pct = p.height > 720 ? int(720.0 * 100.0 / p.height) : 100;
        s = {QString(), QStringLiteral("mp4"), QStringLiteral("h264"),
             QStringLiteral("aac"), 26.0, 160000.0, pct, p.fps > 0 ? p.fps : 30.0, true};
    } else if (name == QStringLiteral("mp4-hevc-archive")) {
        s = {QString(), QStringLiteral("mp4"), QStringLiteral("hevc"),
             QStringLiteral("aac"), 24.0, 192000.0, 100, p.fps > 0 ? p.fps : 30.0, true};
    } else if (name == QStringLiteral("webm-vp9")) {
        s = {QString(), QStringLiteral("webm"), QStringLiteral("vp9"),
             QStringLiteral("opus"), 32.0, 160000.0, 100, p.fps > 0 ? p.fps : 30.0, true};
    } else {
        s = {QString(), QStringLiteral("mp4"), QStringLiteral("h264"),
             QStringLiteral("aac"), 23.0, 192000.0, 100, p.fps > 0 ? p.fps : 30.0, true};
    }
    return s;
}

// --- Estimates ---------------------------------------------------------------------

// Documented heuristic (UI label says "approx"): ~1% of (w*h*frames) bits at
// crf 0, decaying 12% per crf stop; audio = bitrate * duration.
ExportEstimate Exporter::estimate(const ExportSettings &s, const Project &p)
{
    ExportEstimate e;
    if (p.fps <= 0.0) return e;
    qint64 totalFrames = 0;
    for (const Track &t : p.tracks)
        if (t.type == TrackType::Video)
            for (const Clip &c : t.clips)
                totalFrames = qMax<qint64>(totalFrames, c.offset + c.duration());
    if (totalFrames <= 0) return e;

    const double fps = s.fps > 0 ? s.fps : p.fps;
    const int w = qMax(2, (p.width * qBound(10, s.scalePercent, 400) / 100) & ~1);
    const int h = qMax(2, (p.height * qBound(10, s.scalePercent, 400) / 100) & ~1);
    const double seconds = double(totalFrames) / fps;
    const double quality =
        std::clamp(1.0 - std::clamp(s.crf, 0.0, 51.0) / 32.0 * 0.88, 0.04, 1.0);
    const double bits = double(w) * h * double(totalFrames) * 0.08 * quality;
    e.videoBytes = qint64(bits / 8.0);
    e.audioBytes = s.audioCodec.isEmpty()
                       ? 0
                       : qint64(std::clamp(s.audioBitrate, 32000.0, 1000000.0) * seconds / 8.0);

    // Encode-speed heuristic (frames/s) per requested codec family.
    double speed = 90.0;
    if (s.videoCodec == QStringLiteral("hevc")) speed = 25.0;
    else if (s.videoCodec == QStringLiteral("av1")) speed = 12.0;
    else if (s.videoCodec == QStringLiteral("vp9")) speed = 30.0;
    e.estEncodeSeconds = double(totalFrames) / speed;
    return e;
}

#if !ARENA_HAS_FFMPEG
// Honest boundary for non-FFmpeg builds.
ExportJobResult Exporter::exportProject(const Project &, const ExportSettings &,
                                        const Compositor::FrameFetcher &,
                                        const AudioFetcher &, const Progress &,
                                        const CancelToken &)
{
    ExportJobResult r;
    r.error = QStringLiteral("export requires the FFmpeg build");
    return r;
}
#endif

} // namespace arena
