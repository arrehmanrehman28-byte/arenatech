// FFmpeg (LGPL, dynamic) implementation of the media boundary.
// Compiled only when ARENA_ENABLE_FFMPEG=ON (ARENA_HAS_FFMPEG defined).
// All functions here are blocking and meant for worker threads; probeFull is
// cheap enough to run synchronously for small files.
#include "media.h"

#include <QDir>
#include <QStandardPaths>

#include <cmath>
#include <cstring>

// FFmpeg headers only when the media stack is enabled: the stub path below
// compiles without FFmpeg (ARENA_ENABLE_FFMPEG=OFF, e.g. the Windows core
// gate job) and must not require the headers.
#if ARENA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "ffmpeg_compat.h"
#endif // ARENA_HAS_FFMPEG

namespace arena {

// FFmpeg-only helpers. Guarded at namespace scope: with
// ARENA_ENABLE_FFMPEG=OFF none of the FFmpeg types above exist, and the
// stub implementations below use none of these helpers.
#if ARENA_HAS_FFMPEG
namespace {

qint64 fmtDurationMs(AVFormatContext *fmt)
{
    if (fmt->duration > 0 && fmt->duration != AV_NOPTS_VALUE)
        return qint64(fmt->duration / 1000);
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->duration > 0 && fmt->streams[i]->duration != AV_NOPTS_VALUE)
            return qint64(fmt->streams[i]->duration / 1000);
    }
    return 0;
}

int channelCountOf(const AVChannelLayout &lay, int fallback)
{
    return (av_channel_layout_check(&lay) && lay.nb_channels > 0) ? lay.nb_channels
                                                                  : fallback;
}

// Pointer helpers for the void*-typed FFmpeg members (keep headers clean).
static inline AVFormatContext **fmtPtr(void **p) { return reinterpret_cast<AVFormatContext **>(p); }
static inline AVCodecContext **decPtr(void **p) { return reinterpret_cast<AVCodecContext **>(p); }
static inline AVPacket **pktPtr(void **p) { return reinterpret_cast<AVPacket **>(p); }
static inline AVFrame **framePtr(void **p) { return reinterpret_cast<AVFrame **>(p); }
static inline SwrContext **swrPtr(void **p) { return reinterpret_cast<SwrContext **>(p); }

void toQImage(const AVFrame &frame, QImage *out)
{
    // Per-thread scaler (decoders run on different worker threads).
    thread_local SwsContext *sws = nullptr;
    thread_local int lastFmt = AV_PIX_FMT_NONE;
    thread_local int lastW = 0, lastH = 0;
    if (sws && (lastFmt != frame.format || lastW != frame.width || lastH != frame.height)) {
        sws_freeContext(sws);
        sws = nullptr;
    }
    if (!sws) {
        sws = sws_getContext(frame.width, frame.height, static_cast<AVPixelFormat>(frame.format),
                             frame.width, frame.height, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                             nullptr, nullptr, nullptr);
        lastFmt = frame.format;
        lastW = frame.width;
        lastH = frame.height;
    }
    if (!sws) return;
    const int size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, frame.width, frame.height, 1);
    if (size <= 0) return;
    auto *buf = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(size)));
    if (!buf) return;
    uint8_t *planes[4] = {buf, nullptr, nullptr, nullptr};
    int linesize[4] = {0, 0, 0, 0};
    av_image_fill_arrays(planes, linesize, buf, AV_PIX_FMT_RGB24, frame.width, frame.height, 1);
    sws_scale(sws, frame.data, frame.linesize, 0, frame.height, planes, linesize);
    const QImage tmp(planes[0], frame.width, frame.height, linesize[0], QImage::Format_RGB888);
    *out = tmp.copy();
    av_free(buf);
}

QString codecName(AVCodecID id)
{
    const AVCodec *c = avcodec_find_decoder(id);
    if (!c) return QStringLiteral("unknown");
    return QString::fromUtf8(c->name);
}

// Seconds -> stream time_base.
qint64 secToStreamTimeBase(double sec, const AVRational &tb)
{
    const AVRational msBase = {1, 1000000};
    return av_rescale_q(static_cast<int64_t>(sec * 1000000.0), msBase, tb);
}

} // namespace
#endif // ARENA_HAS_FFMPEG

// --- MediaProbe ---------------------------------------------------------------

bool MediaProbe::probeFull(const QString &path, MediaInfo *out, QString *err)
{
#if ARENA_HAS_FFMPEG
    MediaInfo info = MediaProbe::probe(path);
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        if (out) *out = info;
        if (err) *err = QStringLiteral("Could not open media container");
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        if (out) *out = info;
        if (err) *err = QStringLiteral("Could not analyze media streams");
        return false;
    }
    int v = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int a = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    info.container = QString::fromUtf8(fmt->iformat ? fmt->iformat->name : "unknown");
    info.durationMs = fmtDurationMs(fmt);
    info.hasVideo = v >= 0;
    info.hasAudio = a >= 0;
    if (v >= 0) {
        const AVCodecParameters *par = fmt->streams[v]->codecpar;
        info.width = par->width;
        info.height = par->height;
        info.videoCodec = codecName(par->codec_id);
        AVRational r = fmt->streams[v]->avg_frame_rate;
        if (r.num <= 0) r = fmt->streams[v]->r_frame_rate;
        info.fps = (r.num > 0 && r.den > 0) ? double(r.num) / double(r.den) : 0.0;
    }
    if (a >= 0) {
        const AVCodecParameters *par = fmt->streams[a]->codecpar;
        info.audioCodec = codecName(par->codec_id);
        info.sampleRate = par->sample_rate;
        info.channels = channelCountOf(par->ch_layout, 2);
    }
    if (info.hasVideo && info.hasAudio)
        info.type = MediaType::Video;
    else if (info.hasVideo)
        info.type = MediaType::Video;
    else if (info.hasAudio)
        info.type = MediaType::Audio;

    avformat_close_input(&fmt);
    if (out) *out = info;
    return true;
#else
    MediaInfo info = MediaProbe::probe(path);
    if (out) *out = info;
    if (err) *err = QStringLiteral("Built without FFmpeg (ARENA_ENABLE_FFMPEG=OFF)");
    return false;
#endif
}

// --- VideoDecoder ---------------------------------------------------------------

VideoDecoder::~VideoDecoder() { close(); }

bool VideoDecoder::open(const QString &path, QString *err)
{
    close();
#if !ARENA_HAS_FFMPEG
    Q_UNUSED(path);
    Q_UNUSED(err);
    return false;
#else
    if (avformat_open_input(fmtPtr(&m_fmt), path.toUtf8().constData(), nullptr, nullptr) < 0) {
        if (err) *err = QStringLiteral("Could not open media container");
        return false;
    }
    auto *fmt = static_cast<AVFormatContext *>(m_fmt);
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        if (err) *err = QStringLiteral("Could not analyze media streams");
        close();
        return false;
    }
    m_stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_stream < 0) {
        if (err) *err = QStringLiteral("No video stream found");
        close();
        return false;
    }
    const AVCodecParameters *par = fmt->streams[m_stream]->codecpar;
    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        if (err) *err = QStringLiteral("Video decoder not found");
        close();
        return false;
    }
    m_dec = avcodec_alloc_context3(dec);
    if (!m_dec) {
        if (err) *err = QStringLiteral("Out of memory");
        close();
        return false;
    }
    if (avcodec_parameters_to_context(static_cast<AVCodecContext *>(m_dec), par) < 0 ||
        avcodec_open2(static_cast<AVCodecContext *>(m_dec), dec, nullptr) < 0) {
        if (err) *err = QStringLiteral("Could not initialize video decoder");
        close();
        return false;
    }
    m_width = par->width;
    m_height = par->height;
    AVRational r = fmt->streams[m_stream]->avg_frame_rate;
    if (r.num <= 0) r = fmt->streams[m_stream]->r_frame_rate;
    m_fps = (r.num > 0 && r.den > 0) ? double(r.num) / double(r.den) : 30.0;
    AVStream *st = fmt->streams[m_stream];
    m_durationSec = (st->duration > 0 && st->duration != AV_NOPTS_VALUE)
                        ? av_q2d(st->time_base) * double(st->duration)
                        : (fmt->duration > 0 ? double(fmt->duration) / 1e6 : 0.0);
    if (m_durationSec <= 0.0) m_durationSec = 0.0;
    m_pkt = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_open = true;
    return true;
#endif
}

void VideoDecoder::close()
{
#if ARENA_HAS_FFMPEG
    if (m_frame) av_frame_free(framePtr(&m_frame));
    if (m_pkt) av_packet_free(pktPtr(&m_pkt));
    if (m_dec) avcodec_free_context(decPtr(&m_dec));
    if (m_fmt) avformat_close_input(fmtPtr(&m_fmt));
#endif
    m_open = false;
    m_stream = -1;
    m_durationSec = 0.0;
    m_fps = 0.0;
}

// FFmpeg-only decoder plumbing (the guarded members above/below are the
// OFF-path entry points; these are never reached when ARENA_ENABLE_FFMPEG=OFF).
#if ARENA_HAS_FFMPEG
int VideoDecoder::readVideoPacket()
{
    // 0 = a video packet was fed to the decoder;
    // AVERROR(EAGAIN) = decoder queue full (call receive first);
    // <0 otherwise = stream ended / error.
    auto *fmt = static_cast<AVFormatContext *>(m_fmt);
    auto *pkt = static_cast<AVPacket *>(m_pkt);
    for (;;) {
        const int r = av_read_frame(fmt, pkt);
        if (r < 0) return r;
        if (pkt->stream_index == m_stream) {
            const int s = avcodec_send_packet(static_cast<AVCodecContext *>(m_dec), pkt);
            av_packet_unref(pkt);
            return s;
        }
        av_packet_unref(pkt);
    }
}

bool VideoDecoder::convertFrame(QImage *out, double *ptsOut)
{
    auto *frame = static_cast<AVFrame *>(m_frame);
    auto *fmt = static_cast<AVFormatContext *>(m_fmt);
    if (!out) return false;
    toQImage(*frame, out);
    if (ptsOut)
        *ptsOut = (frame->pts != AV_NOPTS_VALUE)
                      ? av_q2d(fmt->streams[m_stream]->time_base) * double(frame->pts)
                      : 0.0;
    av_frame_unref(frame);
    return !out->isNull();
}
#endif // ARENA_HAS_FFMPEG

bool VideoDecoder::nextFrame(QImage *out, double *ptsOut)
{
#if !ARENA_HAS_FFMPEG
    Q_UNUSED(out);
    Q_UNUSED(ptsOut);
    return false;
#else
    if (!m_open) return false;
    auto *dec = static_cast<AVCodecContext *>(m_dec);
    auto *frame = static_cast<AVFrame *>(m_frame);
    bool flushed = false;
    for (;;) {
        const int r = avcodec_receive_frame(dec, frame);
        if (r == 0) return convertFrame(out, ptsOut);
        if (r < 0 && r != AVERROR(EAGAIN)) return false;
        const int s = readVideoPacket();
        if (s < 0) {
            if (!flushed) {
                avcodec_send_packet(dec, nullptr); // drain
                flushed = true;
            } else {
                return false;
            }
        }
        // s == EAGAIN (decoder full) or 0 (packet fed): loop back to receive.
    }
#endif
}

bool VideoDecoder::frameAtSec(double sec, QImage *out, double *ptsOut)
{
#if !ARENA_HAS_FFMPEG
    Q_UNUSED(sec);
    Q_UNUSED(out);
    Q_UNUSED(ptsOut);
    return false;
#else
    if (!m_open) return false;
    auto *fmt = static_cast<AVFormatContext *>(m_fmt);
    AVStream *st = fmt->streams[m_stream];
    if (av_seek_frame(fmt, m_stream, secToStreamTimeBase(sec, st->time_base),
                      AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(static_cast<AVCodecContext *>(m_dec));
    QImage frame;
    double pts = 0.0;
    for (int i = 0; i < 1200; ++i) {
        if (!nextFrame(&frame, &pts)) break;
        if (pts >= sec - 1e-4) {
            *out = frame;
            if (ptsOut) *ptsOut = pts;
            return true;
        }
    }
    return false;
#endif
}

// --- AudioReader --------------------------------------------------------------

AudioReader::~AudioReader() { close(); }

bool AudioReader::open(const QString &path, QString *err)
{
    close();
#if !ARENA_HAS_FFMPEG
    Q_UNUSED(path);
    Q_UNUSED(err);
    return false;
#else
    if (avformat_open_input(fmtPtr(&m_fmt), path.toUtf8().constData(), nullptr, nullptr) < 0) {
        if (err) *err = QStringLiteral("Could not open media container");
        return false;
    }
    auto *fmt = static_cast<AVFormatContext *>(m_fmt);
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        if (err) *err = QStringLiteral("Could not analyze media streams");
        close();
        return false;
    }
    m_stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_stream < 0) {
        if (err) *err = QStringLiteral("No audio stream found");
        close();
        return false;
    }
    const AVCodecParameters *par = fmt->streams[m_stream]->codecpar;
    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        if (err) *err = QStringLiteral("Audio decoder not found");
        close();
        return false;
    }
    m_dec = avcodec_alloc_context3(dec);
    if (!m_dec) {
        if (err) *err = QStringLiteral("Out of memory");
        close();
        return false;
    }
    if (avcodec_parameters_to_context(static_cast<AVCodecContext *>(m_dec), par) < 0 ||
        avcodec_open2(static_cast<AVCodecContext *>(m_dec), dec, nullptr) < 0) {
        if (err) *err = QStringLiteral("Could not initialize audio decoder");
        close();
        return false;
    }
    AVStream *st = fmt->streams[m_stream];
    m_sourceRate = par->sample_rate;
    m_sourceChannels = channelCountOf(par->ch_layout, 2);
    if (m_sourceChannels <= 0) m_sourceChannels = 2;
    m_durationSec = (st->duration > 0 && st->duration != AV_NOPTS_VALUE)
                        ? av_q2d(st->time_base) * double(st->duration)
                        : (fmt->duration > 0 ? double(fmt->duration) / 1e6 : 0.0);
    m_pkt = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_open = true;
    return true;
#endif
}

void AudioReader::close()
{
#if ARENA_HAS_FFMPEG
    if (m_swr) swr_free(swrPtr(&m_swr));
    if (m_frame) av_frame_free(framePtr(&m_frame));
    if (m_pkt) av_packet_free(pktPtr(&m_pkt));
    if (m_dec) avcodec_free_context(decPtr(&m_dec));
    if (m_fmt) avformat_close_input(fmtPtr(&m_fmt));
#endif
    m_open = false;
    m_stream = -1;
    m_eof = false;
    m_lastInRate = 0;
}

void AudioReader::setOutput(int sampleRate, int channels)
{
    m_outRate = sampleRate;
    m_outChannels = channels;
}

// FFmpeg-only resampler/decoder plumbing (never reached when
// ARENA_ENABLE_FFMPEG=OFF; the OFF path's open() rejects before any of
// this is invoked).
#if ARENA_HAS_FFMPEG
bool AudioReader::ensureResampler()
{
    auto *frame = static_cast<AVFrame *>(m_frame);
    const int inRate = frame->sample_rate;
    const int inCh = channelCountOf(frame->ch_layout, 2);
    const AVSampleFormat inFmt = static_cast<AVSampleFormat>(frame->format);
    const int outRate = (m_outRate > 0) ? m_outRate : inRate;
    const int outCh = (m_outChannels > 0) ? m_outChannels : inCh;
    if (m_swr && m_lastInRate == inRate && m_lastInFormat == inFmt &&
        m_lastInChannels == inCh)
        return true;
    if (m_swr) swr_free(swrPtr(&m_swr));
    AVChannelLayout inLayout = frame->ch_layout;
    if (!av_channel_layout_check(&inLayout))
        av_channel_layout_default(&inLayout, inCh);
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, outCh);
    const int sr = swr_alloc_set_opts2(swrPtr(&m_swr), &outLayout, AV_SAMPLE_FMT_FLT, outRate,
                                       &inLayout, inFmt, inRate, 0, nullptr);
    if (sr < 0 || !m_swr || swr_init(static_cast<SwrContext *>(m_swr)) < 0) {
        if (m_swr) swr_free(swrPtr(&m_swr));
        return false;
    }
    m_lastInRate = inRate;
    m_lastInFormat = inFmt;
    m_lastInChannels = inCh;
    return true;
}

int AudioReader::readAudioPacket()
{
    auto *fmt = static_cast<AVFormatContext *>(m_fmt);
    auto *pkt = static_cast<AVPacket *>(m_pkt);
    for (;;) {
        const int r = av_read_frame(fmt, pkt);
        if (r < 0) {
            m_eof = true;
            return r;
        }
        if (pkt->stream_index == m_stream) {
            const int s = avcodec_send_packet(static_cast<AVCodecContext *>(m_dec), pkt);
            av_packet_unref(pkt);
            if (s < 0 && s != AVERROR(EAGAIN)) return s;
            return s;
        }
        av_packet_unref(pkt);
    }
}
#endif // ARENA_HAS_FFMPEG

int AudioReader::readInterleaved(float *buf, int maxSamples)
{
#if !ARENA_HAS_FFMPEG
    Q_UNUSED(buf);
    Q_UNUSED(maxSamples);
    return 0;
#else
    if (!m_open) return 0;
    auto *dec = static_cast<AVCodecContext *>(m_dec);
    auto *frame = static_cast<AVFrame *>(m_frame);
    const int outCh = (m_outChannels > 0) ? m_outChannels : m_sourceChannels;
    int total = 0;
    bool flushed = false;
    for (;;) {
        const int roomFrames = (maxSamples - total) / outCh;
        if (roomFrames <= 0) break;
        const int r = avcodec_receive_frame(dec, frame);
        if (r == 0) {
            if (ensureResampler()) {
                uint8_t *outBuf[1] = {reinterpret_cast<uint8_t *>(buf + total)};
                const int conv =
                    swr_convert(static_cast<SwrContext *>(m_swr), outBuf, roomFrames,
                                const_cast<const uint8_t **>(frame->extended_data),
                                frame->nb_samples);
                if (conv > 0) {
                    total += conv * outCh;
                    av_frame_unref(frame);
                    continue;
                }
            }
            av_frame_unref(frame);
            continue;
        }
        if (r == AVERROR(EAGAIN)) {
            if (m_eof) {
                if (!flushed) {
                    avcodec_send_packet(dec, nullptr);
                    flushed = true;
                } else {
                    break;
                }
            } else {
                readAudioPacket(); // 0 or EAGAIN: feed more; <0 sets m_eof
            }
            continue;
        }
        break;
    }
    return total;
#endif
}

bool AudioReader::seekSec(double sec)
{
#if !ARENA_HAS_FFMPEG
    Q_UNUSED(sec);
    return false;
#else
    if (!m_open) return false;
    auto *fmt = static_cast<AVFormatContext *>(m_fmt);
    AVStream *st = fmt->streams[m_stream];
    if (av_seek_frame(fmt, m_stream, secToStreamTimeBase(sec, st->time_base),
                      AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(static_cast<AVCodecContext *>(m_dec));
    m_eof = false;
    m_lastInRate = 0; // force resampler rebuild after format change
    return true;
#endif
}

// --- WaveformGenerator --------------------------------------------------------

bool WaveformGenerator::isAvailable()
{
#if ARENA_HAS_FFMPEG
    return true;
#else
    return false;
#endif
}

bool WaveformGenerator::generate(const QString &path, int buckets, QVector<Peak> *out, QString *err)
{
    AudioReader reader;
    if (!reader.open(path, err)) return false;
    const int bucketsN = qMax(1, buckets);
    const int rate = reader.sourceRate();
    const int totalSamples = static_cast<int>(reader.durationSec() * rate);
    // Samples per bucket; files without duration metadata get 0.2 s buckets.
    const int perBucket =
        (totalSamples > 0) ? qMax(1, totalSamples / bucketsN) : qMax(1, rate / 5);

    QVector<Peak> peaks(bucketsN);
    QVector<float> buf(qMax(rate, 48000));
    qint64 readTotal = 0;
    for (;;) {
        const int n = reader.readInterleaved(buf.data(), buf.size());
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) {
            int b = static_cast<int>(qMin<qint64>(peaks.size() - 1,
                                                  (readTotal + i) / perBucket));
            const float v = qBound(-1.0f, buf[i], 1.0f);
            peaks[b].lo = qMin(peaks[b].lo, v);
            peaks[b].hi = qMax(peaks[b].hi, v);
        }
        readTotal += n;
    }
    // Normalize so the loudest peak reaches ~1.0 (visually stable across files).
    float maxAbs = 0.0f;
    for (const Peak &pk : peaks) maxAbs = qMax(maxAbs, qMax(qAbs(pk.lo), qAbs(pk.hi)));
    if (maxAbs > 1e-4f) {
        const float g = 1.0f / maxAbs;
        for (Peak &pk : peaks) {
            pk.lo *= g;
            pk.hi *= g;
        }
    }
    *out = peaks;
    return true;
}

// --- Thumbnailer --------------------------------------------------------------

bool Thumbnailer::isAvailable()
{
#if ARENA_HAS_FFMPEG
    return true;
#else
    return false;
#endif
}

bool Thumbnailer::generate(const QString &path, int count, int targetHeight, QList<QImage> *out,
                           QString *err)
{
    VideoDecoder dec;
    if (!dec.open(path, err)) return false;
    const double dur = dec.durationSec();
    const int n = qBound(1, count, 32);
    out->clear();
    for (int i = 0; i < n; ++i) {
        const double t = (dur > 0.0) ? dur * (i + 0.5) / n : 0.0;
        QImage img;
        if (!dec.frameAtSec(t, &img)) break;
        if (img.height() != targetHeight)
            img = img.scaledToHeight(targetHeight, Qt::SmoothTransformation);
        out->append(img);
    }
    return !out->isEmpty();
}

// --- ProxyGenerator -----------------------------------------------------------

bool ProxyGenerator::isAvailable()
{
#if ARENA_HAS_FFMPEG
    return true;
#else
    return false;
#endif
}

QString ProxyGenerator::defaultProxyDir()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/proxies");
    QDir().mkpath(dir);
    return dir;
}

#if ARENA_HAS_FFMPEG

namespace {

struct ProxyJob {
    AVFormatContext *in = nullptr;
    AVFormatContext *out = nullptr;
    AVCodecContext *vdec = nullptr;
    AVCodecContext *venc = nullptr;
    AVCodecContext *adec = nullptr;
    AVCodecContext *aenc = nullptr;
    SwsContext *sws = nullptr;
    SwrContext *swr = nullptr;
    AVStream *vin = nullptr;
    AVStream *ain = nullptr;
    AVStream *vout = nullptr;
    AVStream *aout = nullptr;
    int vIdx = -1;
    int aIdx = -1;
    int width = 0;
    int height = 0;
    double inDuration = 0.0;
    bool headerWritten = false;
    bool pendingVideo = false; // stashed packet the decoder refused
    AVPacket *stash = nullptr;
};

void proxyFree(ProxyJob &j)
{
    if (j.stash) av_packet_free(&j.stash);
    if (j.sws) sws_freeContext(j.sws);
    if (j.swr) swr_free(&j.swr);
    if (j.vdec) avcodec_free_context(&j.vdec);
    if (j.venc) avcodec_free_context(&j.venc);
    if (j.adec) avcodec_free_context(&j.adec);
    if (j.aenc) avcodec_free_context(&j.aenc);
    if (j.out) {
        if (j.out->pb) avio_closep(&j.out->pb);
        avformat_free_context(j.out);
    }
    if (j.in) avformat_close_input(&j.in);
}

bool openDecoder(AVCodecContext **ctx, AVStream *st)
{
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) return false;
    *ctx = avcodec_alloc_context3(dec);
    if (!*ctx) return false;
    if (avcodec_parameters_to_context(*ctx, st->codecpar) < 0 || avcodec_open2(*ctx, dec, nullptr) < 0) {
        avcodec_free_context(ctx);
        return false;
    }
    return true;
}

bool initVideoEncoder(ProxyJob &j, int targetHeight, QString *err)
{
    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!enc) {
        if (err) *err = QStringLiteral("mpeg4 encoder not available");
        return false;
    }
    j.vout = avformat_new_stream(j.out, nullptr);
    j.venc = avcodec_alloc_context3(enc);
    if (!j.vout || !j.venc) {
        if (err) *err = QStringLiteral("Out of memory");
        return false;
    }

    // Scale to target height, preserve aspect, force even dimensions.
    const int inW = j.vin->codecpar->width;
    const int inH = j.vin->codecpar->height;
    int h = targetHeight & ~1;
    int w = static_cast<int>(std::lround(inW * double(targetHeight) / inH)) & ~1;
    if (w < 2) w = 2;
    j.width = w;
    j.height = h;

    j.venc->width = w;
    j.venc->height = h;
    j.venc->pix_fmt = AV_PIX_FMT_YUV420P;
    j.venc->bit_rate = 1000000; // 1 Mbps is plenty for a scrub proxy
    // Keep the source frame rate so proxy duration matches the original.
    AVRational fps = j.vin->avg_frame_rate;
    if (fps.num <= 0) fps = j.vin->r_frame_rate;
    if (fps.num <= 0 || fps.den <= 0) fps = {30, 1};
    j.venc->framerate = fps;
    j.venc->time_base = {fps.den, fps.num};
    if (avcodec_open2(j.venc, enc, nullptr) < 0) {
        if (err) *err = QStringLiteral("Could not initialize proxy video encoder");
        return false;
    }
    // The encoder may normalize its time base; the output stream must use the
    // encoder's actual one so packet timestamps stay consistent.
    j.vout->time_base = j.venc->time_base;
    if (avcodec_parameters_from_context(j.vout->codecpar, j.venc) < 0) {
        if (err) *err = QStringLiteral("Could not copy video encoder parameters");
        return false;
    }
    return true;
}

bool initAudioEncoder(ProxyJob &j, QString *err)
{
    if (j.ain == nullptr) return true; // silent source
    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!enc) {
        if (err) *err = QStringLiteral("aac encoder not available");
        return false;
    }
    j.aout = avformat_new_stream(j.out, nullptr);
    j.aenc = avcodec_alloc_context3(enc);
    if (!j.aout || !j.aenc) {
        if (err) *err = QStringLiteral("Out of memory");
        return false;
    }

    const int inRate = j.ain->codecpar->sample_rate;
    // AAC supports a fixed set of rates; fall back to 48 kHz.
    static const int kSupported[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
    bool ok = false;
    for (int r : kSupported) {
        if (r == inRate) {
            ok = true;
            break;
        }
    }
    j.aenc->sample_rate = ok ? inRate : 48000;

    const int inCh = channelCountOf(j.ain->codecpar->ch_layout, 2);
    if (av_channel_layout_check(&j.ain->codecpar->ch_layout))
        av_channel_layout_copy(&j.aenc->ch_layout, &j.ain->codecpar->ch_layout);
    else
        av_channel_layout_default(&j.aenc->ch_layout, inCh > 0 ? inCh : 2);

    // Pick a sample format the encoder supports (version-aware helper).
    j.aenc->sample_fmt = arena::ff::pickEncoderSampleFormat(enc);
    if (j.aenc->sample_fmt == AV_SAMPLE_FMT_NONE) {
        if (err) *err = QStringLiteral("aac encoder has no usable sample format");
        return false;
    }
    j.aenc->bit_rate = 128000;
    j.aout->time_base = {1, j.aenc->sample_rate};
    if (avcodec_open2(j.aenc, enc, nullptr) < 0) {
        if (err) *err = QStringLiteral("Could not initialize proxy audio encoder");
        return false;
    }
    j.aout->time_base = j.aenc->time_base;
    if (avcodec_parameters_from_context(j.aout->codecpar, j.aenc) < 0) {
        if (err) *err = QStringLiteral("Could not copy audio encoder parameters");
        return false;
    }
    return true;
}

} // namespace

bool ProxyGenerator::generate(const QString &source, const QString &out, int targetHeight,
                              QString *err, std::function<void(double)> *progress)
{
    ProxyJob j;
    bool success = false;
    do {
        if (avformat_open_input(&j.in, source.toUtf8().constData(), nullptr, nullptr) < 0) {
            if (err) *err = QStringLiteral("Could not open source media");
            break;
        }
        if (avformat_find_stream_info(j.in, nullptr) < 0) {
            if (err) *err = QStringLiteral("Could not analyze source streams");
            break;
        }
        j.vIdx = av_find_best_stream(j.in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        j.aIdx = av_find_best_stream(j.in, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (j.vIdx < 0) {
            if (err) *err = QStringLiteral("Source has no video stream");
            break;
        }
        j.vin = j.in->streams[j.vIdx];
        j.ain = (j.aIdx >= 0) ? j.in->streams[j.aIdx] : nullptr;
        j.inDuration =
            (j.vin->duration > 0 && j.vin->duration != AV_NOPTS_VALUE)
                ? av_q2d(j.vin->time_base) * double(j.vin->duration)
                : (j.in->duration > 0 ? double(j.in->duration) / 1e6 : 0.0);

        if (!openDecoder(&j.vdec, j.vin)) {
            if (err) *err = QStringLiteral("Could not initialize source video decoder");
            break;
        }
        if (j.ain && !openDecoder(&j.adec, j.ain)) {
            if (err) *err = QStringLiteral("Could not initialize source audio decoder");
            break;
        }

        if (avformat_alloc_output_context2(&j.out, nullptr, "mp4", out.toUtf8().constData()) < 0) {
            if (err) *err = QStringLiteral("Could not create mp4 output context");
            break;
        }
        j.out->flags |= AVFMT_GLOBALHEADER;
        if (!initVideoEncoder(j, targetHeight, err)) {
            break;
        }
        if (!initAudioEncoder(j, err)) {
            break;
        }
        if (avio_open(&j.out->pb, out.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
            if (err) *err = QStringLiteral("Could not open output file for writing");
            break;
        }
        if (avformat_write_header(j.out, nullptr) < 0) {
            if (err) *err = QStringLiteral("Could not write proxy file header");
            break;
        }
        j.headerWritten = true;

        const int inW = j.vin->codecpar->width;
        const int inH = j.vin->codecpar->height;
        j.sws = sws_getContext(inW, inH, static_cast<AVPixelFormat>(j.vin->codecpar->format),
                               j.width, j.height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr,
                               nullptr, nullptr);
        if (!j.sws) {
            if (err) *err = QStringLiteral("Could not create video scaler");
            break;
        }

        AVPacket *pkt = av_packet_alloc();
        AVFrame *vframe = av_frame_alloc();
        AVFrame *aframe = av_frame_alloc();
        AVFrame *scaled = av_frame_alloc();
        AVFrame *ares = av_frame_alloc();
        if (!pkt || !vframe || !aframe || !scaled || !ares) {
            av_packet_free(&pkt);
            av_frame_free(&vframe);
            av_frame_free(&aframe);
            av_frame_free(&scaled);
            av_frame_free(&ares);
            break;
        }
        scaled->format = AV_PIX_FMT_YUV420P;
        scaled->width = j.width;
        scaled->height = j.height;
        if (av_frame_get_buffer(scaled, 0) < 0) {
            av_packet_free(&pkt);
            av_frame_free(&vframe);
            av_frame_free(&aframe);
            av_frame_free(&scaled);
            av_frame_free(&ares);
            break;
        }
        if (j.aenc) {
            ares->format = j.aenc->sample_fmt;
            ares->sample_rate = j.aenc->sample_rate;
            ares->nb_samples = 2048; // swr output can slightly exceed input frame size
            av_channel_layout_copy(&ares->ch_layout, &j.aenc->ch_layout);
            if (av_frame_get_buffer(ares, 0) < 0) {
                av_packet_free(&pkt);
                av_frame_free(&vframe);
                av_frame_free(&aframe);
                av_frame_free(&scaled);
                av_frame_free(&ares);
                break;
            }
        }

        bool vFlushed = false;
        bool aFlushed = false;
        bool aFlEnc = false;
        bool vDrained = false;
        bool vFlEnc = false;
        bool vEncEof = false;
        bool aEncEof = true; // no audio -> already done
        if (j.aenc) aEncEof = false;
        bool writeFailed = false;
        bool objectsFreed = false;
        auto freeObjects = [&]() {
            if (objectsFreed) return;
            objectsFreed = true;
            av_packet_free(&pkt);
            av_frame_free(&vframe);
            av_frame_free(&aframe);
            av_frame_free(&scaled);
            av_frame_free(&ares);
        };

        auto writePkt = [&](AVPacket *p, AVStream *st) {
            p->stream_index = st->index;
            return av_interleaved_write_frame(j.out, p) >= 0;
        };

        // Main transcode pump: feed decoders, drain decoders into encoders,
        // drain encoders into the muxer, until both encoders hit EOF.
        for (int guard = 0; guard < 1000000 && !(vEncEof && aEncEof); ++guard) {
            // 1) Feed a packet to a decoder that can accept one.
            if (!vFlushed || (j.adec && !aFlushed)) {
                AVPacket *toFeed = nullptr;
                bool isVideo = false;
                if (j.stash) {
                    toFeed = j.stash;
                    isVideo = j.pendingVideo;
                } else {
                    const int r = av_read_frame(j.in, pkt);
                    if (r < 0) {
                        if (!vFlushed) {
                            avcodec_send_packet(j.vdec, nullptr);
                            vFlushed = true;
                        }
                        if (j.adec && !aFlushed) {
                            avcodec_send_packet(j.adec, nullptr);
                            aFlushed = true;
                        }
                        if (j.aenc && !aFlEnc) {
                            avcodec_send_frame(j.aenc, nullptr);
                            aFlEnc = true;
                        }
                    } else {
                        if (pkt->stream_index == j.vIdx) {
                            toFeed = pkt;
                            isVideo = true;
                        } else if (j.aIdx >= 0 && pkt->stream_index == j.aIdx) {
                            toFeed = pkt;
                        } else {
                            av_packet_unref(pkt); // unexpected stream; ignore
                        }
                    }
                }
                if (toFeed) {
                    const int s = isVideo ? avcodec_send_packet(j.vdec, toFeed)
                                          : avcodec_send_packet(j.adec, toFeed);
                    if (s == 0) {
                        av_packet_unref(toFeed);
                        if (toFeed == j.stash) {
                            av_packet_free(&j.stash);
                            j.stash = nullptr;
                        }
                    } else if (s == AVERROR(EAGAIN)) {
                        // Decoder full: stash the packet, retry next round.
                        if (!j.stash) {
                            j.stash = av_packet_alloc();
                            av_packet_move_ref(j.stash, toFeed);
                            j.pendingVideo = isVideo;
                        } else if (toFeed != j.stash) {
                            av_packet_unref(toFeed); // stash busy; drop this one
                        }
                    } else {
                        // Unrecoverable send error: drop the packet.
                        av_packet_unref(toFeed);
                        if (toFeed == j.stash) {
                            av_packet_free(&j.stash);
                            j.stash = nullptr;
                        }
                    }
                }
            }

            // 2) Video decoder -> scaler -> video encoder.
            if (!vFlushed || !vEncEof) {
                const int rv = avcodec_receive_frame(j.vdec, vframe);
                if (rv == 0) {
                    sws_scale(j.sws, vframe->data, vframe->linesize, 0, vframe->height,
                              scaled->data, scaled->linesize);
                    scaled->pts = av_rescale_q(vframe->pts, j.vin->time_base, j.venc->time_base);
                    scaled->best_effort_timestamp = scaled->pts;
                    avcodec_send_frame(j.venc, scaled);
                    if (progress && j.inDuration > 0.0 && vframe->pts != AV_NOPTS_VALUE)
                        (*progress)(qMin(1.0, av_q2d(j.vin->time_base) * double(vframe->pts) /
                                          j.inDuration));
                } else if (vFlushed) {
                    // Decoder drained after the input flush.
                    vDrained = true;
                }
            }
            if (vDrained && !vFlEnc) {
                avcodec_send_frame(j.venc, nullptr);
                vFlEnc = true;
            }
            // 3) Video encoder -> muxer.
            {
                AVPacket e;
                av_new_packet(&e, 0);
                const int rp = avcodec_receive_packet(j.venc, &e);
                if (rp == 0) {
                    if (e.pts != AV_NOPTS_VALUE)
                        e.pts = av_rescale_q(e.pts, j.venc->time_base, j.vout->time_base);
                    if (e.dts != AV_NOPTS_VALUE)
                        e.dts = av_rescale_q(e.dts, j.venc->time_base, j.vout->time_base);
                    if (!writePkt(&e, j.vout)) {
                        av_packet_unref(&e);
                        writeFailed = true;
                        freeObjects();
                        break;
                    }
                } else if (rp < 0 && rp != AVERROR(EAGAIN)) {
                    vEncEof = true;
                }
                av_packet_unref(&e);
            }

            if (j.adec) {
                // 4) Audio decoder -> resampler -> audio encoder.
                if (!aFlushed || !aEncEof) {
                    const int ra = avcodec_receive_frame(j.adec, aframe);
                    if (ra == 0) {
                        AVChannelLayout inLay = aframe->ch_layout;
                        if (!av_channel_layout_check(&inLay))
                            av_channel_layout_default(&inLay,
                                                      channelCountOf(aframe->ch_layout, 2));
                        if (!j.swr) {
                            const int sr = swr_alloc_set_opts2(
                                &j.swr, &j.aenc->ch_layout,
                                static_cast<AVSampleFormat>(j.aenc->sample_fmt),
                                j.aenc->sample_rate, &inLay,
                                static_cast<AVSampleFormat>(aframe->format), aframe->sample_rate,
                                0, nullptr);
                            if (sr < 0 || !j.swr || swr_init(j.swr) < 0) {
                                if (j.swr) swr_free(&j.swr);
                                av_frame_unref(aframe);
                                if (err && err->isEmpty())
                                    *err = QStringLiteral("Audio resampling failed");
                                writeFailed = true;
                                freeObjects();
                                break;
                            }
                        }
                        const int conv = swr_convert(
                            j.swr, ares->data, aframe->nb_samples,
                            const_cast<const uint8_t **>(aframe->extended_data),
                            aframe->nb_samples);
                        ares->nb_samples = conv;
                        ares->pts = (aframe->pts != AV_NOPTS_VALUE)
                                        ? av_rescale_q(aframe->pts, j.ain->time_base,
                                                      j.aenc->time_base)
                                        : AV_NOPTS_VALUE;
                        avcodec_send_frame(j.aenc, ares);
                        av_frame_unref(aframe);
                    }
                }
                // 5) Audio encoder -> muxer.
                {
                    AVPacket e;
                    av_new_packet(&e, 0);
                    const int rp = avcodec_receive_packet(j.aenc, &e);
                    if (rp == 0) {
                        if (e.pts != AV_NOPTS_VALUE)
                            e.pts = av_rescale_q(e.pts, j.aenc->time_base, j.aout->time_base);
                        if (e.dts != AV_NOPTS_VALUE)
                            e.dts = av_rescale_q(e.dts, j.aenc->time_base, j.aout->time_base);
                        if (!writePkt(&e, j.aout)) {
                            av_packet_unref(&e);
                            writeFailed = true;
                            freeObjects();
                            break;
                        }
                    } else if (rp < 0 && rp != AVERROR(EAGAIN)) {
                        aEncEof = true;
                    }
                    av_packet_unref(&e);
                }
            }
        }

        if (!writeFailed) {
            av_write_trailer(j.out);
            success = true;
        }
        freeObjects();
        if (success && progress) (*progress)(1.0);
    } while (false);

    proxyFree(j);
    if (!success) {
        if (j.headerWritten) QFile::remove(out);
        if (err && err->isEmpty())
            *err = QStringLiteral("Proxy encoding failed");
    }
    return success;
}

#else

bool ProxyGenerator::generate(const QString &source, const QString &out, int targetHeight,
                              QString *err, std::function<void(double)> *progress)
{
    Q_UNUSED(source);
    Q_UNUSED(out);
    Q_UNUSED(targetHeight);
    Q_UNUSED(progress);
    if (err) *err = QStringLiteral("Built without FFmpeg (ARENA_ENABLE_FFMPEG=OFF)");
    return false;
}

#endif

} // namespace arena
