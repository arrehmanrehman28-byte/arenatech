// Theme tokens: single source of truth, accent propagation, contrast basics.
// These tests are pure data/string math (JSON tokens, QSS text, WCAG
// luminance ratios) — no widgets, no fonts. Deliberately NO Qt GUI
// application is constructed: on headless Windows CI runners even the
// offscreen QGuiApplication/QApplication constructor hangs (M9.22), and
// nothing in this file needs one (it used to, defensively — removed).
#include <gtest/gtest.h>
#include "themer.h"
#include <QColor>
#include <cmath>

using namespace arena;

namespace {
class ThemerFixture : public ::testing::Test {
protected:
    void SetUp() override
    {
        QString err;
        const bool ok = t.load(ARENA_THEME_JSON, &err);
        ASSERT_TRUE(ok) << err.toStdString();
    }
    Themer t;
};
} // namespace

TEST_F(ThemerFixture, LoadsAllThreeThemes)
{
    EXPECT_TRUE(t.hasTheme("dark"));
    EXPECT_TRUE(t.hasTheme("light"));
    EXPECT_TRUE(t.hasTheme("high-contrast"));
    EXPECT_EQ(t.accentDefault(), "#4C8DFF");
    EXPECT_EQ(t.aiAccent(), "#9B6DFF");
    EXPECT_EQ(t.radiusControl(), 6);
    EXPECT_EQ(t.radiusPanel(), 10);
}

TEST_F(ThemerFixture, AccentColorPropagatesToQss)
{
    const QColor accent(0x4C, 0x8D, 0xFF);
    const QString qss = t.buildQss("dark", accent);
    EXPECT_NE(qss.indexOf("#4c8dff"), -1);
    // A custom accent must show up too.
    const QString qss2 = t.buildQss("dark", QColor("#123456"));
    EXPECT_NE(qss2.indexOf("#123456"), -1);
    // AI violet is always present (AI actions must be recognizable).
    EXPECT_NE(qss.indexOf("#9B6DFF"), -1);
}

TEST_F(ThemerFixture, SurfacesHaveFiveElevationLevels)
{
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(t.tokens("dark").contains(QStringLiteral("surface%1").arg(i)))
            << "missing surface level " << i;
}

TEST_F(ThemerFixture, HighContrastTextMeetsWcagOnBackground)
{
    const auto tokens = t.tokens("high-contrast");
    const QColor text(tokens.value("text"));
    const QColor bg(tokens.value("surface0"));
    auto lum = [](const QColor &c) {
        auto f = [](int v) {
            const double x = v / 255.0;
            return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * f(c.red()) + 0.7152 * f(c.green()) + 0.0722 * f(c.blue());
    };
    const double l1 = lum(text), l2 = lum(bg);
    const double ratio = (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
    EXPECT_GE(ratio, 4.5) << "contrast ratio " << ratio << " below WCAG AA";
}

// M8 (WCAG AA audit): every shipped theme must clear 4.5:1 for all
// text/status pairs used on surfaces. Pairs verified 2026-10-14; the light
// theme's success green was darkened to #177345 (5.87:1) to reach AA
// (was #1F8A54 at 4.36:1).
TEST_F(ThemerFixture, AllThemesMeetWcagAaForTextPairs)
{
    auto lum = [](const QString &hex) {
        const QColor c(hex);
        auto f = [](int v) {
            const double x = v / 255.0;
            return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * f(c.red()) + 0.7152 * f(c.green()) + 0.0722 * f(c.blue());
    };
    auto ratio = [&](const QString &a, const QString &b) {
        const double l1 = lum(a), l2 = lum(b);
        return (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
    };
    const QStringList themes = {"dark", "light", "high-contrast"};
    const QStringList fgBgPairs = {
        QStringLiteral("text/surface0"),     QStringLiteral("text/surface1"),
        QStringLiteral("textMuted/surface0"), QStringLiteral("textMuted/surface1"),
        QStringLiteral("success/surface1"),  QStringLiteral("warning/surface1"),
        QStringLiteral("error/surface1")};
    for (const QString &theme : themes) {
        const auto tokens = t.tokens(theme);
        for (const QString &pair : fgBgPairs) {
            const QStringList parts = pair.split('/');
            const double r = ratio(tokens.value(parts[0]), tokens.value(parts[1]));
            EXPECT_GE(r, 4.5)
                << theme.toStdString() << " pair " << pair.toStdString() << " ratio " << r;
        }
    }
}

// M8: the stylesheet must carry the keyboard focus rings and the tour-target
// highlight (the UI has no other focus indication).
TEST_F(ThemerFixture, FocusRingsAndTourHighlightInQss)
{
    const QString qss = t.buildQss("dark", QColor("#4C8DFF"));
    EXPECT_NE(qss.indexOf("QPushButton:focus"), -1) << "button focus ring missing";
    EXPECT_NE(qss.indexOf("TimelineCanvas:focus"), -1) << "canvas focus ring missing";
    EXPECT_NE(qss.indexOf("tourTarget"), -1) << "tour target highlight missing";
}
