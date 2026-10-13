#pragma once
// M1 playback engine: A/V-synced preview for a single source media file.
// - Audio is the master clock (QAudioSink processed frames) when the file
//   has audio; otherwise a wall clock drives video.
// - Video frames are decoded on demand (scrub-safe): the timer asks for the
//   frame the clock points at; the decoder seeks + decodes forward.
// - Pure clock math (targetFrame/secondsFromProcessed) is unit-tested.
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>

#include "media.h"

#include <atomic>
#include <thread>

class QAudioSink;

namespace arena {

class PlaybackEngine : public QObject {
    Q_OBJECT
public:
    explicit PlaybackEngine(QObject *parent = nullptr);
    ~PlaybackEngine() override;
    PlaybackEngine(const PlaybackEngine &) = delete;
    PlaybackEngine &operator=(const PlaybackEngine &) = delete;

    // Opens the media file (video and/or audio). *err receives failures.
    bool open(const QString &path, QString *err = nullptr);
    void close();
    bool isOpen() const { return m_open; }
    bool hasVideo() const { return m_video.isOpen(); }
    bool hasAudio() const { return m_audio.isOpen(); }
    double durationSec() const { return m_duration; }
    double fps() const { return m_fps; }

    void play();
    void pause();
    void stop();               // position back to 0
    bool isPlaying() const { return m_playing; }

    // Position of the playback clock (seconds).
    double positionSec() const;

    // Move the playhead; shows the frame at `sec` via frameReady.
    void seekSec(double sec);
    // Step by whole frames (pauses first).
    void stepFrame(int dir);

    // Decode the frame at `sec` directly (used for scrubbing the monitor).
    QImage frameAtSec(double sec);

    // --- Pure clock math (unit-tested) ---
    // Frame index for a clock position (round-to-nearest, clamped at 0).
    static int targetFrame(double elapsedSec, double fps);
    // Clock seconds for an audio-sink processed-frame count.
    static double secondsFromProcessed(qint64 processedFrames, int sampleRate);

signals:
    void frameReady(const QImage &img, int frame);
    void positionChanged(double sec);
    void playStateChanged(bool playing);
    void ended(); // reached the end of the source

private:
    void startAudioPipeline(double fromSec);
    void stopAudioPipeline();
    void audioLoop();
    void onTick();
    double audioPositionSec() const;
    void emitFrameForClock(double sec);
    void stopAtEnd();

    QString m_path;
    double m_duration = 0.0;
    double m_fps = 30.0;
    bool m_open = false;
    bool m_playing = false;
    double m_baseSec = 0.0;      // clock position when paused/stopped
    QElapsedTimer m_wall;        // wall clock for audio-less sources

    VideoDecoder m_video;
    AudioReader m_audio;

    QAudioSink *m_sink = nullptr;
    QIODevice *m_io = nullptr; // write end from QAudioSink::start() (owned by sink)
    std::thread m_audioThread;
    std::atomic<bool> m_audioRun{false};

    QTimer m_tick;
    int m_shownFrame = -1;
    static constexpr int kOutRate = 48000;
    static constexpr int kOutChannels = 2;
};

} // namespace arena
