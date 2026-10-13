// M5 subtitle tests: SRT/VTT/ASS parse + canonical encode (byte-stable
// round-trips on the sample set), time codes, frame conversion, reading
// speed thresholds, RTL detection, and project JSON round-trip with cues.
#include <gtest/gtest.h>

#include <QFile>
#include <QJsonDocument>

#include "project.h"
#include "subtitles.h"

using namespace arena;

namespace {

bool loadFixture(const QString &name, QString *out)
{
    const QString path = QStringLiteral(ARENA_TEST_FIXTURES) + "/subs/" + name;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    *out = QString::fromUtf8(f.readAll());
    return true;
}

QVector<SubtitleCue> expectedCues()
{
    QVector<SubtitleCue> v;
    SubtitleCue c1{0.0, 2.0, QStringLiteral("Hello world")};
    SubtitleCue c2{2.5, 5.0, QStringLiteral("Second cue\nsecond line")};
    SubtitleCue c3{5.0, 7.25, QStringLiteral("Third cue with a few more words to read here")};
    SubtitleCue c4{3600.0, 3602.5, QStringLiteral("Hour cue")};
    v << c1 << c2 << c3 << c4;
    return v;
}

void expectCuesEqual(const QVector<SubtitleCue> &got, const QVector<SubtitleCue> &want)
{
    ASSERT_EQ(got.size(), want.size());
    for (int i = 0; i < want.size(); ++i) {
        EXPECT_DOUBLE_EQ(got[i].startSec, want[i].startSec) << "cue " << i << " start";
        EXPECT_DOUBLE_EQ(got[i].endSec, want[i].endSec) << "cue " << i << " end";
        EXPECT_EQ(got[i].text, want[i].text) << "cue " << i << " text";
    }
}

} // namespace

// --- byte-stable round trips (sample set) --------------------------------------

TEST(Subtitles, SrtRoundTripByteStable)
{
    QString text;
    ASSERT_TRUE(loadFixture(QStringLiteral("sample.srt"), &text));
    QVector<SubtitleCue> cues;
    QString err;
    ASSERT_TRUE(Subtitles::parseSrt(text, &cues, &err)) << err.toUtf8().constData();
    expectCuesEqual(cues, expectedCues());
    EXPECT_EQ(Subtitles::encodeSrt(cues).toUtf8(), text.toUtf8());
}

TEST(Subtitles, VttRoundTripByteStable)
{
    QString text;
    ASSERT_TRUE(loadFixture(QStringLiteral("sample.vtt"), &text));
    QVector<SubtitleCue> cues;
    QString err;
    ASSERT_TRUE(Subtitles::parseVtt(text, &cues, &err)) << err.toUtf8().constData();
    expectCuesEqual(cues, expectedCues());
    EXPECT_EQ(Subtitles::encodeVtt(cues).toUtf8(), text.toUtf8());
}

TEST(Subtitles, AssRoundTripByteStable)
{
    QString text;
    ASSERT_TRUE(loadFixture(QStringLiteral("sample.ass"), &text));
    QVector<SubtitleCue> cues;
    QString err;
    ASSERT_TRUE(Subtitles::parseAss(text, &cues, &err)) << err.toUtf8().constData();
    expectCuesEqual(cues, expectedCues());
    EXPECT_EQ(Subtitles::encodeAss(cues).toUtf8(), text.toUtf8());
}

// The three formats must agree on the same sample set.
TEST(Subtitles, FormatsAgree)
{
    QString srt, vtt, ass;
    ASSERT_TRUE(loadFixture(QStringLiteral("sample.srt"), &srt));
    ASSERT_TRUE(loadFixture(QStringLiteral("sample.vtt"), &vtt));
    ASSERT_TRUE(loadFixture(QStringLiteral("sample.ass"), &ass));
    QVector<SubtitleCue> a, b, c;
    QString err;
    ASSERT_TRUE(Subtitles::parse(srt, "x.srt", &a, &err));
    ASSERT_TRUE(Subtitles::parse(vtt, "x.vtt", &b, &err));
    ASSERT_TRUE(Subtitles::parse(ass, "x.ass", &c, &err));
    expectCuesEqual(a, expectedCues());
    expectCuesEqual(b, a);
    expectCuesEqual(c, a);
}

// --- time codes -------------------------------------------------------------------

TEST(Subtitles, TimeCodeParseAndFormat)
{
    double s = -1;
    ASSERT_TRUE(Subtitles::parseSrtTime(QStringLiteral("00:00:07,250"), &s));
    EXPECT_DOUBLE_EQ(s, 7.25);
    ASSERT_TRUE(Subtitles::parseSrtTime(QStringLiteral("01:00:02,500"), &s));
    EXPECT_DOUBLE_EQ(s, 3602.5);
    EXPECT_EQ(Subtitles::formatSrtTime(7.25), QStringLiteral("00:00:07,250"));
    EXPECT_EQ(Subtitles::formatSrtTime(3600.0), QStringLiteral("01:00:00,000"));

    ASSERT_TRUE(Subtitles::parseVttTime(QStringLiteral("00:05.500"), &s));
    EXPECT_DOUBLE_EQ(s, 5.5); // MM:SS.mmm accepted
    ASSERT_TRUE(Subtitles::parseVttTime(QStringLiteral("00:00:05,500").replace(',', '.'), &s));
    EXPECT_DOUBLE_EQ(s, 5.5);
    EXPECT_EQ(Subtitles::formatVttTime(3602.5), QStringLiteral("01:00:02.500"));

    ASSERT_TRUE(Subtitles::parseAssTime(QStringLiteral("1:00:02.50"), &s));
    EXPECT_DOUBLE_EQ(s, 3602.5);
    EXPECT_EQ(Subtitles::formatAssTime(7.25), QStringLiteral("0:00:07.25"));

    // Malformed inputs.
    EXPECT_FALSE(Subtitles::parseSrtTime(QStringLiteral("00:00:07.250"), &s)); // wrong sep
    EXPECT_FALSE(Subtitles::parseSrtTime(QStringLiteral("00:60:00,000"), &s)); // minute 60
    EXPECT_FALSE(Subtitles::parseSrtTime(QStringLiteral("--> 00:00:01,000"), &s));
}

TEST(Subtitles, TimeWithoutFractionAccepted)
{
    // SRT without the ,mmm fraction is common in the wild; accept it.
    double s = -1;
    EXPECT_TRUE(Subtitles::parseSrtTime(QStringLiteral("00:00:07"), &s));
    EXPECT_DOUBLE_EQ(s, 7.0);
}

TEST(Subtitles, FrameConversion)
{
    EXPECT_EQ(Subtitles::secToFrame(0.0, 30.0), 0);
    EXPECT_EQ(Subtitles::secToFrame(2.5, 30.0), 75);
    EXPECT_EQ(Subtitles::secToFrame(1.0 / 30.0, 30.0), 1);
    EXPECT_DOUBLE_EQ(Subtitles::frameToSec(75, 30.0), 2.5);
    EXPECT_DOUBLE_EQ(Subtitles::frameToSec(0, 30.0), 0.0);
    EXPECT_EQ(Subtitles::secToFrame(1.5, 0.0), 0); // bad fps guard
}

// --- reading speed ------------------------------------------------------------------

TEST(Subtitles, ReadingSpeedThresholds)
{
    // 20 chars over 2 s (10 cps): neither threshold tripped.
    const QString ok(20, QLatin1Char('a'));
    Subtitles::CueWarning w = Subtitles::checkCue(ok, 0.0, 2.0);
    EXPECT_FALSE(w.any());
    EXPECT_EQ(w.visibleChars, 20);
    EXPECT_DOUBLE_EQ(w.cps, 10.0);

    // 42 chars is still fine length-wise (threshold is > 42).
    Subtitles::CueWarning len42 = Subtitles::checkCue(QString(42, QLatin1Char('a')), 0.0, 3.0);
    EXPECT_FALSE(len42.tooLong);

    // 43 chars -> tooLong.
    Subtitles::CueWarning long1 = Subtitles::checkCue(QString(43, QLatin1Char('a')), 0.0, 3.0);
    EXPECT_TRUE(long1.tooLong);

    // 18 chars in 1 s -> 18 cps > 17 -> tooFast.
    Subtitles::CueWarning fast = Subtitles::checkCue(QString(18, QLatin1Char('b')), 0.0, 1.0);
    EXPECT_TRUE(fast.tooFast);
    EXPECT_FALSE(fast.tooLong);

    // 17 chars in 1 s -> exactly 17 cps, not > 17.
    Subtitles::CueWarning exact = Subtitles::checkCue(QString(17, QLatin1Char('c')), 0.0, 1.0);
    EXPECT_FALSE(exact.tooFast);

    // Spaces are not counted.
    Subtitles::CueWarning spaced = Subtitles::checkCue("a a a a a", 0.0, 1.0);
    EXPECT_EQ(spaced.visibleChars, 5);

    // Zero-duration cue: cps undefined, length still checked.
    Subtitles::CueWarning zero = Subtitles::checkCue(QString(50, QLatin1Char('d')), 1.0, 1.0);
    EXPECT_TRUE(zero.tooLong);
    EXPECT_FALSE(zero.tooFast);
    EXPECT_DOUBLE_EQ(zero.cps, 0.0);
}

// --- direction ----------------------------------------------------------------------------

TEST(Subtitles, RtlLanguageDetection)
{
    EXPECT_TRUE(Subtitles::isRtlLanguage(QStringLiteral("ar")));
    EXPECT_TRUE(Subtitles::isRtlLanguage(QStringLiteral("ur")));
    EXPECT_TRUE(Subtitles::isRtlLanguage(QStringLiteral("ar-PK")));
    EXPECT_TRUE(Subtitles::isRtlLanguage(QStringLiteral("en-Arab")));
    EXPECT_TRUE(Subtitles::isRtlLanguage(QStringLiteral("he-IL")));
    EXPECT_FALSE(Subtitles::isRtlLanguage(QStringLiteral("en")));
    EXPECT_FALSE(Subtitles::isRtlLanguage(QStringLiteral("zh")));
    EXPECT_FALSE(Subtitles::isRtlLanguage(QStringLiteral("")));
}

TEST(Subtitles, RtlTextDetection)
{
    EXPECT_TRUE(Subtitles::textIsRtl(QString::fromUtf8("\xd8\xa8\xd9\x84\xd8\xa7")));   // بلا (Arabic)
    EXPECT_TRUE(Subtitles::textIsRtl(QString::fromUtf8("\xd9\x87\xd9\x84\xd9\x88")));   // ہلو (Urdu)
    // First strong character decides: 'h' (LTR) comes before the Arabic char.
    EXPECT_FALSE(Subtitles::textIsRtl(QStringLiteral("hello \u0635")));
    EXPECT_FALSE(Subtitles::textIsRtl(QStringLiteral("Hello world")));
    EXPECT_FALSE(Subtitles::textIsRtl(QStringLiteral("   ")));
    EXPECT_FALSE(Subtitles::textIsRtl(QStringLiteral("12345"))); // no strong characters
}

// --- parse errors -------------------------------------------------------------------------

TEST(Subtitles, ParseErrors)
{
    QVector<SubtitleCue> cues;
    QString err;
    // VTT without header.
    EXPECT_FALSE(Subtitles::parseVtt(QStringLiteral("00:00:00.000 --> 00:00:01.000\nHi\n"), &cues, &err));
    // SRT with nothing.
    EXPECT_FALSE(Subtitles::parseSrt(QStringLiteral("\n\n  \n"), &cues, &err));
    // ASS without Dialogue lines.
    EXPECT_FALSE(Subtitles::parseAss(QStringLiteral("[Events]\nFormat: Layer, Start, End, Text\n"),
                                     &cues, &err));
    // SRT skips a malformed block but keeps the good one.
    err.clear();
    const QString mixed =
        QStringLiteral("1\r\nbad timing\r\n\r\n2\r\n00:00:00,000 --> 00:00:01,000\r\nGood\r\n\r\n");
    ASSERT_TRUE(Subtitles::parseSrt(mixed, &cues, &err));
    ASSERT_EQ(cues.size(), 1);
    EXPECT_EQ(cues.first().text, QStringLiteral("Good"));
    EXPECT_NE(err, QStringLiteral("")); // reports the skipped block
}

// --- project round trip ---------------------------------------------------------------------

TEST(ProjectModel, SubtitleTrackRoundTripWithCues)
{
    Project p;
    SubtitleTrack t;
    t.id = "s1";
    t.language = "ur";
    t.name = "Urdu captions";
    t.burnIn = true;
    TimelineCue c;
    c.startFrame = 10;
    c.endFrame = 40;
    c.text = QString::fromUtf8("\xd8\xa7\xd9\x85\xd8\xb1\xd9\x88"); // "امرو"
    t.cues.append(c);
    p.subtitleTracks.append(t);

    const QJsonObject o = p.toJson();
    Project q = Project::fromJson(o);
    ASSERT_EQ(q.subtitleTracks.size(), 1);
    const SubtitleTrack &u = q.subtitleTracks.first();
    EXPECT_EQ(u.id, "s1");
    EXPECT_EQ(u.language, "ur");
    EXPECT_TRUE(u.burnIn);
    ASSERT_EQ(u.cues.size(), 1);
    EXPECT_EQ(u.cues.first().startFrame, 10);
    EXPECT_EQ(u.cues.first().endFrame, 40);
    EXPECT_EQ(u.cues.first().text, c.text);

    // Old-schema file without the new fields still loads (additive change).
    QJsonObject oldTrack = o.value("subtitleTracks").toArray().first().toObject();
    oldTrack.remove("burnIn");
    oldTrack.remove("cues");
    QJsonObject oldProject = o;
    QJsonArray arr;
    arr.append(oldTrack);
    oldProject.insert("subtitleTracks", arr);
    Project r = Project::fromJson(oldProject);
    ASSERT_EQ(r.subtitleTracks.size(), 1);
    EXPECT_FALSE(r.subtitleTracks.first().burnIn);
    EXPECT_TRUE(r.subtitleTracks.first().cues.isEmpty());
}
