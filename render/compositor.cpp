// Compositor: deterministic software render pipeline + LRU frame cache.
#include "compositor.h"

#include "subtitles.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFont>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QTextLayout>
#include <QtMath>

#include <algorithm>
#include <vector>

namespace arena {

bool Compositor::isAvailable() { return true; }

// The GPU path (D3D11 on Windows, OpenGL optional) lands with the M9
// Windows packaging work; the contract stays identical.
bool Compositor::isGpuAvailable() { return false; }

// ---------------------------------------------------------------------------
// Subtitle burn-in (M5.2)
// ---------------------------------------------------------------------------

namespace {
QMutex g_subtitleFontMutex;
QString g_subtitleFont = QStringLiteral("Arial");
} // namespace

void Compositor::setDefaultSubtitleFont(const QString &family)
{
    const QString f = family.trimmed();
    QMutexLocker lock(&g_subtitleFontMutex);
    if (!f.isEmpty()) g_subtitleFont = f;
}

QString Compositor::defaultSubtitleFont()
{
    QMutexLocker lock(&g_subtitleFontMutex);
    return g_subtitleFont;
}

// Draw every active cue of every burn-in subtitle track onto the canvas
// (bottom-center, white with a black outline, RTL-aware). Runs after the
// video tracks so subtitles are always on top. Deterministic: fixed font
// family/size relative to the canvas, so golden frames are exact.
static void drawBakedSubtitles(QPainter &p, const QImage &canvas,
                               const Project &project, qint64 frame)
{
    if (project.subtitleTracks.isEmpty()) return;
    const int w = canvas.width();
    const int h = canvas.height();
    const int fontPx = qMax(12, h / 24);
    const int outlinePx = qMax(1, fontPx / 16);
    const int marginY = qMax(8, h / 30);
    const int maxTextWidth = qMax(10, w - 2 * (w / 40));

    // Draw top track first so the LAST enabled track ends on top.
    for (int ti = project.subtitleTracks.size() - 1; ti >= 0; --ti) {
        const SubtitleTrack &st = project.subtitleTracks.at(ti);
        if (!st.burnIn) continue;
        for (const TimelineCue &cue : st.cues) {
            if (frame < cue.startFrame || frame >= cue.endFrame) continue;
            const QString text = cue.text.trimmed();
            if (text.isEmpty()) continue;

            QFont font(Compositor::defaultSubtitleFont());
            font.setPixelSize(fontPx);
            font.setBold(true);

            // Direction is resolved by QTextLayout's built-in bidi
            // algorithm (first strong character), so nothing extra is
            // needed here; isRtlLanguage()/textIsRtl() remain available to
            // the UI (panel alignment, editing hints).
            QTextLayout layout(text, font);
            layout.beginLayout();
            while (true) {
                QTextLine line = layout.createLine();
                if (!line.isValid()) break;
                line.setLineWidth(maxTextWidth);
            }
            layout.endLayout();
            const QRectF bounds = layout.boundingRect();
            const double x = (w - bounds.width()) / 2.0;
            const double y = h - marginY - bounds.height();

            // Two passes: black outline, then white fill.
            p.setPen(QPen(Qt::black, 2 * outlinePx, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
            layout.draw(&p, QPointF(x, y));
            p.setPen(QPen(Qt::white, 0));
            layout.draw(&p, QPointF(x, y));
        }
    }
}

// ---------------------------------------------------------------------------
// Deterministic helpers
// ---------------------------------------------------------------------------

static int clampi(int v, int lo, int hi) { return qBound(lo, v, hi); }

// exposure/contrast/temperature -> per-channel LUTs (256 entries).
// Saturation mixes channels toward luma, so it is applied per-pixel
// afterwards (it cannot be expressed as a per-channel LUT).
static void buildColorLUTs(const QJsonObject &color, quint8 lr[256], quint8 lg[256],
                           quint8 lb[256])
{
    const double exposure = color.value(QStringLiteral("exposure")).toDouble(0.0);
    const double contrast = color.value(QStringLiteral("contrast")).toDouble(1.0);
    const double temperature = color.value(QStringLiteral("temperature")).toDouble(0.0);
    const double expMul = qPow(2.0, qBound(-4.0, exposure, 4.0));
    const double c = qBound(0.0, contrast, 2.0);
    const double t = qBound(-100.0, temperature, 100.0) / 100.0;
    for (int i = 0; i < 256; ++i) {
        double v = double(i) * expMul;
        v = (v - 128.0) * c + 128.0;
        double r = v, g = v, b = v;
        r += t * 24.0;
        b -= t * 24.0;
        lr[i] = quint8(clampi(int(qRound(r)), 0, 255));
        lg[i] = quint8(clampi(int(qRound(g)), 0, 255));
        lb[i] = quint8(clampi(int(qRound(b)), 0, 255));
    }
}

static QImage applySaturation(const QImage &in, double s)
{
    if (qAbs(s - 1.0) < 1e-6) return in;
    QImage out(in.size(), QImage::Format_ARGB32);
    for (int y = 0; y < in.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            const QRgb p = src[x];
            const double luma = 0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p);
            dst[x] = qRgba(quint8(clampi(int(luma + (qRed(p) - luma) * s + 0.5), 0, 255)),
                           quint8(clampi(int(luma + (qGreen(p) - luma) * s + 0.5), 0, 255)),
                           quint8(clampi(int(luma + (qBlue(p) - luma) * s + 0.5), 0, 255)),
                           qAlpha(p));
        }
    }
    return out;
}

static QImage applyLUT(const QImage &in, const quint8 *lr, const quint8 *lg,
                       const quint8 *lb)
{
    QImage out(in.size(), QImage::Format_ARGB32);
    for (int y = 0; y < in.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            const QRgb p = src[x];
            dst[x] = qRgba(lr[qRed(p)], lg[qGreen(p)], lb[qBlue(p)], qAlpha(p));
        }
    }
    return out;
}

// Separable box blur (3 passes each direction ~ gaussian), RGB only,
// edge-clamped. Deterministic for a fixed radius.
static QImage boxBlur(const QImage &in, int radius)
{
    if (radius <= 0) return in;
    const int w = in.width();
    const int h = in.height();
    std::vector<float> buf(size_t(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = row[x];
            buf[(size_t(y) * w + x) * 3 + 0] = qRed(p);
            buf[(size_t(y) * w + x) * 3 + 1] = qGreen(p);
            buf[(size_t(y) * w + x) * 3 + 2] = qBlue(p);
        }
    }
    std::vector<float> tmp(buf.size());
    auto pass = [&](bool horizontal) {
        const int len = horizontal ? w : h;
        const int lines = horizontal ? h : w;
        for (int line = 0; line < lines; ++line) {
            for (int ch = 0; ch < 3; ++ch) {
                for (int i = 0; i < len; ++i) {
                    float sum = 0.0f;
                    int n = 0;
                    for (int k = -radius; k <= radius; ++k) {
                        const int j = clampi(i + k, 0, len - 1);
                        const int idx = horizontal ? (size_t(line) * w + j) * 3 + ch
                                                   : (size_t(j) * w + line) * 3 + ch;
                        sum += buf[idx];
                        ++n;
                    }
                    const int oidx = horizontal ? (size_t(line) * w + i) * 3 + ch
                                                : (size_t(i) * w + line) * 3 + ch;
                    tmp[oidx] = sum / n;
                }
            }
        }
        std::swap(buf, tmp);
    };
    for (int i = 0; i < 3; ++i) {
        pass(true);
        pass(false);
    }
    QImage out(in.size(), QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = src[x];
            dst[x] = qRgba(quint8(clampi(int(buf[(size_t(y) * w + x) * 3 + 0] + 0.5f), 0, 255)),
                           quint8(clampi(int(buf[(size_t(y) * w + x) * 3 + 1] + 0.5f), 0, 255)),
                           quint8(clampi(int(buf[(size_t(y) * w + x) * 3 + 2] + 0.5f), 0, 255)),
                           qAlpha(p));
        }
    }
    return out;
}

// Pixel-exact linear blend: out = a*(1-p) + b*p (per channel, +0.5 round).
static QImage blendPixels(const QImage &a, const QImage &b, double p)
{
    QImage out(a.size(), QImage::Format_ARGB32);
    const double ip = 1.0 - p;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *ra = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *rb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        QRgb *ro = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const QRgb pa = ra[x];
            const QRgb pb = rb[x];
            const int ar = int(qRed(pa) * ip + qRed(pb) * p + 0.5);
            const int ag = int(qGreen(pa) * ip + qGreen(pb) * p + 0.5);
            const int ab = int(qBlue(pa) * ip + qBlue(pb) * p + 0.5);
            const int aa = int(qAlpha(pa) * ip + qAlpha(pb) * p + 0.5);
            ro[x] = qRgba(quint8(clampi(ar, 0, 255)), quint8(clampi(ag, 0, 255)),
                          quint8(clampi(ab, 0, 255)), quint8(clampi(aa, 0, 255)));
        }
    }
    return out;
}

// Wipe: left of the boundary = a, right = b, 2 px feather at the boundary.
static QImage blendWipe(const QImage &a, const QImage &b, double p)
{
    QImage out = a;
    const int w = a.width();
    const double bx = p * double(w);
    for (int x = 0; x < w; ++x) {
        const double dx = bx - double(x);
        double takeB; // fraction of b at column x
        if (dx >= 2.0) takeB = 0.0;
        else if (dx <= -2.0) takeB = 1.0;
        else takeB = (2.0 - dx) / 4.0;
        if (takeB <= 0.0) continue;
        if (takeB >= 1.0) takeB = 1.0;
        for (int y = 0; y < a.height(); ++y) {
            QRgb *ro = reinterpret_cast<QRgb *>(out.scanLine(y));
            const QRgb pa = a.pixel(x, y);
            const QRgb pb = b.pixel(x, y);
            ro[x] = qRgba(quint8(clampi(int(qRed(pa) * (1 - takeB) + qRed(pb) * takeB + 0.5), 0, 255)),
                          quint8(clampi(int(qGreen(pa) * (1 - takeB) + qGreen(pb) * takeB + 0.5), 0, 255)),
                          quint8(clampi(int(qBlue(pa) * (1 - takeB) + qBlue(pb) * takeB + 0.5), 0, 255)),
                          quint8(clampi(int(qAlpha(pa) * (1 - takeB) + qAlpha(pb) * takeB + 0.5), 0, 255)));
        }
    }
    return out;
}

// Push: outgoing slides left, incoming slides in from the right.
static QImage blendPush(const QImage &a, const QImage &b, double p)
{
    QImage out(a.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter q(&out);
    const int w = a.width();
    q.drawImage(QPointF(-p * double(w), 0), a);
    q.drawImage(QPointF((1.0 - p) * double(w), 0), b);
    return out;
}

// ---------------------------------------------------------------------------
// Per-clip layer
// ---------------------------------------------------------------------------

// Apply the clip's effects chain: geometry (kenburns+transform) -> color
// -> blur -> opacity. Order is fixed so results are reproducible.
static QImage applyEffects(const QImage &in, const QJsonObject &fx, int W, int H,
                           const Clip &c, qint64 rel)
{
    QImage img = in;
    const QJsonObject kb = fx.value(QStringLiteral("kenburns")).toObject();
    const QJsonObject tf = fx.value(QStringLiteral("transform")).toObject();
    double scale = tf.value(QStringLiteral("scale")).toDouble(1.0);
    double rot = tf.value(QStringLiteral("rotation")).toDouble(0.0);
    double dx = tf.value(QStringLiteral("x")).toDouble(0.0) * W;
    double dy = tf.value(QStringLiteral("y")).toDouble(0.0) * H;
    if (!kb.isEmpty()) {
        const double t = double(rel) / double(qMax<qint64>(1, c.duration()));
        const double z1 = qBound(1.0, kb.value(QStringLiteral("zoom")).toDouble(1.2), 2.0);
        scale *= 1.0 + (z1 - 1.0) * t;
        dx += kb.value(QStringLiteral("panX")).toDouble(0.0) * W * t;
        dy += kb.value(QStringLiteral("panY")).toDouble(0.0) * H * t;
    }
    if (scale != 1.0 || rot != 0.0 || dx != 0.0 || dy != 0.0) {
        QImage out(W, H, QImage::Format_ARGB32);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.translate(W / 2.0 + dx, H / 2.0 + dy);
        p.rotate(rot);
        p.scale(scale, scale);
        p.drawImage(QRectF(-W / 2.0, -H / 2.0, W, H), img);
        img = out;
    }
    const QJsonObject color = fx.value(QStringLiteral("color")).toObject();
    if (!color.isEmpty()) {
        quint8 lr[256], lg[256], lb[256];
        buildColorLUTs(color, lr, lg, lb);
        img = applyLUT(img, lr, lg, lb);
        img = applySaturation(img, qBound(0.0, color.value(QStringLiteral("saturation")).toDouble(1.0), 2.0));
    }
    const QJsonObject blur = fx.value(QStringLiteral("blur")).toObject();
    const int radius = blur.value(QStringLiteral("radius")).toInt(0);
    if (radius > 0) img = boxBlur(img, radius);
    const QJsonObject op = fx.value(QStringLiteral("opacity")).toObject();
    const double alpha = qBound(0.0, op.value(QStringLiteral("value")).toDouble(1.0), 1.0);
    if (alpha < 1.0) {
        for (int y = 0; y < img.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const QRgb p = row[x];
                row[x] = qRgba(qRed(p), qGreen(p), qBlue(p),
                               quint8(int(qAlpha(p) * alpha + 0.5)));
            }
        }
    }
    return img;
}

static QImage renderClip(const Project &project, const Track &track, int clipIdx,
                         qint64 frame, const Compositor::FrameFetcher &fetch)
{
    const Clip &c = track.clips[clipIdx];
    if (frame < c.offset || frame >= c.offset + c.duration()) return QImage();
    const double speed = qBound(0.25, c.speed, 4.0);
    const qint64 rel = frame - c.offset;
    const qint64 sourceFrame = c.in + qRound(double(rel) / speed);
    QImage src = fetch(c.sourceRef, sourceFrame);
    if (src.isNull()) return QImage();
    const int W = project.width;
    const int H = project.height;
    // Base layer: fit the source frame to the canvas (KeepAspectRatio).
    QImage canvas(W, H, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    QImage fitted = src.scaled(QSize(W, H), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    {
        QPainter cp(&canvas);
        cp.drawImage((W - fitted.width()) / 2, (H - fitted.height()) / 2, fitted);
    }
    if (!c.effects.isEmpty())
        canvas = applyEffects(canvas, c.effects, W, H, c, rel);
    return canvas;
}

// Tail transition of clip i against clip i+1 (no-overlap model: the
// incoming side advances dur frames across the outgoing tail).
static QImage applyTailTransition(const Project &project, const Track &track, int i,
                                  qint64 frame, const Compositor::FrameFetcher &fetch)
{
    const Clip &c = track.clips[i];
    const QJsonObject tr =
        c.effects.value(QStringLiteral("transition")).toObject();
    if (tr.isEmpty()) return QImage();
    const QString type = tr.value(QStringLiteral("type")).toString(
        QStringLiteral("cross_dissolve"));
    const bool hasNext = i + 1 < track.clips.size() &&
        track.clips[i + 1].offset == c.offset + c.duration();
    if (!hasNext && type != QStringLiteral("fade_to_black")) return QImage();
    const int dur = qMax(1, tr.value(QStringLiteral("duration")).toInt(30));
    const qint64 left = c.offset + c.duration() - frame; // frames remaining
    if (left >= dur || left < 0) return QImage();
    const double p = double(dur - left) / double(dur);
    if (type == QStringLiteral("fade_to_black")) {
        const QImage outLayer = renderClip(project, track, i, frame, fetch);
        if (outLayer.isNull()) return QImage();
        QImage black(outLayer.size(), QImage::Format_ARGB32);
        black.fill(Qt::black);
        return blendPixels(outLayer, black, p);
    }
    // Incoming sample: advances from n.in to n.in + dur across the tail.
    const int ni = int(qBound(0.0, p, 1.0) * double(dur));
    const Clip &n = track.clips[i + 1];
    QImage inLayer =
        fetch(n.sourceRef, n.in + ni);
    if (inLayer.isNull()) return QImage();
    QImage inCanvas(project.width, project.height, QImage::Format_ARGB32);
    inCanvas.fill(Qt::transparent);
    QImage fitted = inLayer.scaled(QSize(project.width, project.height),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
    {
        QPainter cp(&inCanvas);
        cp.drawImage((project.width - fitted.width()) / 2,
                     (project.height - fitted.height()) / 2, fitted);
    }
    if (!n.effects.isEmpty())
        inCanvas = applyEffects(inCanvas, n.effects, project.width, project.height, n, 0);

    const QImage outLayer = renderClip(project, track, i, frame, fetch);
    if (outLayer.isNull()) return QImage();
    if (type == QStringLiteral("wipe"))
        return blendWipe(outLayer, inCanvas, p);
    if (type == QStringLiteral("push"))
        return blendPush(outLayer, inCanvas, p);
    // default: cross_dissolve
    return blendPixels(outLayer, inCanvas, p);
}

// ---------------------------------------------------------------------------
// Frame composition
// ---------------------------------------------------------------------------

QImage Compositor::renderFrame(const Project &project, qint64 frame,
                               const FrameFetcher &fetch)
{
    QImage canvas(project.width, project.height, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    QList<const Track *> video;
    for (const Track &t : project.tracks)
        if (t.type == TrackType::Video) video.append(&t);
    for (qsizetype ti = video.size() - 1; ti >= 0; --ti) { // last track on top
        const Track &t = *video[ti];
        for (int i = 0; i < t.clips.size(); ++i) {
            const Clip &c = t.clips[i];
            if (frame < c.offset || frame >= c.offset + c.duration()) continue;
            QImage layer;
            const QJsonObject tr = c.effects.value(QStringLiteral("transition")).toObject();
            if (!tr.isEmpty()) {
                const QString type =
                    tr.value(QStringLiteral("type")).toString(QStringLiteral("cross_dissolve"));
                const bool adjacent = i + 1 < t.clips.size() &&
                    t.clips[i + 1].offset == c.offset + c.duration();
                if (adjacent || type == QStringLiteral("fade_to_black")) {
                    const int dur = qMax(1, tr.value(QStringLiteral("duration")).toInt(30));
                    const qint64 left = c.offset + c.duration() - frame;
                    if (left < dur && left >= 0) {
                        layer = applyTailTransition(project, t, i, frame, fetch);
                    }
                }
            }
            if (layer.isNull()) layer = renderClip(project, t, i, frame, fetch);
            if (!layer.isNull()) p.drawImage(0, 0, layer);
        }
    }
    // M5.2: burn in enabled subtitle tracks (on top of all video).
    drawBakedSubtitles(p, canvas, project, frame);
    return canvas;
}

QString Compositor::effectsHash(const QJsonObject &effects)
{
    // QJsonObject keeps its keys sorted, so the compact JSON is stable.
    const QByteArray data = QJsonDocument(effects).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(16));
}

// ---------------------------------------------------------------------------
// LRU frame cache
// ---------------------------------------------------------------------------

Compositor::FrameCache::FrameCache(qint64 maxBytes) : m_maxBytes(maxBytes) {}

void Compositor::FrameCache::put(const QString &key, const QImage &img)
{
    const QImage conv(img.convertToFormat(QImage::Format_ARGB32));
    Entry e;
    e.pixels = QByteArray(reinterpret_cast<const char *>(conv.constBits()),
                        conv.sizeInBytes());
    e.format = QImage::Format_ARGB32;
    e.w = conv.width();
    e.h = conv.height();
    e.touched = ++m_seq;
    e.bytes = e.pixels.size();
    QMutexLocker lock(&m_mutex);
    if (m_map.contains(key)) {
        const Entry old = m_map.take(key);
        m_used -= old.bytes;
    }
    m_map.insert(key, e);
    m_lru.removeAll(key);
    m_lru.append(key);
    m_used += e.bytes;
    while (m_used > m_maxBytes && !m_lru.isEmpty()) {
        const QString victim = m_lru.takeFirst();
        const Entry old = m_map.take(victim);
        m_used -= old.bytes;
    }
}

bool Compositor::FrameCache::get(const QString &key, QImage *out)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_map.find(key);
    if (it == m_map.end()) {
        ++m_misses;
        return false;
    }
    ++m_hits;
    it->touched = ++m_seq;
    if (m_lru.contains(key)) {
        m_lru.removeAll(key);
        m_lru.append(key);
    }
    if (out)
        *out = QImage(reinterpret_cast<const uchar *>(it->pixels.constData()),
                     it->w, it->h, 0, it->format).copy();
    return true;
}

void Compositor::FrameCache::setMaxBytes(qint64 bytes)
{
    QMutexLocker lock(&m_mutex);
    m_maxBytes = bytes;
    while (m_used > m_maxBytes && !m_lru.isEmpty()) {
        const QString victim = m_lru.takeFirst();
        const Entry old = m_map.take(victim);
        m_used -= old.bytes;
    }
}

void Compositor::FrameCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_map.clear();
    m_lru.clear();
    m_used = 0;
}

qint64 Compositor::FrameCache::maxBytes() const
{
    QMutexLocker lock(&m_mutex);
    return m_maxBytes;
}

qint64 Compositor::FrameCache::usedBytes() const
{
    QMutexLocker lock(&m_mutex);
    return m_used;
}

int Compositor::FrameCache::count() const
{
    QMutexLocker lock(&m_mutex);
    return m_map.size();
}

} // namespace arena
