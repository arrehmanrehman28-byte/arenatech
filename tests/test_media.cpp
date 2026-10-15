// Media probe (file-level) and STT/render availability honesty.
#include <gtest/gtest.h>
#include "media.h"
#include "whisper.h"
#include "render.h"
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <functional>
#include <vector>

using namespace arena;

// The MediaM1 suite drives the real FFmpeg pipeline; it only exists when
// the media stack is built with FFmpeg (ARENA_HAS_FFMPEG is a PUBLIC
// compile definition of arena_media). Other suites still use
// ARENA_TEST_FIXTURES for file-level probes and the STT fixture.
#if defined(ARENA_TEST_FIXTURES) && defined(ARENA_HAS_FFMPEG)
// M1: real FFmpeg pipeline against the shipped fixtures (tests/fixtures/).

TEST(MediaM1, ProbeFullVideoAndAudio)
{
    MediaInfo info;
    QString err;
    ASSERT_TRUE(MediaProbe::probeFull(QString(ARENA_TEST_FIXTURES) + "/clip01.mp4", &info, &err));
    EXPECT_TRUE(info.exists);
    EXPECT_TRUE(info.hasVideo);
    EXPECT_TRUE(info.hasAudio);
    EXPECT_GT(info.width, 0);
    EXPECT_GT(info.height, 0);
    EXPECT_GT(info.fps, 0.0);
    EXPECT_GT(info.durationMs, 0);
    EXPECT_EQ(info.videoCodec, QStringLiteral("h264"));
}

TEST(MediaM1, ProbeWavAudio)
{
    MediaInfo info;
    QString err;
    ASSERT_TRUE(MediaProbe::probeFull(QString(ARENA_TEST_FIXTURES) + "/sample.wav", &info, &err));
    EXPECT_TRUE(info.hasAudio);
    EXPECT_FALSE(info.hasVideo);
    EXPECT_GT(info.sampleRate, 0);
}

TEST(MediaM1, WaveformBucketsAndRange)
{
    QVector<Peak> peaks;
    QString err;
    ASSERT_TRUE(WaveformGenerator::generate(QString(ARENA_TEST_FIXTURES) + "/clip01.mp4",
                                            24, &peaks, &err));
    ASSERT_EQ(peaks.size(), 24);
    for (const Peak &p : peaks) {
        EXPECT_GE(p.hi, p.lo);
        EXPECT_LE(p.hi, 1.0f);
        EXPECT_GE(p.lo, -1.0f);
    }
}

TEST(MediaM1, ThumbnailsCountAndHeight)
{
    QList<QImage> thumbs;
    QString err;
    ASSERT_TRUE(Thumbnailer::generate(QString(ARENA_TEST_FIXTURES) + "/clip01.mp4", 4, 48,
                                      &thumbs, &err));
    ASSERT_EQ(thumbs.size(), 4);
    for (const QImage &t : thumbs) {
        EXPECT_FALSE(t.isNull());
        EXPECT_EQ(t.height(), 48);
    }
}

TEST(MediaM1, VideoDecoderSeekAndFrame)
{
    VideoDecoder dec;
    QString err;
    ASSERT_TRUE(dec.open(QString(ARENA_TEST_FIXTURES) + "/clip01.mp4", &err));
    QImage img;
    double pts = -1;
    ASSERT_TRUE(dec.frameAtSec(0.5, &img, &pts));
    EXPECT_FALSE(img.isNull());
    EXPECT_GE(pts, 0.0);
}

TEST(MediaM1, AudioReaderInterleaved)
{
    AudioReader ar;
    QString err;
    ASSERT_TRUE(ar.open(QString(ARENA_TEST_FIXTURES) + "/clip01.mp4", &err));
    ar.setOutput(48000, 2);
    std::vector<float> buf(4800);
    const int n = ar.readInterleaved(buf.data(), buf.size());
    EXPECT_GT(n, 0);
}

TEST(MediaM1, ProxyGenerationProducesPlayable)
{
    QTemporaryDir tmp;
    const QString out = tmp.filePath("proxy.mp4");
    QString err;
    double lastProg = -1;
    std::function<void(double)> prog = [&lastProg](double d) { lastProg = d; };
    ASSERT_TRUE(ProxyGenerator::generate(QString(ARENA_TEST_FIXTURES) + "/clip01.mp4", out, 180,
                                         &err, &prog));
    EXPECT_GE(lastProg, 1.0 - 1e-6);
    MediaInfo info;
    QString perr;
    ASSERT_TRUE(MediaProbe::probeFull(out, &info, &perr));
    EXPECT_TRUE(info.hasVideo && info.hasAudio);
    EXPECT_EQ(info.height, 180);
    EXPECT_GT(info.durationMs, 0);
}
#endif // ARENA_TEST_FIXTURES


TEST(MediaProbe, DetectsContainersByExtension)
{
    EXPECT_EQ(MediaProbe::typeFromPath("a.mp4"), MediaType::Video);
    EXPECT_EQ(MediaProbe::typeFromPath("a.MKV"), MediaType::Video);
    EXPECT_EQ(MediaProbe::typeFromPath("a.wav"), MediaType::Audio);
    EXPECT_EQ(MediaProbe::typeFromPath("a.png"), MediaType::Image);
    EXPECT_EQ(MediaProbe::typeFromPath("a.xyz"), MediaType::Unknown);
}

TEST(MediaProbe, ProbesExistingFile)
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("clip.mp4");
    {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1234, 'x'));
    }
    const MediaInfo info = MediaProbe::probe(path);
    EXPECT_TRUE(info.exists);
    EXPECT_EQ(info.sizeBytes, 1234);
    EXPECT_EQ(info.type, MediaType::Video);
}

TEST(MediaProbe, MissingFileFlagged)
{
    const MediaInfo info = MediaProbe::probe("/nonexistent/clip.mp4");
    EXPECT_FALSE(info.exists);
    EXPECT_EQ(info.sizeBytes, 0UL);
}

TEST(RenderBoundary, HonestAvailability)
{
    // M3: the deterministic software compositor is available everywhere;
    // the GPU path (D3D11/OpenGL) is an honest no-op off the Windows build.
    EXPECT_TRUE(Compositor::isAvailable());
#if !defined(Q_OS_WIN)
    EXPECT_FALSE(Compositor::isGpuAvailable());
#endif
    EXPECT_GT(EffectRegistry::effectTypes().size(), 5);
    EXPECT_TRUE(EffectRegistry::isTransition("transition.cross_dissolve"));
    EXPECT_FALSE(EffectRegistry::isTransition("transform"));
}

TEST(SttBoundary, HonestAvailability)
{
    // whisper.cpp is linked only when ARENA_ENABLE_WHISPER is set; the
    // boundary must report its state honestly, never fake results.
#if defined(ARENA_ENABLE_WHISPER)
    EXPECT_TRUE(Transcriber::isAvailable());
#else
    EXPECT_FALSE(Transcriber::isAvailable());
#endif
    QString err;
    QList<TranscriptSegment> out;
    // A nonexistent input fails with a clear error either way (and must not
    // touch the network or model cache before checking the file).
    EXPECT_FALSE(Transcriber::transcribe("a.wav", {}, out, &err, nullptr));
    EXPECT_FALSE(err.isEmpty());
    EXPECT_EQ(Transcriber::models().size(), 3);
}

// M7: end-to-end local transcription of the bundled JFK fixture (11 s,
// public domain, shipped with whisper.cpp). The model is on-demand: when it
// has not been downloaded yet the test skips honestly (no network in CI);
// run tools/arena_stt_check once (or set ARENA_WHISPER_MODELS) to populate
// the cache.
TEST(Stt, TranscribeJfkFixture)
{
    if (!Transcriber::isAvailable())
        GTEST_SKIP() << "whisper.cpp not compiled in";
    const QString wav = QString(ARENA_TEST_FIXTURES) + QStringLiteral("/jfk.wav");
    if (!QFile::exists(wav))
        GTEST_SKIP() << "jfk.wav fixture missing";

    // Prefer the fastest model that is ready; skip if none is downloaded.
    QString model;
    if (Transcriber::modelIsReady(QStringLiteral("tiny")))
        model = QStringLiteral("tiny");
    else if (Transcriber::modelIsReady(QStringLiteral("base")))
        model = QStringLiteral("base");
    else
        GTEST_SKIP() << "no whisper model downloaded yet (on-demand; run "
                        "tools/arena_stt_check first)";

    Transcriber::Options opts;
    opts.model = model;
    QList<TranscriptSegment> out;
    QString err;
    QString lang;
    float progressAtEnd = -1.0;
    QElapsedTimer t;
    t.start();
    ASSERT_TRUE(Transcriber::transcribe(wav, opts, out, &err,
                                        [&](float f) { progressAtEnd = f; }, &lang))
        << err.toStdString();
    const qint64 elapsedMs = t.elapsed();

    // Acceptance: bundled sample clip transcribes in < 60 s (4-core CPU).
    EXPECT_LT(elapsedMs, 60000) << "transcription too slow: " << elapsedMs << " ms";
    EXPECT_EQ(lang, QStringLiteral("en"));
    EXPECT_FALSE(out.isEmpty());
    EXPECT_EQ(progressAtEnd, 1.0f);

    QString allText;
    for (const TranscriptSegment &seg : out) {
        EXPECT_LT(seg.startMs, seg.endMs);
        EXPECT_GT(seg.endMs, 0);
        EXPECT_FALSE(seg.text.trimmed().isEmpty());
        allText += seg.text + QLatin1Char(' ');

        qint64 prevEnd = 0;
        for (const WordSpan &w : seg.words) {
            // Word spans stay (within tolerance) inside their segment.
            EXPECT_GE(w.startMs, seg.startMs - 100);
            EXPECT_LE(w.endMs, seg.endMs + 100);
            EXPECT_GE(w.endMs, w.startMs); // zero-length ok at segment edges
            EXPECT_GE(w.startMs, prevEnd - 50); // non-overlapping
            prevEnd = w.endMs;
            const QString word = w.text.trimmed();
            EXPECT_FALSE(word.isEmpty());
            // No leaked special/timestamp tokens (whisper internals).
            EXPECT_EQ(word.contains(QLatin1Char('[')), false) << word.toStdString();
            EXPECT_EQ(word.contains(QLatin1Char('_')), false) << word.toStdString();
        }
    }
    // The JFK fixture ends with the famous last line.
    const QString lower = allText.toLower();
    EXPECT_NE(lower.indexOf(QStringLiteral("country")), -1);
    qInfo() << "STT(" << model << "):" << out.size() << "segments in"
            << elapsedMs << "ms —" << allText.trimmed();
}

// M7.2: transcript segments -> prompt lines (pure formatter used by the
// AI panel to feed local STT output into the feature prompts).
TEST(Stt, TranscriptLinesFormat)
{
    QList<TranscriptSegment> segs;
    TranscriptSegment s1;
    s1.startMs = 0;
    s1.endMs = 1500;
    s1.text = "And so my fellow";
    TranscriptSegment s2;
    s2.startMs = 1533; // 0.511 s -> frame 46 at 30 fps (1 s + 16)
    s2.endMs = 4000;
    s2.text = " Americans, ask not.";
    TranscriptSegment empty;
    empty.startMs = 5000;
    empty.endMs = 6000;
    empty.text = "   ";
    segs = {s1, s2, empty};

    const QStringList lines = transcriptLines(segs, 30.0);
    ASSERT_EQ(lines.size(), 2); // whitespace-only segments dropped
    EXPECT_EQ(lines[0], QStringLiteral("00:00:00:00 And so my fellow"));
    EXPECT_EQ(lines[1], QStringLiteral("00:00:01:16 Americans, ask not.")); // 46 frames = 1s + 16

    // fps=25: 1533 ms -> 38 frames = 1 s + 13 frames
    const QStringList l25 = transcriptLines(segs, 25.0);
    EXPECT_EQ(l25[1], QStringLiteral("00:00:01:13 Americans, ask not."));

    EXPECT_TRUE(transcriptLines({}, 30.0).isEmpty());
}
