// NL command action validator: strict schema, no silent execution.
#include <gtest/gtest.h>
#include "actionvalidator.h"
#include <QJsonDocument>

static bool hasSubstr(const QStringList &errors, const char *needle)
{
    return errors.join(' ').contains(QLatin1String(needle));
}

using namespace arena;

static QJsonArray arr(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).array();
}

TEST(ActionValidator, AcceptsValidTransitionAction)
{
    const auto r = actionValidator::validateActions(
        arr(R"([{"op":"add_transition","target":{"type":"clip","index":-1},"transition":"fade_to_black"}])"));
    EXPECT_TRUE(r.ok) << r.errors.join("; ").toStdString();
}

TEST(ActionValidator, AcceptsExportWithSubtitles)
{
    const auto r = actionValidator::validateActions(
        arr(R"([{"op":"export","preset":"youtube_1080p","subtitles":"sidecar"}])"));
    EXPECT_TRUE(r.ok) << r.errors.join("; ").toStdString();
}

TEST(ActionValidator, AcceptsMultiActionList)
{
    const auto r = actionValidator::validateActions(arr(R"([
        {"op":"set_clip_speed","target":{"type":"clip","index":0},"speed":1.5},
        {"op":"add_marker","time":120,"label":"Drop"},
        {"op":"export","preset":"instagram_reels","subtitles":"burn"}
    ])"));
    EXPECT_TRUE(r.ok) << r.errors.join("; ").toStdString();
}

TEST(ActionValidator, RejectsUnknownOp)
{
    const auto r = actionValidator::validateActions(arr(R"([{"op":"fly_to_moon"}])"));
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(hasSubstr(r.errors, "unknown op"));
}

TEST(ActionValidator, RejectsBadTargetType)
{
    const auto r = actionValidator::validateActions(
        arr(R"([{"op":"add_transition","target":{"type":"timeline","index":0},"transition":"wipe"}])"));
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(hasSubstr(r.errors, "target.type"));
}

TEST(ActionValidator, RejectsOutOfRangeSpeedAndDuration)
{
    const auto r1 = actionValidator::validateActions(
        arr(R"([{"op":"set_clip_speed","target":{"type":"clip","index":0},"speed":99}])"));
    EXPECT_FALSE(r1.ok);
    const auto r2 = actionValidator::validateActions(
        arr(R"([{"op":"add_transition","target":{"type":"clip","index":0},"transition":"push","duration_ms":999999}])"));
    EXPECT_FALSE(r2.ok);
}

TEST(ActionValidator, RejectsEmptyList)
{
    const auto r = actionValidator::validateActions(QJsonArray());
    EXPECT_FALSE(r.ok);
}

TEST(ActionValidator, ExtractsArrayFromProseAndFences)
{
    const QString prose = "Sure! Here is the plan:\n"
                          "```json\n"
                          "[{\"op\":\"add_marker\",\"time\":0,\"label\":\"Intro\"}]\n"
                          "```\nDone.";
    const QJsonArray a = actionValidator::extractJsonArray(prose);
    ASSERT_EQ(a.size(), 1);
    EXPECT_TRUE(actionValidator::validateActions(a).ok);
}

TEST(ActionValidator, ExtractorHandlesStringsContainingBrackets)
{
    // Prose before the array; brackets inside string values must not break
    // the matcher.
    const QString text = QStringLiteral(
        "Here is the plan:\n"
        "[{\"op\":\"add_marker\",\"time\":5,\"label\":\"intro [draft]\"},"
        "{\"op\":\"export\",\"preset\":\"web_720p\"}]");
    const QJsonArray a = actionValidator::extractJsonArray(text);
    ASSERT_EQ(a.size(), 2);
    EXPECT_EQ(a.at(0).toObject().value("label").toString(), "intro [draft]");
    EXPECT_TRUE(actionValidator::validateActions(a).ok);
}

TEST(ActionValidator, ExtractorReturnsEmptyOnGarbage)
{
    EXPECT_TRUE(actionValidator::extractJsonArray("no json here").isEmpty());
    EXPECT_TRUE(actionValidator::extractJsonArray("[{broken").isEmpty());
}
