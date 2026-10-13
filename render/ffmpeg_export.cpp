// M4 export: FFmpeg encode pipeline (blocking; worker threads).
// Video: project frames via the deterministic M3 compositor -> YUV420P ->
// encoder (HW candidate or software fallback). Audio: clip spans from the
// AudioFetcher, crossfaded over clip-tail transitions, 48 kHz stereo.
#include "exporter.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>

#if ARENA_HAS_FFMPEG

// FFmpeg 7 headers carry no C++ guards; the build convention (see
// media/ffmpeg_media.cpp) is to wrap the includes in extern "C".
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cstring>
#include <vector>

namespace arena {
namespace {

constexpr int kOutRate = 48000;
constexpr int kOutChannels = 2;
constexpr int kAudioChunk = 1024; // AAC/Opus frame size

bool isHwEncoder(const QString &name)
{
    return name.contains(QStringLiteral("_nvenc"))
        || name.contains(QStringLiteral("_qsv"))
        || name.contains(QStringLiteral("_amf"));
}

// Encoders present in this build, for the selection function.
QStringList availableEncoders()
{
    const QStringList candidates = {
        QStringLiteral("h264_nvenc"), QStringLiteral("h264_qsv"), QStringLiteral("h264_amf"),
        QStringLiteral("libx264"), QStringLiteral("hevc_nvenc"), QStringLiteral("hevc_qsv"),
        QStringLiteral("hevc_amf"), QStringLiteral("libx265"), QStringLiteral("av1_nvenc"),
        QStringLiteral("av1_amf"), QStringLiteral("libsvtav1"), QStringLiteral("libaom"),
        QStringLiteral("librav1e"), QStringLiteral("libvpx-vp9"), QStringLiteral("aac"),
        QStringLiteral("libopus")};
    QStringList out;
    for (const QString &n : candidates)
        if (avcodec_find_encoder_by_name(n.toUtf8().constData()))
            out.append(n);
    return out;
}

// Ordered candidates to try (HW first when enabled), filtered by availability.
// exportProject walks this list and falls through to the next candidate when
// a hardware encoder exists in the build but cannot open (no driver/GPU).
QStringList videoCandidates(const QString &requested, const QStringList &available,
                            bool hwEnabled)
{
    const static QStringList kHw = {QStringLiteral("h264_nvenc"),
                                    QStringLiteral("h264_qsv"),
                                    QStringLiteral("h264_amf"),
                                    QStringLiteral("hevc_nvenc"),
                                    QStringLiteral("hevc_qsv"),
                                    QStringLiteral("hevc_amf"),
                                    QStringLiteral("av1_nvenc"),
                                    QStringLiteral("av1_amf")};
    QStringList out;
    for (const QString &name : Exporter::videoEncoderChain(requested))
        if ((hwEnabled || !kHw.contains(name)) && available.contains(name))
            out.append(name);
    return out;
}

// Pick the encoder sample format (FLTP preferred), same pattern as the proxy.
AVSampleFormat pickSampleFormat(const AVCodec *enc)
{
    const void *cfg = nullptr;
    int cfgCount = 0;
    AVSampleFormat first = AV_SAMPLE_FMT_NONE;
    if (avcodec_get_supported_config(nullptr, enc, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &cfg,
                                     &cfgCount) == 0 && cfg) {
        const auto *fmts = static_cast<const AVSampleFormat *>(cfg);
        for (int i = 0; fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
            if (fmts[i] == AV_SAMPLE_FMT_FLTP) return AV_SAMPLE_FMT_FLTP;
            if (first == AV_SAMPLE_FMT_NONE) first = fmts[i];
        }
    }
    return first;
}

void fail(ExportJobResult *r, const QString &what)
{
    if (r->error.isEmpty()) r->error = what; // keep the first error
}

// Streamed audio timeline: clip spans (with tail-transition fade applied)
// plus silence in the gaps between them.
class AudioTimeline {
public:
    AudioTimeline(const Project &p, const Exporter::AudioFetcher &audio)
    {
        if (!audio) return;
        const double fps = p.fps;
        for (const Track &t : p.tracks) {
            if (t.type != TrackType::Video) continue;
            for (const Clip &c : t.clips) {
                Span s;
                const int dur = int(c.duration());
                s.startSample = qint64(double(c.offset) * kOutRate / fps);
                s.endSample = qint64(double(c.offset + dur) * kOutRate / fps);
                if (audio(c.sourceRef, c.in, c.in + dur, kOutRate, kOutChannels, &s.samples))
                    s.hasAudio = true;
                // Fade out over the tail transition (dissolve into the next
                // clip, or into black at end of track).
                const QJsonObject tr =
                    c.effects.value(QStringLiteral("transition")).toObject();
                const int fadeFrames =
                    tr.isEmpty() ? 0 : tr.value(QStringLiteral("duration")).toInt(0);
                if (fadeFrames > 0 && !s.samples.isEmpty()) {
                    const int n = qMin(fadeFrames, dur);
                    const int total = int(s.samples.size());
                    const int start =
                        qMax(0, total - int(double(n) * kOutRate / fps + 0.5));
                    for (int i = start; i < total; ++i)
                        s.samples[i] *= float(1.0 - double(i - start + 1) / (total - start));
                }
                m_spans.append(s);
                m_totalSamples = qMax<qint64>(m_totalSamples, s.endSample);
            }
        }
    }

    bool hasContent() const { return m_totalSamples > 0; }
    qint64 totalSamples() const { return m_totalSamples; }

    // Fill `nInterleaved` interleaved slots (L,R,L,R,...). Positions are
    // tracked in per-channel samples; gaps and the end of the timeline are
    // silence-padded. Returns the number of slots written (0 when the
    // timeline is already drained).
    int nextChunk(float *buf, int nInterleaved)
    {
        int filled = 0;
        while (filled < nInterleaved && m_pos < m_totalSamples) {
            // Advance to the span covering the current position.
            while (m_idx < m_spans.size()
                   && (m_pos < m_spans[m_idx].startSample
                       || m_pos >= m_spans[m_idx].endSample))
                ++m_idx;
            float sample[kOutChannels] = {0.0f, 0.0f};
            if (m_idx < m_spans.size()) {
                const Span &sp = m_spans[m_idx];
                const qint64 off = m_pos - sp.startSample;
                if (sp.hasAudio && off >= 0 && off < sp.samples.size() / kOutChannels) {
                    for (int c = 0; c < kOutChannels; ++c)
                        sample[c] = sp.samples[int(off * kOutChannels + c)];
                }
            }
            for (int c = 0; c < kOutChannels; ++c) buf[filled + c] = sample[c];
            filled += kOutChannels;
            ++m_pos;
        }
        // Silence-pad the remainder (final partial chunk).
        for (int i = filled; i < nInterleaved; ++i) buf[i] = 0.0f;
        return filled;
    }

private:
    struct Span {
        qint64 startSample = 0;
        qint64 endSample = 0;
        QVector<float> samples;
        bool hasAudio = false;
    };
    QList<Span> m_spans;
    qint64 m_totalSamples = 0;
    qint64 m_pos = 0;
    int m_idx = 0;
};

} // namespace

ExportJobResult Exporter::exportProject(const Project &p, const ExportSettings &s,
                                        const Compositor::FrameFetcher &fetch,
                                        const AudioFetcher &audio, const Progress &progress,
                                        const CancelToken &cancel)
{
    ExportJobResult r;
    if (p.width <= 0 || p.height <= 0 || p.fps <= 0.0) {
        fail(&r, QStringLiteral("project has no dimensions or fps"));
        return r;
    }
    qint64 totalFrames = 0;
    for (const Track &t : p.tracks)
        if (t.type == TrackType::Video)
            for (const Clip &c : t.clips)
                totalFrames = qMax<qint64>(totalFrames, c.offset + c.duration());
    if (totalFrames <= 0) {
        fail(&r, QStringLiteral("project has no video clips"));
        return r;
    }

    const int fpsInt = qMax(1, int(p.fps + 0.5));
    const int pct = qBound(10, s.scalePercent, 400);
    const int outW = qMax(2, (p.width * pct / 100) & ~1);
    const int outH = qMax(2, (p.height * pct / 100) & ~1);

    const QStringList available = availableEncoders();
    const QStringList vCandidates = videoCandidates(s.videoCodec, available, s.hwEncode);
    if (vCandidates.isEmpty()) {
        fail(&r, QStringLiteral("no usable %1 encoder in this build").arg(s.videoCodec));
        return r;
    }
    const QString aencName =
        s.audioCodec.isEmpty() ? QString() : selectAudioEncoder(s.audioCodec, available);
    if (!s.audioCodec.isEmpty() && aencName.isEmpty()) {
        fail(&r, QStringLiteral("no usable %1 audio encoder").arg(s.audioCodec));
        return r;
    }

    AVFormatContext *fmt = nullptr;
    AVStream *vout = nullptr;
    AVCodecContext *venc = nullptr;
    AVStream *aout = nullptr;
    AVCodecContext *aenc = nullptr;
    SwsContext *sws = nullptr;
    AVFrame *vframe = nullptr;
    bool headerWritten = false;
    bool videoFlushed = false;

    do {
    // --- output context ---------------------------------------------------
    if (avformat_alloc_output_context2(&fmt, nullptr, nullptr,
                                       s.outputPath.toUtf8().constData()) < 0 || !fmt) {
        fail(&r, QStringLiteral("could not open output container: %1").arg(s.outputPath));
        if (fmt) avformat_free_context(fmt);
        return r;
    }

    vout = avformat_new_stream(fmt, nullptr);
    vout->time_base = AVRational{1, fpsInt};
    vout->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    QString vencName;
    for (const QString &name : vCandidates) {
        const AVCodec *vencDef = avcodec_find_encoder_by_name(name.toUtf8().constData());
        AVCodecContext *ctx = avcodec_alloc_context3(vencDef);
        if (!ctx) continue;
        ctx->width = outW;
        ctx->height = outH;
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx->time_base = AVRational{1, fpsInt};
        ctx->framerate = AVRational{fpsInt, 1};
        ctx->gop_size = fpsInt * 2;
        ctx->max_b_frames = 0; // keep the mux simple
        if (isHwEncoder(name)) {
            ctx->bit_rate = 8000000;
        } else {
            av_opt_set_double(ctx->priv_data, "crf", s.crf, 0);
            // Reasonable speed for both interactive and batch export.
            if (name == QStringLiteral("libx264"))
                av_opt_set(ctx->priv_data, "preset", "fast", 0);
            if (name == QStringLiteral("libx265"))
                av_opt_set(ctx->priv_data, "preset", "fast", 0);
            if (name == QStringLiteral("libsvtav1"))
                av_opt_set_int(ctx->priv_data, "preset", 4, 0);
            if (name == QStringLiteral("libaom"))
                av_opt_set_int(ctx->priv_data, "cpu-used", 8, 0);
            if (name == QStringLiteral("librav1e"))
                av_opt_set_int(ctx->priv_data, "speed", 5, 0);
            if (name == QStringLiteral("libvpx-vp9")) {
                av_opt_set(ctx->priv_data, "deadline", "good", 0);
                av_opt_set_int(ctx->priv_data, "b", 0, 0); // pure CRF mode
            }
        }
        if (avcodec_open2(ctx, vencDef, nullptr) == 0) {
            venc = ctx;
            vencName = name;
            break;
        }
        // The encoder exists in the build but cannot open (e.g. hardware
        // encoder without a driver/GPU): fall through to the next candidate.
        av_log(nullptr, AV_LOG_WARNING,
               "Arena Edit: video encoder %s could not open; trying fallback",
               name.toUtf8().constData());
        avcodec_free_context(&ctx);
    }
    if (!venc) {
        fail(&r, QStringLiteral("no usable %1 encoder: all candidates failed to open")
                     .arg(s.videoCodec));
        avformat_free_context(fmt);
        return r;
    }
    r.encoder = vencName;
    // Matroska/WebM timestamps are millisecond-based and the muxer writes
    // packet pts verbatim, so the stream time base must be 1/1000. MP4
    // accepts it just as well, which keeps one code path for all formats.
    vout->time_base = AVRational{1, 1000};
    if (avcodec_parameters_from_context(vout->codecpar, venc) < 0) {
        fail(&r, QStringLiteral("could not copy video encoder parameters"));
        avcodec_free_context(&venc);
        avformat_free_context(fmt);
        return r;
    }

    // --- audio encoder -----------------------------------------------------
    if (!aencName.isEmpty()) {
        aout = avformat_new_stream(fmt, nullptr);
        const AVCodec *aencDef = avcodec_find_encoder_by_name(aencName.toUtf8().constData());
        aenc = avcodec_alloc_context3(aencDef);
        // Millisecond time base for the muxer (see vout above); encoder
        // packets are rescaled from 1/48000 before writing.
        aout->time_base = AVRational{1, 1000};
        aout->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        aenc->sample_rate = kOutRate;
        av_channel_layout_default(&aenc->ch_layout, kOutChannels);
        aenc->bit_rate = qMax(32000, int(s.audioBitrate));
        const AVSampleFormat sf = pickSampleFormat(aencDef);
        if (sf == AV_SAMPLE_FMT_NONE) {
            fail(&r, QStringLiteral("audio encoder %1 has no usable sample format")
                         .arg(aencName));
            avcodec_free_context(&venc);
            avformat_free_context(fmt);
            return r;
        }
        aenc->sample_fmt = sf;
        if (avcodec_open2(aenc, aencDef, nullptr) < 0) {
            fail(&r, QStringLiteral("could not open audio encoder %1").arg(aencName));
            avcodec_free_context(&venc);
            avcodec_free_context(&aenc);
            avformat_free_context(fmt);
            return r;
        }
        if (avcodec_parameters_from_context(aout->codecpar, aenc) < 0) {
            fail(&r, QStringLiteral("could not copy audio encoder parameters"));
            avcodec_free_context(&venc);
            avcodec_free_context(&aenc);
            avformat_free_context(fmt);
            return r;
        }
    }

    // --- file + header ------------------------------------------------------
    if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt->pb, s.outputPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
            fail(&r, QStringLiteral("could not open file for writing: %1").arg(s.outputPath));
            avcodec_free_context(&venc);
            if (aenc) avcodec_free_context(&aenc);
            avformat_free_context(fmt);
            return r;
        }
    }
    if (avformat_write_header(fmt, nullptr) < 0) {
        fail(&r, QStringLiteral("could not write container header"));
        break;
    }
    headerWritten = true;

    // --- scaler: project canvas (QImage ARGB32 = BGRA bytes) -> YUV420P -----
    sws = sws_getContext(p.width, p.height, AV_PIX_FMT_BGRA,
                         outW, outH, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                         nullptr, nullptr, nullptr);
    if (!sws) {
        fail(&r, QStringLiteral("could not create frame scaler"));
        break;
    }
    vframe = av_frame_alloc();
    vframe->format = AV_PIX_FMT_YUV420P;
    vframe->width = outW;
    vframe->height = outH;
    if (av_frame_get_buffer(vframe, 0) < 0) {
        fail(&r, QStringLiteral("could not allocate frame buffer"));
        av_frame_free(&vframe);
        break;
    }

    // --- encode video --------------------------------------------------------
    for (qint64 f = 0; f < totalFrames; ++f) {
        if (cancel && cancel()) {
            r.cancelled = true;
            break;
        }
        QImage img = Compositor::renderFrame(p, f, fetch);
        if (img.isNull()) img = QImage(p.width, p.height, QImage::Format_ARGB32);
        if (img.format() != QImage::Format_ARGB32)
            img = img.convertToFormat(QImage::Format_ARGB32);
        // Opaque: the compositor canvas can be transparent (empty timeline).
        QImage opaque(img.size(), QImage::Format_ARGB32);
        opaque.fill(Qt::black);
        {
            QPainter q(&opaque);
            q.drawImage(0, 0, img);
        }
        const uint8_t *planes[4] = {
            reinterpret_cast<const uint8_t *>(opaque.constScanLine(0)), nullptr, nullptr,
            nullptr};
        int linesizes[4] = {int(opaque.bytesPerLine()), 0, 0, 0};
        sws_scale(sws, planes, linesizes, 0, p.height, vframe->data, vframe->linesize);
        // Frame index f expressed in the encoder's (possibly normalized)
        // time base. Packets coming back are rescaled to the stream time
        // base (1/1000) before muxing.
        vframe->pts = av_rescale_q(f, AVRational{1, fpsInt}, venc->time_base);

        if (avcodec_send_frame(venc, vframe) < 0) {
            fail(&r, QStringLiteral("video encode send failed"));
            break;
        }
        bool drained = false;
        bool sendFailed = false;
        while (!drained) {
            AVPacket *pkt = av_packet_alloc();
            const int rc = avcodec_receive_packet(venc, pkt);
            if (rc == 0) {
                pkt->stream_index = vout->index;
                av_packet_rescale_ts(pkt, venc->time_base, vout->time_base);
                if (av_interleaved_write_frame(fmt, pkt) < 0)
                    fail(&r, QStringLiteral("could not write video packet"));
                av_packet_free(&pkt);
                if (!r.error.isEmpty()) { sendFailed = true; break; }
            } else if (rc == AVERROR(EAGAIN)) {
                av_packet_free(&pkt);
                drained = true;
            } else {
                av_packet_free(&pkt);
                fail(&r, QStringLiteral("video encode receive failed"));
                sendFailed = true;
                drained = true;
            }
        }
        if (sendFailed) break;
        r.framesWritten = f + 1;
        if (progress && (f % 5 == 0 || f + 1 == totalFrames)) progress(f + 1, totalFrames);
    }
    // Drain the video encoder.
    avcodec_send_frame(venc, nullptr);
    for (;;) {
        AVPacket *pkt = av_packet_alloc();
        const int rc = avcodec_receive_packet(venc, pkt);
        if (rc == 0) {
            pkt->stream_index = vout->index;
            av_packet_rescale_ts(pkt, venc->time_base, vout->time_base);
            av_interleaved_write_frame(fmt, pkt);
            av_packet_free(&pkt);
        } else {
            av_packet_free(&pkt);
            break;
        }
    }
    videoFlushed = true;

    // --- encode audio ----------------------------------------------------------
    if (aenc) {
        const int chunkSamples = (aenc->frame_size > 0) ? aenc->frame_size : kAudioChunk;
        AudioTimeline tl(p, audio);
        AVFrame *aframe = av_frame_alloc();
        aframe->format = aenc->sample_fmt;
        aframe->sample_rate = kOutRate;
        av_channel_layout_copy(&aframe->ch_layout, &aenc->ch_layout);
        aframe->nb_samples = chunkSamples;
        if (av_frame_get_buffer(aframe, 0) < 0)
            fail(&r, QStringLiteral("could not allocate audio frame buffer"));
        std::vector<float> chunkBuf(size_t(chunkSamples) * kOutChannels);
        qint64 apts = 0;
        while (r.error.isEmpty()) {
            const int filled = tl.nextChunk(chunkBuf.data(), chunkSamples * kOutChannels);
            if (filled == 0) break; // timeline drained
            // unref resets format/rate/layout; set everything, then allocate
            // (get_buffer needs nb_samples for audio frames).
            av_frame_unref(aframe);
            aframe->format = aenc->sample_fmt;
            aframe->sample_rate = kOutRate;
            av_channel_layout_uninit(&aframe->ch_layout);
            av_channel_layout_copy(&aframe->ch_layout, &aenc->ch_layout);
            aframe->nb_samples = chunkSamples;
            aframe->pts = apts;
            if (av_frame_get_buffer(aframe, 0) < 0) {
                fail(&r, QStringLiteral("audio frame reallocation failed"));
                break;
            }
            if (aenc->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                // Planar float: de-interleave.
                for (int c = 0; c < kOutChannels; ++c) {
                    auto *dst = reinterpret_cast<float *>(aframe->data[c]);
                    for (int i = 0; i < chunkSamples; ++i)
                        dst[i] = chunkBuf[size_t(i) * kOutChannels + c];
                }
            } else if (aenc->sample_fmt == AV_SAMPLE_FMT_S16) {
                auto *dst = reinterpret_cast<int16_t *>(aframe->data[0]);
                for (int i = 0; i < chunkSamples * kOutChannels; ++i)
                    dst[i] = int16_t(qBound(-32768.0f, chunkBuf[i] * 32767.0f, 32767.0f));
            } else {
                std::memcpy(aframe->data[0], chunkBuf.data(),
                            sizeof(float) * size_t(chunkSamples) * kOutChannels);
            }
            if (avcodec_send_frame(aenc, aframe) < 0) {
                fail(&r, QStringLiteral("audio encode send failed"));
                break;
            }
            bool drained = false;
            while (!drained) {
                AVPacket *pkt = av_packet_alloc();
                const int rc = avcodec_receive_packet(aenc, pkt);
                if (rc == 0) {
                    pkt->stream_index = aout->index;
                    av_packet_rescale_ts(pkt, aenc->time_base, aout->time_base);
                    av_interleaved_write_frame(fmt, pkt);
                    av_packet_free(&pkt);
                } else if (rc == AVERROR(EAGAIN)) {
                    av_packet_free(&pkt);
                    drained = true;
                } else {
                    av_packet_free(&pkt);
                    fail(&r, QStringLiteral("audio encode receive failed"));
                    drained = true;
                }
            }
            apts += chunkSamples;
        }
        // Drain the audio encoder.
        if (r.error.isEmpty()) {
            avcodec_send_frame(aenc, nullptr);
            for (;;) {
                AVPacket *pkt = av_packet_alloc();
                const int rc = avcodec_receive_packet(aenc, pkt);
                if (rc == 0) {
                    pkt->stream_index = aout->index;
                    av_packet_rescale_ts(pkt, aenc->time_base, aout->time_base);
                    av_interleaved_write_frame(fmt, pkt);
                    av_packet_free(&pkt);
                } else {
                    av_packet_free(&pkt);
                    break;
                }
            }
        }
        av_frame_free(&aframe);
    }
    } while (false);

    if (headerWritten && !r.cancelled) {
        if (fmt->pb) {
            av_write_trailer(fmt);
            if (!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
        }
        if (!r.error.isEmpty()) {
            QDir().remove(s.outputPath); // don't leave a broken file
        } else {
            r.ok = videoFlushed;
            r.bytesWritten = r.ok ? QFileInfo(s.outputPath).size() : 0;
            r.durationSec = double(totalFrames) / fpsInt;
        }
    } else {
        if (fmt->pb) avio_closep(&fmt->pb);
        QDir().remove(s.outputPath); // partial file (cancel or pre-trailer error)
    }
    sws_freeContext(sws);
    if (vframe) av_frame_free(&vframe);
    if (aenc) avcodec_free_context(&aenc);
    avcodec_free_context(&venc);
    avformat_free_context(fmt);
    return r;
}

} // namespace arena

#endif // ARENA_HAS_FFMPEG
