#pragma once
// Media boundary: probe, decode, waveform, thumbnails, proxy generation.
// M1: real FFmpeg (LGPL, dynamic) pipeline behind ARENA_HAS_FFMPEG — demux +
// decode off the UI thread, software decode (hardware decode lands with M3
// render), 540p proxies for smooth scrubbing, waveforms on background threads.
#include <QImage>
#include <QString>
#include <QList>
#include <QVector>

namespace arena {

enum class MediaType { Unknown, Video, Audio, Image };

struct MediaInfo {
    QString path;
    bool exists = false;
    qulonglong sizeBytes = 0;
    MediaType type = MediaType::Unknown;
    // Filled by the FFmpeg probe (M1):
    int width = 0;
    int height = 0;
    double fps = 0.0;
    qint64 durationMs = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    QString videoCodec;   // e.g. "h264"
    QString audioCodec;   // e.g. "aac"
    int sampleRate = 0;
    int channels = 0;
    QString container;    // e.g. "mov,mp4,m4a,3gp,3g2,mj2"
    QString error;        // probe failure text (empty on success)
};

class MediaProbe {
public:
    // Cheap, file-level probe. No FFmpeg required.
    static MediaInfo probe(const QString &path);
    // Full FFmpeg probe (streams, codecs, duration). *err receives a
    // human-readable failure; returns false on failure. No-op returning
    // false when built without FFmpeg.
    static bool probeFull(const QString &path, MediaInfo *out, QString *err);
    static MediaType typeFromPath(const QString &path);
};

// --- Decode (M1) ------------------------------------------------------------

// Decodes video frames from a file. Not thread-safe; one instance per stream.
class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder &) = delete;
    VideoDecoder &operator=(const VideoDecoder &) = delete;

    bool open(const QString &path, QString *err = nullptr);
    void close();
    bool isOpen() const { return m_open; }
    double durationSec() const { return m_durationSec; }
    double fps() const { return m_fps; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Frame-accurate: seeks to the keyframe at/before `sec`, then decodes
    // forward to the first frame with pts >= `sec`.
    bool frameAtSec(double sec, QImage *out, double *ptsOut = nullptr);
    // Next video frame in stream order (from the current demux position).
    bool nextFrame(QImage *out, double *ptsOut = nullptr);

private:
    int readVideoPacket();
    bool convertFrame(QImage *out, double *ptsOut);

    bool m_open = false;
    void *m_fmt = nullptr;   // AVFormatContext*
    void *m_dec = nullptr;   // AVCodecContext*
    int m_stream = -1;
    double m_durationSec = 0.0;
    double m_fps = 0.0;
    int m_width = 0;
    int m_height = 0;
    void *m_pkt = nullptr;   // AVPacket*
    void *m_frame = nullptr; // AVFrame*
};

// Decodes audio as interleaved float32 (44.1/48k source; resample target can
// be set via setOutput). Not thread-safe.
class AudioReader {
public:
    AudioReader() = default;
    ~AudioReader();
    AudioReader(const AudioReader &) = delete;
    AudioReader &operator=(const AudioReader &) = delete;

    bool open(const QString &path, QString *err = nullptr);
    void close();
    bool isOpen() const { return m_open; }
    double durationSec() const { return m_durationSec; }
    int sourceRate() const { return m_sourceRate; }
    int sourceChannels() const { return m_sourceChannels; }

    // Output format for readInterleaved; defaults to the source format.
    void setOutput(int sampleRate, int channels);

    // Decodes up to `maxSamples` interleaved samples (per the output format)
    // into `buf`. Returns the number of interleaved samples written; 0 at EOF.
    int readInterleaved(float *buf, int maxSamples);
    bool seekSec(double sec);

private:
    int readAudioPacket();
    bool ensureResampler();

    bool m_open = false;
    void *m_fmt = nullptr;
    void *m_dec = nullptr;
    void *m_swr = nullptr;   // SwrContext*
    int m_stream = -1;
    double m_durationSec = 0.0;
    int m_sourceRate = 48000;
    int m_sourceChannels = 2;
    int m_outRate = 0;       // 0 = follow source
    int m_outChannels = 0;
    int m_lastInRate = 0;
    int m_lastInChannels = 0;
    int m_lastInFormat = 0;
    void *m_pkt = nullptr;
    void *m_frame = nullptr;
    bool m_eof = false;
};

// --- Generators (blocking; run on worker threads) ----------------------------

struct Peak {
    float lo = 0.0f;
    float hi = 0.0f;
};

class WaveformGenerator {
public:
    static bool isAvailable();
    // Peak pairs (lo/hi in [-1, 1]) over `buckets` time slices of the file.
    static bool generate(const QString &path, int buckets, QVector<Peak> *out,
                         QString *err);
};

class Thumbnailer {
public:
    static bool isAvailable();
    // `count` evenly spaced frames, scaled to `targetHeight`.
    static bool generate(const QString &path, int count, int targetHeight,
                         QList<QImage> *out, QString *err);
};

class ProxyGenerator {
public:
    static bool isAvailable();
    // Transcode to a small proxy for smooth scrubbing: mp4 container,
    // mpeg4 video (LGPL encoder) scaled to `targetHeight` (even dims, aspect
    // preserved), aac audio. *progress may receive 0..1 updates.
    static bool generate(const QString &source, const QString &out,
                         int targetHeight, QString *err,
                         std::function<void(double)> *progress = nullptr);
    // Per-user cache dir for proxies, created on demand.
    static QString defaultProxyDir();
};

} // namespace arena
