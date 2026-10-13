// Media probe (file-level) and STT/render availability honesty.
#include <gtest/gtest.h>
#include "media.h"
#include "whisper.h"
#include "render.h"
#include <QTemporaryDir>
#include <functional>
#include <vector>

using namespace arena;

#if defined(ARENA_TEST_FIXTURES)
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
    // M0 has no compositor; the registry (the contract) is available.
    EXPECT_FALSE(Compositor::isAvailable());
    EXPECT_GT(EffectRegistry::effectTypes().size(), 5);
    EXPECT_TRUE(EffectRegistry::isTransition("transition.cross_dissolve"));
    EXPECT_FALSE(EffectRegistry::isTransition("transform"));
}

TEST(SttBoundary, HonestAvailability)
{
    // whisper.cpp binds in M7; M0 must say so clearly, not fake results.
    EXPECT_FALSE(Transcriber::isAvailable());
    QString err;
    QList<TranscriptSegment> out;
    EXPECT_FALSE(Transcriber::transcribe("a.wav", {}, out, &err, nullptr));
    EXPECT_FALSE(err.isEmpty());
    EXPECT_EQ(Transcriber::models().size(), 3);
}
