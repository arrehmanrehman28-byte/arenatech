// Theme tokens: single source of truth, accent propagation, contrast basics.
#include <gtest/gtest.h>
#include "themer.h"
#include <QtWidgets/QApplication>
#include <QColor>
#include <cmath>

using namespace arena;

namespace {
QApplication *ensureApp()
{
    static QApplication *app = [] {
        QApplication *instance = nullptr;
        if (QApplication::instance() == nullptr) {
            int argc = 1;
            char a0[] = "arena-tests";
            char *argv[] = {a0, nullptr};
            qputenv("QT_QPA_PLATFORM", "offscreen");
            instance = new QApplication(argc, argv);
        } else {
            instance = static_cast<QApplication *>(QApplication::instance());
        }
        return instance;
    }();
    return app;
}

class ThemerFixture : public ::testing::Test {
protected:
    void SetUp() override
    {
        ensureApp();
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
