// M1 playback engine — see playback.h for the sync model.
#include "playback.h"

#include <QThread>

#include <algorithm>
#include <cmath>
#include <vector>

#if ARENA_HAS_QTAUDIO
#include <QAudioFormat>
#include <QAudioSink>
#endif

namespace arena {

PlaybackEngine::PlaybackEngine(QObject *parent) : QObject(parent)
{
    m_tick.setInterval(33);
    connect(&m_tick, &QTimer::timeout, this, &PlaybackEngine::onTick);
}

PlaybackEngine::~PlaybackEngine()
{
    m_playing = false;
    stopAudioPipeline();
    m_video.close();
    m_audio.close();
#if ARENA_HAS_QTAUDIO
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
#endif
}

bool PlaybackEngine::open(const QString &path, QString *err)
{
    close();
    QString vErr, aErr;
    const bool v = m_video.open(path, &vErr);
    const bool a = m_audio.open(path, &aErr);
    if (!v && !a) {
        if (err) *err = vErr;
        return false;
    }
    m_path = path;
    m_duration = qMax(m_video.durationSec(), m_audio.durationSec());
    m_fps = m_video.isOpen() ? m_video.fps() : 30.0;
    if (m_fps <= 0.0) m_fps = 30.0;
    m_open = true;
    m_baseSec = 0.0;
    m_shownFrame = -1;
    return true;
}

void PlaybackEngine::close()
{
    m_playing = false;
    m_tick.stop();
    stopAudioPipeline();
    m_video.close();
    m_audio.close();
    m_open = false;
    m_path.clear();
    m_duration = 0.0;
    m_shownFrame = -1;
}

// --- clock math (pure) ---------------------------------------------------------

int PlaybackEngine::targetFrame(double elapsedSec, double fps)
{
    if (fps <= 0.0 || elapsedSec < 0.0) return 0;
    const int f = static_cast<int>(std::lround(elapsedSec * fps));
    return f > 0 ? f : 0;
}

double PlaybackEngine::secondsFromProcessed(qint64 processedFrames, int sampleRate)
{
    if (sampleRate <= 0) return 0.0;
    return double(processedFrames) / double(sampleRate);
}

// --- audio pipeline -------------------------------------------------------------

double PlaybackEngine::audioPositionSec() const
{
#if ARENA_HAS_QTAUDIO
    if (m_sink) return m_baseSec + m_sink->processedUSecs() / 1e6;
#endif
    return m_baseSec;
}

void PlaybackEngine::stopAudioPipeline()
{
    m_audioRun = false;
#if ARENA_HAS_QTAUDIO
    if (m_sink) m_sink->stop();
#endif
    if (m_audioThread.joinable()) m_audioThread.join();
#if ARENA_HAS_QTAUDIO
    // The device returned by QAudioSink::start() is owned by the sink.
    m_io = nullptr;
    if (m_sink) {
        delete m_sink;
        m_sink = nullptr;
    }
#endif
}

void PlaybackEngine::startAudioPipeline(double fromSec)
{
#if !ARENA_HAS_QTAUDIO
    Q_UNUSED(fromSec);
    return;
#else
    if (!m_audio.isOpen()) return;
    stopAudioPipeline();
    QAudioFormat fmt;
    fmt.setSampleFormat(QAudioFormat::SampleFormat::Int16);
    fmt.setSampleRate(kOutRate);
    fmt.setChannelCount(kOutChannels);
    m_sink = new QAudioSink(fmt, this);
    m_sink->setBufferSize(200000); // ~200 ms
    m_audio.setOutput(kOutRate, kOutChannels);
    if (!m_audio.seekSec(fromSec)) {
        fromSec = 0.0;
    }
    m_baseSec = fromSec; // sink element 0 == fromSec on the timeline

    // Prime: discard everything before `fromSec`, then prefill ~100 ms.
    const int chunk = kOutRate / 10 * kOutChannels;
    std::vector<float> buf(chunk);
    qint64 toSkip = static_cast<qint64>(fromSec * kOutRate) * kOutChannels;
    while (toSkip > 0) {
        const int n = m_audio.readInterleaved(buf.data(), qMin<qint64>(chunk, toSkip));
        if (n <= 0) break;
        toSkip -= n;
    }
    m_io = m_sink->start();
    m_audioRun = true;
    m_audioThread = std::thread([this] { audioLoop(); });
#endif
}

void PlaybackEngine::audioLoop()
{
#if !ARENA_HAS_QTAUDIO
    return;
#else
    const int chunk = kOutRate / 10 * kOutChannels; // 100 ms
    std::vector<float> fbuf(chunk);
    std::vector<qint16> ibuf(chunk);
    int silentChunks = 0;
    while (m_audioRun) {
        if (!m_io || m_sink->state() == QAudio::IdleState) break;
        const int n = m_audio.readInterleaved(fbuf.data(), chunk);
        if (n > 0) {
            silentChunks = 0;
            for (int i = 0; i < n; ++i) {
                const double v = std::clamp(double(fbuf[i]), -1.0, 1.0);
                ibuf[i] = static_cast<qint16>(v * 32767.0);
            }
        } else {
            std::fill(ibuf.begin(), ibuf.end(), 0);
            if (++silentChunks > 30) break; // ~3 s of silence: stop
        }
        qint64 written = 0;
        const qint64 total = silentChunks > 0 ? qint64(chunk) * 2 : qint64(n) * 2;
        int spin = 0;
        while (m_audioRun && written < total) {
            // Never block in write(): if the device buffer cannot take the
            // data yet, sleep and retry (bounded, so stopAudioPipeline's
            // join() can never deadlock on a stalled/no-consumer device).
            if (m_sink->bytesFree() < total - written) {
                if (++spin > 500) break; // ~10 s without space: give up
                QThread::msleep(2);
                continue;
            }
            spin = 0;
            const qint64 w = m_io->write(
                reinterpret_cast<const char *>(ibuf.data()) + written,
                static_cast<qint64>(total - written));
            if (w <= 0) {
                if (++spin > 500) break;
                QThread::msleep(2);
            } else {
                written += w;
            }
        }
        if (m_sink->state() == QAudio::IdleState) break;
    }
#endif
}

// --- transport ------------------------------------------------------------------

double PlaybackEngine::positionSec() const
{
    if (!m_playing) return m_baseSec;
#if ARENA_HAS_QTAUDIO
    if (m_audio.isOpen() && m_sink) return audioPositionSec();
#endif
    // Wall clock (audio-less sources, or builds without audio output).
    return m_baseSec + m_wall.elapsed() / 1000.0;
}

void PlaybackEngine::play()
{
    if (!m_open || m_playing) return;
    double pos = positionSec();
    if (m_duration > 0.0 && pos >= m_duration - 0.05) pos = 0.0;
    m_playing = true;
    m_shownFrame = -1;
#if ARENA_HAS_QTAUDIO
    if (m_audio.isOpen()) {
        startAudioPipeline(pos); // sets m_baseSec; audio drives the clock
        if (!m_io) {
            // No usable audio device: fall back to the wall clock so video
            // still plays (headless CI, muted machines, ...).
            stopAudioPipeline();
            m_baseSec = pos;
            m_wall.start();
        }
    } else {
        m_baseSec = pos;
        m_wall.start();
    }
#else
    m_baseSec = pos;
    m_wall.start();
#endif
    m_tick.start();
    emitFrameForClock(pos);
    emit playStateChanged(true);
}

void PlaybackEngine::pause()
{
    if (!m_open || !m_playing) return;
    m_playing = false;
    m_tick.stop();
    m_baseSec = positionSec();
#if ARENA_HAS_QTAUDIO
    if (m_sink) m_sink->suspend();
#endif
    emit playStateChanged(false);
}

void PlaybackEngine::stop()
{
    if (!m_open) return;
    m_playing = false;
    m_tick.stop();
    stopAudioPipeline();
    m_baseSec = 0.0;
    m_shownFrame = -1;
    emitFrameForClock(0.0);
    emit positionChanged(0.0);
    emit playStateChanged(false);
}

void PlaybackEngine::seekSec(double sec)
{
    if (!m_open) return;
    const double clamped = std::clamp(sec, 0.0, qMax(0.0, m_duration - 1e-3));
    const bool wasPlaying = m_playing;
    if (wasPlaying) {
        m_playing = false;
        m_tick.stop();
        stopAudioPipeline();
    }
    m_baseSec = clamped;
#if ARENA_HAS_QTAUDIO
    if (!m_audio.isOpen()) m_wall.start();
#else
    m_wall.start();
#endif
    m_shownFrame = -1;
    emitFrameForClock(clamped);
    if (wasPlaying) {
        m_playing = true;
#if ARENA_HAS_QTAUDIO
        if (m_audio.isOpen()) {
            startAudioPipeline(clamped); // sets m_baseSec
            if (!m_io) {
                stopAudioPipeline();
                m_baseSec = clamped;
                m_wall.start();
            }
        } else {
            m_baseSec = clamped;
            m_wall.start();
        }
#else
        m_baseSec = clamped;
        m_wall.start();
#endif
        m_tick.start();
    }
    emit positionChanged(clamped);
}

void PlaybackEngine::stepFrame(int dir)
{
    if (!m_open) return;
    if (m_playing) pause();
    const double f = m_fps;
    int frame = targetFrame(m_baseSec, f) + dir;
    const int lastFrame = static_cast<int>(std::floor(m_duration * f));
    frame = std::clamp(frame, 0, qMax(0, lastFrame));
    m_baseSec = double(frame) / f;
    m_shownFrame = frame;
    if (m_video.isOpen()) {
        QImage img;
        if (m_video.frameAtSec(m_baseSec, &img))
            emit frameReady(img, frame);
    }
    emit positionChanged(m_baseSec);
}

QImage PlaybackEngine::frameAtSec(double sec)
{
    if (!m_video.isOpen()) return QImage();
    QImage img;
    const double clamped = std::clamp(sec, 0.0, qMax(0.0, m_duration));
    if (!m_video.frameAtSec(clamped, &img)) {
        // Near-EOF: fall back to the last decodable frame.
        if (m_video.frameAtSec(qMax(0.0, clamped - 0.2), &img)) return img;
    }
    return img;
}

void PlaybackEngine::emitFrameForClock(double sec)
{
    if (!m_video.isOpen()) {
        emit positionChanged(sec);
        return;
    }
    const int frame = targetFrame(sec, m_fps);
    QImage img;
    if (m_video.frameAtSec(sec, &img)) {
        m_shownFrame = frame;
        emit frameReady(img, frame);
    } else if (m_shownFrame != frame) {
        // Decode hiccup: hold the last good frame.
        m_shownFrame = frame;
    }
    emit positionChanged(sec);
}

void PlaybackEngine::onTick()
{
    if (!m_playing) return;
    const double sec = positionSec();
    if (m_duration > 0.0 && sec >= m_duration + 0.05) {
        stopAtEnd();
        return;
    }
    emitFrameForClock(sec);
}

void PlaybackEngine::stopAtEnd()
{
    m_playing = false;
    m_tick.stop();
    stopAudioPipeline();
    m_baseSec = qMax(0.0, m_duration);
    emit positionChanged(m_baseSec);
    emit playStateChanged(false);
    emit ended();
}

} // namespace arena
