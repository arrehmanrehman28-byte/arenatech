// Media probe (file-level) and STT/render availability honesty.
#include <gtest/gtest.h>
#include "media.h"
#include "whisper.h"
#include "render.h"
#include <QTemporaryDir>

using namespace arena;

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
