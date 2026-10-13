// M3 render tests: deterministic compositor pipeline + golden frame +
// LRU cache. All sources are synthetic (injected fetcher), so results are
// exact — no tolerance needed.
#include <gtest/gtest.h>

#include "compositor.h"
#include "render.h"

#include <memory>

using namespace arena;

namespace {

// 64x36 synthetic source: deterministic RGB pattern per (source, frame).
// Same pattern for every source name (tests address clips by geometry).
QImage synthFrame(int frame)
{
    QImage img(64, 36, QImage::Format_RGB32);
    for (int x = 0; x < 64; ++x)
        for (int y = 0; y < 36; ++y) {
            const int r = (x + frame) & 0xFF;
            const int g = (y * 7 + frame * 3) & 0xFF;
            const int b = (128 + frame) & 0xFF;
            img.setPixel(x, y, qRgb(r, g, b));
        }
    return img;
}

QRgb synthPixel(int x, int y, int frame)
{
    return qRgb((x + frame) & 0xFF, (y * 7 + frame * 3) & 0xFF, (128 + frame) & 0xFF);
}

// Project: 64x36 canvas (1:1 with the synthetic frames — no resampling),
// single video track, 30 fps.
std::unique_ptr<Project> makeProject()
{
    auto p = std::make_unique<Project>();
    p->width = 64;
    p->height = 36;
    p->fps = 30.0;
    Track t;
    t.id = QStringLiteral("V1");
    t.type = TrackType::Video;
    t.name = QStringLiteral("V1");
    p->tracks.append(t);
    return p;
}

void addClip(Project &p, const QString &source, qint64 offset, qint64 in, qint64 out,
             QJsonObject effects = {})
{
    Clip c;
    c.id = QStringLiteral("clip_") + source + QStringLiteral("_") + QString::number(offset);
    c.sourceRef = source;
    c.in = in;
    c.out = out;
    c.offset = offset;
    c.effects = std::move(effects);
    p.tracks.first().clips.append(c);
}

const Compositor::FrameFetcher kFetch = [](const QString &, qint64 f) {
    return synthFrame(int(f));
};

int lum(QRgb p)
{
    return int(0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p) + 0.5);
}

} // namespace

// --- identity / geometry ------------------------------------------------------

TEST(Compositor, IdentityRendersSourceFrame)
{
    auto p = makeProject();
    addClip(*p, QStringLiteral("A"), 0, 0, 60);
    const QImage out = Compositor::renderFrame(*p, 5, kFetch);
    EXPECT_EQ(out.size(), QSize(64, 36));
    const QRgb got = out.pixel(10, 5);
    const QRgb want = synthPixel(10, 5, 5);
    EXPECT_EQ(qRgb(qRed(got), qGreen(got), qBlue(got)), qRgb(qRed(want), qGreen(want), qBlue(want)));
    EXPECT_EQ(qAlpha(got), 255);
}

TEST(Compositor, EmptyTimelineIsTransparent)
{
    auto p = makeProject();
    const QImage out = Compositor::renderFrame(*p, 5, kFetch);
    EXPECT_TRUE(out.isNull() == false);
    EXPECT_EQ(qAlpha(out.pixel(32, 18)), 0);
}

TEST(Compositor, OpacityHalvesAlpha)
{
    auto p = makeProject();
    QJsonObject fx;
    fx.insert(QStringLiteral("opacity"), QJsonObject{{QStringLiteral("value"), 0.5}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, fx);
    const QImage out = Compositor::renderFrame(*p, 5, kFetch);
    EXPECT_EQ(qAlpha(out.pixel(10, 5)), 128); // int(255*0.5+0.5)
}

// Square project + square source so rotation is 1:1 (no resampling).
QImage synthSquare(int frame)
{
    QImage img(36, 36, QImage::Format_RGB32);
    for (int x = 0; x < 36; ++x)
        for (int y = 0; y < 36; ++y)
            img.setPixel(x, y, qRgb((x + frame) & 0xFF, (y * 7 + frame * 3) & 0xFF,
                                    (128 + frame) & 0xFF));
    return img;
}

TEST(Compositor, Rotation90SwapsCorners)
{
    auto p = std::make_unique<Project>();
    p->width = 36;
    p->height = 36;
    p->fps = 30.0;
    Track t;
    t.id = QStringLiteral("V1");
    t.type = TrackType::Video;
    t.name = QStringLiteral("V1");
    p->tracks.append(t);
    addClip(*p, QStringLiteral("A"), 0, 0, 60);
    p->tracks.first().clips.first().effects =
        QJsonObject{{QStringLiteral("transform"),
                     QJsonObject{{QStringLiteral("rotation"), 90.0}}}};
    const Compositor::FrameFetcher fetch = [](const QString &, qint64 f) {
        return synthSquare(int(f));
    };
    const QImage out = Compositor::renderFrame(*p, 0, fetch);
    // 90 deg clockwise about center: source (x,y) -> (35-y, x).
    // Source (0,0) = qRgb(0, 0, 128) at frame 0 lands at (35, 0).
    const QRgb got = out.pixel(35, 0);
    EXPECT_EQ(qRed(got), 0);
    EXPECT_EQ(qGreen(got), 0);
    EXPECT_EQ(qBlue(got), 128);
}

// --- color --------------------------------------------------------------------

TEST(Compositor, ExposurePlusOneDoublesClamped)
{
    auto p = makeProject();
    QJsonObject fx;
    fx.insert(QStringLiteral("color"),
              QJsonObject{{QStringLiteral("exposure"), 1.0}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, fx);
    const QImage out = Compositor::renderFrame(*p, 5, kFetch);
    const QRgb got = out.pixel(10, 5);
    // r=15*2=30, g=50*2=100, b=133*2=266->255
    EXPECT_EQ(qRed(got), 30);
    EXPECT_EQ(qGreen(got), 100);
    EXPECT_EQ(qBlue(got), 255);
}

TEST(Compositor, ContrastTwoPushesToBlack)
{
    auto p = makeProject();
    QJsonObject fx;
    fx.insert(QStringLiteral("color"),
              QJsonObject{{QStringLiteral("contrast"), 2.0}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, fx);
    const QImage out = Compositor::renderFrame(*p, 5, kFetch);
    const QRgb got = out.pixel(10, 5);
    // r=15 -> (15-128)*2+128 = -98 -> 0
    // g=50 -> (50-128)*2+128 = -28 -> 0
    // b=133 -> (133-128)*2+128 = 138
    EXPECT_EQ(qRed(got), 0);
    EXPECT_EQ(qGreen(got), 0);
    EXPECT_EQ(qBlue(got), 138);
}

TEST(Compositor, SaturationZeroIsGray)
{
    auto p = makeProject();
    QJsonObject fx;
    fx.insert(QStringLiteral("color"),
              QJsonObject{{QStringLiteral("saturation"), 0.0}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, fx);
    const QImage out = Compositor::renderFrame(*p, 5, kFetch);
    const QRgb got = out.pixel(10, 5);
    const int l = lum(synthPixel(10, 5, 5));
    EXPECT_EQ(qRed(got), l);
    EXPECT_EQ(qGreen(got), l);
    EXPECT_EQ(qBlue(got), l);
}

TEST(Compositor, TemperatureWarmShiftsRedBlue)
{
    auto p = makeProject();
    QJsonObject fx;
    fx.insert(QStringLiteral("color"),
              QJsonObject{{QStringLiteral("temperature"), 100.0}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, fx);
    const QImage out = Compositor::renderFrame(*p, 5, kFetch);
    const QRgb got = out.pixel(10, 5);
    const QRgb base = synthPixel(10, 5, 5);
    EXPECT_EQ(qRed(got), int(qRed(base)) + 24);
    EXPECT_EQ(qGreen(got), qGreen(base));
    EXPECT_EQ(qBlue(got), int(qBlue(base)) - 24);
}

// --- transitions ----------------------------------------------------------------

TEST(Compositor, CrossDissolveHalfIsMean)
{
    auto p = makeProject();
    QJsonObject fx;
    fx.insert(QStringLiteral("transition"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("cross_dissolve")},
                          {QStringLiteral("duration"), 20}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, fx);
    addClip(*p, QStringLiteral("B"), 60, 0, 60);
    // Frame 50: left = 10, p = 0.5. Incoming B frame = 0.5*20 = 10.
    const QImage out = Compositor::renderFrame(*p, 50, kFetch);
    const QRgb a = synthPixel(10, 5, 50); // A at source frame 50
    const QRgb b = synthPixel(10, 5, 10); // B at source frame 10
    const QRgb got = out.pixel(10, 5);
    EXPECT_EQ(qRed(got), int(qRed(a) * 0.5 + qRed(b) * 0.5 + 0.5));
    EXPECT_EQ(qGreen(got), int(qGreen(a) * 0.5 + qGreen(b) * 0.5 + 0.5));
    EXPECT_EQ(qBlue(got), int(qBlue(a) * 0.5 + qBlue(b) * 0.5 + 0.5));
}

TEST(Compositor, FadeToBlackEnd)
{
    auto p = makeProject();
    QJsonObject fx;
    fx.insert(QStringLiteral("transition"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("fade_to_black")},
                          {QStringLiteral("duration"), 10}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, fx);
    // Frame 59: left = 1, p = 0.9 -> out = a * 0.1
    const QImage out = Compositor::renderFrame(*p, 59, kFetch);
    const QRgb a = synthPixel(10, 5, 59);
    const QRgb got = out.pixel(10, 5);
    EXPECT_EQ(qRed(got), int(qRed(a) * 0.1 + 0.5));
    EXPECT_EQ(qGreen(got), int(qGreen(a) * 0.1 + 0.5));
    EXPECT_EQ(qBlue(got), int(qBlue(a) * 0.1 + 0.5));
}

TEST(Compositor, WipeHalfSplitsColumns)
{
    auto p = makeProject();
    QJsonObject fx;
    fx.insert(QStringLiteral("transition"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("wipe")},
                          {QStringLiteral("duration"), 20}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, fx);
    addClip(*p, QStringLiteral("B"), 60, 0, 60);
    // Frame 50 -> p = 0.5, boundary x = 32.
    const QImage out = Compositor::renderFrame(*p, 50, kFetch);
    const QRgb a = synthPixel(5, 5, 50);   // left side: A
    const QRgb b = synthPixel(59, 5, 10);  // right side: B
    const QRgb gotL = out.pixel(5, 5);
    const QRgb gotR = out.pixel(59, 5);
    EXPECT_EQ(qRgb(qRed(gotL), qGreen(gotL), qBlue(gotL)), qRgb(qRed(a), qGreen(a), qBlue(a)));
    EXPECT_EQ(qRgb(qRed(gotR), qGreen(gotR), qBlue(gotR)), qRgb(qRed(b), qGreen(b), qBlue(b)));
}

TEST(Compositor, PushShiftsLayers)
{
    auto p = makeProject();
    QJsonObject fx;
    fx.insert(QStringLiteral("transition"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("push")},
                          {QStringLiteral("duration"), 40}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, fx);
    addClip(*p, QStringLiteral("B"), 60, 0, 60);
    // Frame 45: left = 15, p = (40-15)/40 = 0.625.
    // Outgoing shifted -p*W = -40 px: pixel x=10 shows A at x=50, source frame 45.
    const QImage out = Compositor::renderFrame(*p, 45, kFetch);
    const QRgb a = synthPixel(50, 5, 45);
    const QRgb got = out.pixel(10, 5);
    EXPECT_EQ(qRgb(qRed(got), qGreen(got), qBlue(got)), qRgb(qRed(a), qGreen(a), qBlue(a)));
}

// --- LRU cache -----------------------------------------------------------------

TEST(FrameCache, LruEvictsOldestWithinCap)
{
    Compositor::FrameCache cache(768); // holds exactly three 8x8 ARGB32 frames
    auto mk = [](const QString &seed) {
        QImage img(8, 8, QImage::Format_ARGB32);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                img.setPixel(x, y, qRgb(int(seed[0].unicode() + x), int(seed[1].unicode() + y), 200));
        return img;
    };
    cache.put(QStringLiteral("A"), mk(QStringLiteral("A1")));
    cache.put(QStringLiteral("B"), mk(QStringLiteral("B2")));
    cache.put(QStringLiteral("C"), mk(QStringLiteral("C3")));
    EXPECT_EQ(cache.count(), 3);
    EXPECT_LE(cache.usedBytes(), 768);
    // Touch A so B becomes oldest.
    QImage tmp;
    ASSERT_TRUE(cache.get(QStringLiteral("A"), &tmp));
    cache.put(QStringLiteral("D"), mk(QStringLiteral("D4")));
    EXPECT_EQ(cache.count(), 3);
    EXPECT_FALSE(cache.get(QStringLiteral("B"), &tmp)); // evicted
    EXPECT_TRUE(cache.get(QStringLiteral("D"), &tmp));
    EXPECT_EQ(tmp.size(), QSize(8, 8));
}

TEST(FrameCache, CachedFrameEqualsDirect)
{
    auto p = makeProject();
    addClip(*p, QStringLiteral("A"), 0, 0, 60);
    const QImage direct = Compositor::renderFrame(*p, 7, kFetch);
    Compositor::FrameCache cache;
    cache.put(QStringLiteral("k"), direct);
    QImage cached;
    ASSERT_TRUE(cache.get(QStringLiteral("k"), &cached));
    EXPECT_EQ(cached.size(), direct.size());
    for (int y = 0; y < direct.height(); ++y)
        for (int x = 0; x < direct.width(); ++x)
            EXPECT_EQ(cached.pixel(x, y), direct.pixel(x, y));
}

TEST(FrameCache, HitAndMissCounters)
{
    Compositor::FrameCache cache(1 << 20);
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::red);
    cache.put(QStringLiteral("k"), img);
    QImage tmp;
    EXPECT_TRUE(cache.get(QStringLiteral("k"), &tmp));
    EXPECT_FALSE(cache.get(QStringLiteral("nope"), &tmp));
    EXPECT_EQ(cache.hits(), 1u);
    EXPECT_EQ(cache.misses(), 1u);
}

// --- golden frame ----------------------------------------------------------------

// Deterministic scene: A (0-59) with cross_dissolve(20) + exposure on B.
// Render frame 55 (mid-dissolve, p = 0.75) and compare pixel-exactly with
// tests/fixtures/golden_m3.png. Set ARENA_GOLDEN_UPDATE=1 to regenerate.
TEST(Compositor, GoldenFrame)
{
    auto p = makeProject();
    QJsonObject aFx;
    aFx.insert(QStringLiteral("transition"),
               QJsonObject{{QStringLiteral("type"), QStringLiteral("cross_dissolve")},
                           {QStringLiteral("duration"), 20}});
    QJsonObject bFx;
    bFx.insert(QStringLiteral("color"), QJsonObject{{QStringLiteral("exposure"), 0.5}});
    addClip(*p, QStringLiteral("A"), 0, 0, 60, aFx);
    addClip(*p, QStringLiteral("B"), 60, 0, 60, bFx);
    const QImage out = Compositor::renderFrame(*p, 55, kFetch);

#if defined(ARENA_TEST_FIXTURES)
    const QString path = QStringLiteral(ARENA_TEST_FIXTURES) + "/golden_m3.png";
#else
    const QString path = QStringLiteral("tests/fixtures/golden_m3.png");
#endif
    if (qEnvironmentVariableIsSet("ARENA_GOLDEN_UPDATE")) {
        ASSERT_TRUE(out.save(path));
        GTEST_SKIP() << "golden written";
    }
    QImage g;
    ASSERT_TRUE(g.load(path)) << "missing golden: " << path.toUtf8().constData();
    ASSERT_EQ(g.size(), out.size());
    int diffs = 0;
    for (int y = 0; y < out.height() && diffs < 5; ++y)
        for (int x = 0; x < out.width() && diffs < 5; ++x)
            if (g.pixel(x, y) != out.pixel(x, y)) ++diffs;
    EXPECT_EQ(diffs, 0) << "first diffing pixel region differs from golden";
}

// --- registry contract (M0) stays intact ----------------------------------------

TEST(EffectRegistry, TransitionsAreRecognized)
{
    EXPECT_TRUE(EffectRegistry::isTransition(QStringLiteral("transition.cross_dissolve")));
    EXPECT_FALSE(EffectRegistry::isTransition(QStringLiteral("transform")));
    EXPECT_GT(EffectRegistry::effectTypes().size(), 5);
}
