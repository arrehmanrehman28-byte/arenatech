// M6 AI feature tests: offline parse/validation of model responses.
// Raw inputs mimic real gateway output: code fences, surrounding prose,
// and the corruption classes that must be rejected (count mismatch,
// ordering, out-of-range, bad time codes, oversize titles).
#include <gtest/gtest.h>

#include "aifeatures.h"
#include "prompts.h"

using namespace arena;

namespace {

// A realistic gateway reply: model wraps the JSON in a code fence.
const QString kFence = QStringLiteral("Here you go:\n```json\n%1\n```\n");

} // namespace

// --- batch ranges -------------------------------------------------------------

TEST(AiFeatures, BatchRangesCoverEverythingExactlyOnce)
{
    const auto r1 = aifeatures::batchRanges(0);
    EXPECT_TRUE(r1.isEmpty());
    const auto r2 = aifeatures::batchRanges(40);
    ASSERT_EQ(r2.size(), 1);
    EXPECT_EQ(r2[0], qMakePair(0, 40));
    const auto r3 = aifeatures::batchRanges(81);
    ASSERT_EQ(r3.size(), 3);
    EXPECT_EQ(r3[0], qMakePair(0, 40));
    EXPECT_EQ(r3[1], qMakePair(40, 80));
    EXPECT_EQ(r3[2], qMakePair(80, 81));
    int covered = 0;
    for (const auto &r : r3) {
        EXPECT_EQ(r.first, covered);
        covered = r.second;
    }
    EXPECT_EQ(covered, 81);
}

// --- clock parsing --------------------------------------------------------------

TEST(AiFeatures, ParseClockVariants)
{
    double s = -1;
    EXPECT_TRUE(aifeatures::parseClock("00:01:30", 0.0, &s));
    EXPECT_DOUBLE_EQ(s, 90.0);
    EXPECT_TRUE(aifeatures::parseClock("01:00:00", 0.0, &s));
    EXPECT_DOUBLE_EQ(s, 3600.0);
    EXPECT_FALSE(aifeatures::parseClock("00:01:30:05", 0.0, &s)); // frames need fps
    EXPECT_TRUE(aifeatures::parseClock("00:01:30:15", 30.0, &s));
    EXPECT_DOUBLE_EQ(s, 90.5);
    EXPECT_TRUE(aifeatures::parseClock("00:01:30,15", 30.0, &s));
    EXPECT_DOUBLE_EQ(s, 90.5);
    EXPECT_TRUE(aifeatures::parseClock("00:01:3015", 30.0, &s)); // fused SSFF
    EXPECT_DOUBLE_EQ(s, 90.5);
    EXPECT_FALSE(aifeatures::parseClock("00:61:00", 0.0, &s));   // minute > 59
    EXPECT_FALSE(aifeatures::parseClock("00:01:60:00", 30.0, &s)); // second > 59
    EXPECT_FALSE(aifeatures::parseClock("00:01:30:30", 30.0, &s)); // frame >= fps
    EXPECT_FALSE(aifeatures::parseClock("garbage", 0.0, &s));
    EXPECT_FALSE(aifeatures::parseClock("00:01", 0.0, &s));
}

// --- translation ----------------------------------------------------------------

TEST(AiFeatures, TranslationParsesFencedArray)
{
    const QString raw = kFence.arg(QStringLiteral(R"(["Good evening.", "Line two", "Спасибо"])"));
    QStringList out;
    QString err;
    EXPECT_TRUE(aifeatures::parseTranslation(raw, 3, &out, &err));
    ASSERT_EQ(out.size(), 3);
    EXPECT_EQ(out[0], "Good evening.");
    EXPECT_EQ(out[1], "Line two");
    EXPECT_EQ(out[2], "Спасибо");
}

TEST(AiFeatures, TranslationRejections)
{
    QStringList out;
    QString err;
    // Count mismatch: 3 entries, 2 returned.
    EXPECT_FALSE(aifeatures::parseTranslation(R"(["a","b"])", 3, &out, &err));
    EXPECT_FALSE(err.isEmpty());
    // Non-string member.
    EXPECT_FALSE(aifeatures::parseTranslation(R"(["a", 5, "c"])", 3, &out, &err));
    // Empty string member (a dropped line).
    EXPECT_FALSE(aifeatures::parseTranslation(R"(["a", "", "c"])", 3, &out, &err));
    // No JSON at all.
    EXPECT_FALSE(aifeatures::parseTranslation("I cannot translate that.", 1, &out, &err));
    // Prose around a valid array is fine.
    EXPECT_TRUE(aifeatures::parseTranslation(
        "Sure! ["
        R"("ok")"
        "] done.", 1, &out, &err));
    EXPECT_EQ(out[0], "ok");
    // Multiline entries survive (line breaks preserved inside the string).
    EXPECT_TRUE(aifeatures::parseTranslation(R"(["line one\nline two"])", 1, &out, &err));
    EXPECT_EQ(out[0], "line one\nline two");
}

// --- chapters -------------------------------------------------------------------

TEST(AiFeatures, ChaptersParsesAndValidates)
{
    const QString raw = kFence.arg(
        QStringLiteral(R"([{"start":"00:00:05","title":"Intro"},
                          {"start":"00:00:40","title":"The setup"},
                          {"start":"00:01:20","title":"Payoff"}])"));
    QVector<ChapterSuggestion> out;
    QString err;
    EXPECT_TRUE(aifeatures::parseChapters(raw, 120.0, &out, &err));
    ASSERT_EQ(out.size(), 3);
    EXPECT_DOUBLE_EQ(out[0].startSec, 5.0);
    EXPECT_EQ(out[2].title, "Payoff");
}

TEST(AiFeatures, ChaptersRejections)
{
    QVector<ChapterSuggestion> out;
    QString err;
    // Too many (11 > 10).
    QString many = QStringLiteral("[");
    for (int i = 0; i < 11; ++i) {
        if (i) many += ',';
        many += QStringLiteral(R"({"start":"00:00:0%1","title":"C %1"})")
                    .arg(i % 10)
                    .arg(i);
    }
    many += ']';
    EXPECT_FALSE(aifeatures::parseChapters(many, 3600.0, &out, &err));
    // Out of order.
    EXPECT_FALSE(aifeatures::parseChapters(
        R"([{"start":"00:00:30","title":"B"},{"start":"00:00:10","title":"A"},
            {"start":"00:01:00","title":"C"}])",
        120.0, &out, &err));
    // Beyond video end.
    EXPECT_FALSE(aifeatures::parseChapters(
        R"([{"start":"00:00:01","title":"A"},{"start":"00:00:02","title":"B"},
            {"start":"01:00:00","title":"C"}])",
        60.0, &out, &err));
    // Title too long (> 6 words).
    EXPECT_FALSE(aifeatures::parseChapters(
        R"([{"start":"00:00:01","title":"one two three four five six seven"},
            {"start":"00:00:02","title":"B"},{"start":"00:00:03","title":"C"}])",
        120.0, &out, &err));
    // Bad time code.
    EXPECT_FALSE(aifeatures::parseChapters(
        R"([{"start":"00:00:01","title":"A"},{"start":"nope","title":"B"},
            {"start":"00:00:03","title":"C"}])",
        120.0, &out, &err));
    // Too few (2 < 3).
    EXPECT_FALSE(aifeatures::parseChapters(
        R"([{"start":"00:00:01","title":"A"},{"start":"00:00:02","title":"B"}])",
        120.0, &out, &err));
}

// --- titles ---------------------------------------------------------------------

TEST(AiFeatures, TitleParsesAndValidates)
{
    const QString raw = kFence.arg(QStringLiteral(
        R"({"title":"Editing in a Weekend","description":"We break down a full edit."})"));
    TitleResult t;
    QString err;
    EXPECT_TRUE(aifeatures::parseTitle(raw, &t, &err));
    EXPECT_EQ(t.title, "Editing in a Weekend");
    EXPECT_EQ(t.description, "We break down a full edit.");
}

TEST(AiFeatures, TitleRejections)
{
    TitleResult t;
    QString err;
    // Title over 60 chars.
    const QString longTitle(61, QChar('x'));
    EXPECT_FALSE(aifeatures::parseTitle(
        QString(R"({"title":"%1","description":"fine"})").arg(longTitle), &t, &err));
    // Description over 300 chars.
    const QString longDesc(301, QChar('y'));
    EXPECT_FALSE(aifeatures::parseTitle(
        QString(R"({"title":"ok","description":"%1"})").arg(longDesc), &t, &err));
    // Missing field.
    EXPECT_FALSE(aifeatures::parseTitle(R"({"title":"ok only"})", &t, &err));
    // Empty field.
    EXPECT_FALSE(aifeatures::parseTitle(R"({"title":"","description":"x"})", &t, &err));
    // No JSON.
    EXPECT_FALSE(aifeatures::parseTitle("a title: My Video", &t, &err));
    // Prose before and after the object is fine.
    EXPECT_TRUE(aifeatures::parseTitle(
        "Here is some metadata: "
        R"({"title":"A","description":"B"})"
        " — hope that helps!",
        &t, &err));
    EXPECT_EQ(t.title, "A");
}

// --- cuts -----------------------------------------------------------------------

TEST(AiFeatures, CutsParsesAndValidates)
{
    const QString raw = kFence.arg(
        QStringLiteral(R"([{"start":"00:00:10:00","end":"00:00:12:15","reason":"silence","confidence":0.9},
                          {"start":"00:01:00:00","end":"00:01:01:00","reason":"filler","confidence":0.6}])"));
    QVector<CutSuggestion> out;
    QString err;
    EXPECT_TRUE(aifeatures::parseCuts(raw, 300.0, 30.0, &out, &err));
    ASSERT_EQ(out.size(), 2);
    EXPECT_DOUBLE_EQ(out[0].startSec, 10.0);
    EXPECT_DOUBLE_EQ(out[0].endSec, 12.5);
    EXPECT_EQ(out[0].reason, "silence");
    EXPECT_DOUBLE_EQ(out[0].confidence, 0.9);
    EXPECT_EQ(out[1].reason, "filler");
}

TEST(AiFeatures, CutsRejections)
{
    QVector<CutSuggestion> out;
    QString err;
    // end before start.
    EXPECT_FALSE(aifeatures::parseCuts(
        R"([{"start":"00:00:10:00","end":"00:00:09:00","reason":"silence","confidence":0.5}])",
        300.0, 30.0, &out, &err));
    // Unknown reason.
    EXPECT_FALSE(aifeatures::parseCuts(
        R"([{"start":"00:00:10:00","end":"00:00:12:00","reason":"boredom","confidence":0.5}])",
        300.0, 30.0, &out, &err));
    // Confidence out of range.
    EXPECT_FALSE(aifeatures::parseCuts(
        R"([{"start":"00:00:10:00","end":"00:00:12:00","reason":"silence","confidence":1.7}])",
        300.0, 30.0, &out, &err));
    // Beyond video end.
    EXPECT_FALSE(aifeatures::parseCuts(
        R"([{"start":"00:04:50:00","end":"00:05:00:00","reason":"silence","confidence":0.5}])",
        300.0, 30.0, &out, &err));
    // Bad frame count (>= fps).
    EXPECT_FALSE(aifeatures::parseCuts(
        R"([{"start":"00:00:10:30","end":"00:00:12:00","reason":"silence","confidence":0.5}])",
        300.0, 30.0, &out, &err));
}

// --- prompt builders --------------------------------------------------------------

TEST(AiFeatures, PromptBuildersSubstitutePlaceholders)
{
    QJsonObject e;
    e[QStringLiteral("index")] = 0;
    e[QStringLiteral("start")] = QStringLiteral("00:00:00:00");
    e[QStringLiteral("end")] = QStringLiteral("00:00:02:00");
    e[QStringLiteral("text")] = QStringLiteral("Hello");
    const QString p = aifeatures::translatePrompt({e}, QStringLiteral("ar"));
    EXPECT_NE(p.indexOf(QStringLiteral("ar")), -1);
    EXPECT_NE(p.indexOf(QStringLiteral("Hello")), -1);
    EXPECT_EQ(p.indexOf(QStringLiteral("{targetLang}")), -1);
    EXPECT_EQ(p.indexOf(QStringLiteral("{entries}")), -1);

    const QString c = aifeatures::chaptersPrompt(QStringLiteral("00:00:01 hello"));
    EXPECT_NE(c.indexOf(QStringLiteral("00:00:01 hello")), -1);
    EXPECT_EQ(c.indexOf(QStringLiteral("{transcript}")), -1);

    const QString t = aifeatures::titlePrompt(QStringLiteral("project: demo"));
    EXPECT_NE(t.indexOf(QStringLiteral("project: demo")), -1);
    EXPECT_EQ(t.indexOf(QStringLiteral("{context}")), -1);

    const QString k = aifeatures::cutsPrompt(QStringLiteral("00:00:01 hi"),
                                              QStringLiteral("00:00:05-00:00:08 (3s)"));
    EXPECT_NE(k.indexOf(QStringLiteral("00:00:05-00:00:08 (3s)")), -1);
    EXPECT_EQ(k.indexOf(QStringLiteral("{silences}")), -1);
    EXPECT_EQ(k.indexOf(QStringLiteral("{transcript}")), -1);
}
