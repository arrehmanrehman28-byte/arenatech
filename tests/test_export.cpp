// M4 export tests: pure logic (presets/estimates/codec selection) plus the
// full encode pipeline against synthetic sources (deterministic, no real
// media needed). The FFmpeg-dependent tests skip in non-FFmpeg builds.
#include <gtest/gtest.h>

#include <QDir>
#include <QUuid>

#include "exporter.h"
#include "media.h"

using namespace arena;

namespace {

std::unique_ptr<Project> makeProject()
{
    auto p = std::make_unique<Project>();
    p->width = 320;
    p->height = 180;
    p->fps = 30.0;
    Track t;
    t.id = QStringLiteral("V1");
    t.type = TrackType::Video;
    t.name = QStringLiteral("V1");
    p->tracks.append(t);
    return p;
}

// Solid-color source whose brightness advances with the frame index:
// R = frame % 256, G = 100, B = 100. Easy to verify after encode.
QImage synthFrame(qint64 frame)
{
    QImage img(320, 180, QImage::Format_RGB32);
    const int r = int(frame) & 0xFF;
    for (int y = 0; y < 180; ++y)
        for (int x = 0; x < 320; ++x)
            img.setPixel(x, y, qRgb(r, 100, 100));
    return img;
}

const Compositor::FrameFetcher kFetch = [](const QString &, qint64 f) {
    return synthFrame(f);
};

// Sine-wave audio (440 Hz) at the requested rate/channels.
bool synthAudio(const QString &, qint64 inFrame, qint64 outFrame, int rate, int channels,
                QVector<float> *out)
{
    const double fps = 30.0;
    const int n = int((outFrame - inFrame) * rate / fps);
    out->resize(n * channels);
    const double phase0 = 2.0 * M_PI * 440.0 * double(inFrame) / fps;
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < n; ++i)
            (*out)[i * channels + c] = 0.5f * std::sin(float(phase0 + 2.0 * M_PI * 440.0 * i / rate));
    return true;
}

QString tmpFile(const QString &name)
{
    const QString dir = QDir::tempPath() + "/arena_export_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(dir);
    return dir + "/" + name;
}

} // namespace

// --- pure logic ---------------------------------------------------------------

TEST(Exporter, PresetsCoverCodecs)
{
    const QStringList names = Exporter::presetNames();
    ASSERT_GE(names.size(), 3);
    auto p = makeProject();
    for (const QString &n : names) {
        const ExportSettings s = Exporter::preset(n, *p);
        EXPECT_FALSE(s.videoCodec.isEmpty());
        EXPECT_TRUE(s.container == "mp4" || s.container == "webm");
        EXPECT_GT(s.crf, 0.0);
    }
    // The 720 preset scales down a 1080p project.
    p->width = 1920;
    p->height = 1080;
    const ExportSettings s720 = Exporter::preset("mp4-h264-web-720", *p);
    EXPECT_LT(s720.scalePercent, 100);
    EXPECT_GT(s720.scalePercent, 60);
}

TEST(Exporter, EstimateIsMonotonicInQuality)
{
    auto p = makeProject();
    Clip c;
    c.id = QStringLiteral("c1");
    c.sourceRef = QStringLiteral("a.mp4");
    c.in = 0;
    c.out = 90; // 3 s @ 30 fps
    c.offset = 0;
    p->tracks.first().clips.append(c);
    ExportSettings hi = Exporter::preset("mp4-h264-high", *p);
    ExportSettings bal = Exporter::preset("mp4-h264-balanced", *p);
    const ExportEstimate eHi = Exporter::estimate(hi, *p);
    const ExportEstimate eBal = Exporter::estimate(bal, *p);
    EXPECT_GT(eHi.videoBytes, eBal.videoBytes);
    EXPECT_GT(eHi.audioBytes, 0);
    EXPECT_GT(eHi.estEncodeSeconds, 0.0);
    // Empty project -> zero estimate.
    auto empty = makeProject();
    EXPECT_EQ(Exporter::estimate(hi, *empty).totalBytes(), 0);
}

TEST(Exporter, CodecSelectionFallback)
{
    // HW preferred when enabled and present.
    EXPECT_EQ(Exporter::selectVideoEncoder("h264", {"libx264", "h264_qsv"}, true), "h264_qsv");
    // HW disabled -> software.
    EXPECT_EQ(Exporter::selectVideoEncoder("h264", {"libx264", "h264_qsv"}, false), "libx264");
    // Neither HW nor the SW fallback present -> empty.
    EXPECT_TRUE(Exporter::selectVideoEncoder("hevc", {"libx264"}, true).isEmpty());
    // Unknown codec -> empty chain.
    EXPECT_TRUE(Exporter::selectVideoEncoder("nope", {"libx264"}, true).isEmpty());
    // VP9 is software-only.
    EXPECT_EQ(Exporter::selectVideoEncoder("vp9", {"libvpx-vp9"}, false), "libvpx-vp9");
    // Audio chains.
    EXPECT_EQ(Exporter::selectAudioEncoder("aac", {"aac"}), "aac");
    EXPECT_TRUE(Exporter::selectAudioEncoder("opus", {"aac"}).isEmpty());
}

// --- full pipeline (FFmpeg) -----------------------------------------------------

TEST(Exporter, ExportMp4H264)
{
    if (!Exporter::isAvailable()) {
        GTEST_SKIP() << "FFmpeg build required";
    }
    auto p = makeProject();
    // Two clips with a cross dissolve at the tail of the first (audio
    // crossfade path included).
    Clip a;
    a.id = "a";
    a.sourceRef = "a.mp4";
    a.in = 0;
    a.out = 30;
    a.offset = 0;
    a.effects = QJsonObject{{"transition", QJsonObject{{"type", "cross_dissolve"},
                                                       {"duration", 6}}}};
    Clip b;
    b.id = "b";
    b.sourceRef = "b.mp4";
    b.in = 0;
    b.out = 30;
    b.offset = 30;
    p->tracks.first().clips.append(a);
    p->tracks.first().clips.append(b);

    const QString out = tmpFile("m4.mp4");
    ExportSettings settings = Exporter::preset("mp4-h264-balanced", *p);
    settings.outputPath = out;
    qint64 lastProgress = -1;
    const ExportJobResult r = Exporter::exportProject(
        *p, settings, kFetch,
        [](const QString &src, qint64 i, qint64 o, int rate, int ch, QVector<float> *buf) {
            return synthAudio(src, i, o, rate, ch, buf);
        },
        [&](qint64 done, qint64) { lastProgress = done; }, nullptr);
    EXPECT_TRUE(r.ok) << r.error.toUtf8().constData();
    EXPECT_EQ(r.framesWritten, 60);
    EXPECT_NEAR(r.durationSec, 2.0, 0.01);
    EXPECT_EQ(r.encoder, "libx264");
    EXPECT_GT(r.bytesWritten, 10 * 1024);
    EXPECT_EQ(lastProgress, 60);
    EXPECT_TRUE(QFileInfo(out).exists());

    // The output must be a valid, playable 2 s file at project size.
    MediaInfo info;
    QString err;
    ASSERT_TRUE(MediaProbe::probeFull(out, &info, &err)) << err.toUtf8().constData();
    EXPECT_EQ(info.width, 320);
    EXPECT_EQ(info.height, 180);
    EXPECT_NEAR(double(info.durationMs) / 1000.0, 2.0, 0.05);
    EXPECT_TRUE(info.hasVideo);
    EXPECT_TRUE(info.hasAudio);
    QDir().remove(QFileInfo(out).path());
}

TEST(Exporter, ExportCancelledLeavesNoFile)
{
    if (!Exporter::isAvailable()) {
        GTEST_SKIP() << "FFmpeg build required";
    }
    auto p = makeProject();
    Clip a;
    a.id = "a";
    a.sourceRef = "a.mp4";
    a.in = 0;
    a.out = 90;
    a.offset = 0;
    p->tracks.first().clips.append(a);

    const QString out = tmpFile("cancel.mp4");
    int calls = 0;
    const ExportJobResult r = Exporter::exportProject(
        *p,
        ExportSettings{out, "mp4", "h264", "aac", 23.0, 128000.0, 100, 30.0, false},
        kFetch, nullptr, nullptr,
        [&]() { return ++calls >= 8; });
    EXPECT_TRUE(r.cancelled);
    EXPECT_FALSE(r.ok);
    EXPECT_LT(r.framesWritten, 90);
    EXPECT_FALSE(QFileInfo(out).exists()); // partial file removed
}

// Exit criterion: a batch of three presets on one short project completes
// with playable outputs and coherent sizes.
TEST(Exporter, BatchThreePresets)
{
    if (!Exporter::isAvailable()) {
        GTEST_SKIP() << "FFmpeg build required";
    }
    auto p = makeProject();
    Clip a;
    a.id = "a";
    a.sourceRef = "a.mp4";
    a.in = 0;
    a.out = 30; // 1 s project
    a.offset = 0;
    p->tracks.first().clips.append(a);

    const QString dir =
        QDir::tempPath() + "/arena_export_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    ASSERT_TRUE(QDir().mkpath(dir));

    const QStringList presets = {"mp4-h264-high", "mp4-h264-balanced", "webm-vp9"};
    qint64 highBytes = 0, balBytes = 0, webmBytes = 0;
    for (const QString &name : presets) {
        ExportSettings s = Exporter::preset(name, *p);
        s.outputPath = dir + "/" + name + (s.container == "webm" ? ".webm" : ".mp4");
        s.hwEncode = false; // deterministic software path in CI
        const ExportJobResult r = Exporter::exportProject(
            *p, s, kFetch,
            [](const QString &src, qint64 i, qint64 o, int rate, int ch, QVector<float> *buf) {
                return synthAudio(src, i, o, rate, ch, buf);
            },
            nullptr, nullptr);
        ASSERT_TRUE(r.ok) << name.toUtf8().constData() << " failed: " << r.error.toUtf8().constData();
        ASSERT_EQ(r.framesWritten, 30);
        MediaInfo info;
        QString err;
        ASSERT_TRUE(MediaProbe::probeFull(s.outputPath, &info, &err))
            << name.toUtf8().constData() << " unplayable: " << err.toUtf8().constData();
        EXPECT_NEAR(double(info.durationMs) / 1000.0, 1.0, 0.06);
        if (name == "mp4-h264-high") highBytes = r.bytesWritten;
        if (name == "mp4-h264-balanced") balBytes = r.bytesWritten;
        if (name == "webm-vp9") webmBytes = r.bytesWritten;
    }
    // Higher quality (lower crf) must not be smaller.
    EXPECT_GE(highBytes, balBytes);
    EXPECT_GT(webmBytes, 0);
    QDir().remove(dir);
}
