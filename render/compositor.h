#pragma once
// Compositor: the single place where a timeline frame becomes pixels.
// Deterministic software pipeline (QPainter + per-pixel LUTs) so golden
// frame tests are exact; a GPU path (D3D11 on Windows / OpenGL) hooks in
// behind isGpuAvailable() without changing the contract.
//
// Effect schema (Clip.effects JSON, all values optional):
//   transform : { x, y (canvas fractions, 0 = centered), scale, rotation deg,
//                crop {left,top,right,bottom} }
//   opacity   : { value 0..1 }
//   color     : { exposure (stops, ±4), contrast 0..2, saturation 0..2,
//                 temperature (-100 warm .. +100 cool) }
//   blur      : { radius px }
//   kenburns  : { zoom 1.0..1.5 (animated over the clip), panX, panY }
//   speed     : { value 0.25..4 }
//   transition: { type cross_dissolve|fade_to_black|wipe|push, duration frames }
//                applied at the clip's TAIL against the next clip (or black).
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QString>

#include "project.h"

#include <functional>

namespace arena {

class Compositor {
public:
    struct FrameRequest {
        qint64 frame = 0;   // timeline frame
        double fps = 30.0;
        int width = 1920;
        int height = 1080;
    };

    // The compositor never decodes media itself: the caller fetches source
    // frames. Keeps the pipeline deterministic and unit-testable.
    using FrameFetcher =
        std::function<QImage(const QString &source, qint64 sourceFrame)>;

    static bool isAvailable();   // software core — always true
    static bool isGpuAvailable();// D3D11/OpenGL — false in this build

    // Family used to burn in subtitle tracks (project.subtitleTracks with
    // burnIn == true). "Arial" by default — Windows ships an Arabic subset
    // in Arial; apps/tests may override with any installed family.
    // Process-wide, thread-safe.
    static void setDefaultSubtitleFont(const QString &family);
    static QString defaultSubtitleFont();

    // Render one timeline frame for all video tracks (bottom -> top), then
    // burn in every enabled subtitle track (last track on top). Returns an
    // ARGB32 image of the project canvas size (transparent where no clip
    // covers the frame).
    static QImage renderFrame(const Project &project, qint64 frame,
                              const FrameFetcher &fetch);

    // Deterministic effect hash (cache key ingredient).
    static QString effectsHash(const QJsonObject &effects);

    // ------------------------------------------------------------------
    // LRU frame cache: byte-capped (default 512 MB), thread-safe.
    // ------------------------------------------------------------------
    class FrameCache {
    public:
        explicit FrameCache(qint64 maxBytes = 512 * 1024 * 1024);
        void setMaxBytes(qint64 bytes);
        void put(const QString &key, const QImage &img);
        bool get(const QString &key, QImage *out);
        void clear();
        qint64 maxBytes() const;
        qint64 usedBytes() const;
        quint64 hits() const { return m_hits; }
        quint64 misses() const { return m_misses; }
        int count() const;

    private:
        struct Entry {
            QByteArray pixels;
            QImage::Format format = QImage::Format_ARGB32;
            int w = 0, h = 0;
            qint64 touched = 0;
            qint64 bytes = 0;
        };
        mutable QMutex m_mutex;
        QHash<QString, Entry> m_map;
        QList<QString> m_lru; // oldest at front
        qint64 m_used = 0;
        qint64 m_maxBytes;
        quint64 m_hits = 0;
        quint64 m_misses = 0;
        qint64 m_seq = 0;
    };
};

} // namespace arena
