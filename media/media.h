#pragma once
// Media boundary: probe, proxy generation, waveform generation.
// Phase M0: file-level probing (existence, size, container type from the
// extension). M1 replaces the internals with the FFmpeg (LGPL, dynamic)
// pipeline: demux, decode on a worker pool (never the UI thread), D3D11VA /
// NVDEC / QSV / AMF hardware decode with software fallback, 540p proxies,
// and waveforms on background threads.
#include <QString>
#include <QList>

namespace arena {

enum class MediaType { Unknown, Video, Audio, Image };

struct MediaInfo {
    QString path;
    bool exists = false;
    qulonglong sizeBytes = 0;
    MediaType type = MediaType::Unknown;
    // Filled by the FFmpeg probe in M1:
    int width = 0;
    int height = 0;
    double fps = 0.0;
    qint64 durationMs = 0;
};

class MediaProbe {
public:
    // Cheap, file-level probe. No FFmpeg required.
    static MediaInfo probe(const QString &path);
    static MediaType typeFromPath(const QString &path);
};

class ProxyGenerator {
public:
    // M1: background 540p (configurable) proxy rendering for smooth scrubbing.
    struct Job {
        QString sourcePath;
        int targetHeight = 540;
    };
    static bool isAvailable() { return false; } // FFmpeg phase
};

class WaveformGenerator {
public:
    // M1: peak waveform on background threads; EBU R128 metering in M2/M4.
    static bool isAvailable() { return false; }
};

} // namespace arena
