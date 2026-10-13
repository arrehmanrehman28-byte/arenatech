// i18n loader: discovery, fallback, RTL, placeholders, credit stability.
#include <gtest/gtest.h>
#include "i18n.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

using namespace arena;

class I18nTest : public ::testing::Test {
protected:
    void SetUp() override { I18n::instance().loadDirectory(ARENA_I18N_DIR); }
};

TEST_F(I18nTest, DiscoversShippedLanguages)
{
    const auto locales = I18n::instance().locales();
    QStringList bcp47;
    for (const auto &l : locales) bcp47 << l.bcp47;
    // The eight shipped UI languages (spec).
    for (const QString &e : {QStringLiteral("en"), QStringLiteral("ur"),
                             QStringLiteral("ar"), QStringLiteral("es"),
                             QStringLiteral("fr"), QStringLiteral("hi"),
                             QStringLiteral("zh"), QStringLiteral("ja")})
        EXPECT_TRUE(bcp47.contains(e)) << e.toUtf8().constData();
}

TEST_F(I18nTest, RtlDetection)
{
    I18n::instance().setLocale("ar");
    EXPECT_TRUE(I18n::instance().isRtl());
    I18n::instance().setLocale("ur");
    EXPECT_TRUE(I18n::instance().isRtl());
    I18n::instance().setLocale("en");
    EXPECT_FALSE(I18n::instance().isRtl());
}

TEST_F(I18nTest, TranslationsResolveAndFallBack)
{
    I18n::instance().setLocale("ar");
    // Arabic menu string differs from the English one and is non-empty.
    const QString arFile = I18n::instance().translate("menu.file");
    EXPECT_NE(arFile, "menu.file");
    EXPECT_NE(arFile, "File");
    // A key that exists in all locales resolves in the current locale.
    EXPECT_FALSE(I18n::instance().translate("panel.ai").isEmpty());
    I18n::instance().setLocale("en");
    EXPECT_EQ(I18n::instance().translate("menu.file"), "File");
}

TEST_F(I18nTest, PlaceholdersSubstitute)
{
    I18n::instance().setLocale("en");
    const QString s = I18n::instance().translate("panel.ai.quota",
                                          {{"used", "37"}, {"limit", "200"}});
    EXPECT_NE(s.indexOf("37 of 200"), -1);
    I18n::instance().setLocale("ar");
    const QString ar = I18n::instance().translate("panel.ai.quota",
                                           {{"used", "37"}, {"limit", "200"}});
    EXPECT_NE(ar.indexOf("37"), -1);
    EXPECT_NE(ar.indexOf("200"), -1);
}

TEST_F(I18nTest, BrandingStableAcrossLocales)
{
    // Product name and the credit line must never be localized differently.
    for (const QString &loc : QStringList{QStringLiteral("en"), QStringLiteral("ar"), QStringLiteral("ur")}) {
        I18n::instance().setLocale(loc);
        EXPECT_EQ(I18n::instance().translate("app.name"), "Arena Edit");
        EXPECT_EQ(I18n::instance().translate("credit.madeBy"),
                  "Made by arenatechofficial.pages.dev");
        EXPECT_EQ(I18n::instance().translate("dlg.about.credit"),
                  "Made by arenatechofficial.pages.dev");
    }
}

TEST_F(I18nTest, MissingKeyReturnsKeyItself)
{
    I18n::instance().setLocale("en");
    EXPECT_EQ(I18n::instance().translate("no.such.key.exists"), "no.such.key.exists");
}

TEST_F(I18nTest, DropInLanguageAppears)
{
    // Copy English into a scratch dir as "zz-test" and verify discovery.
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QFile src(QStringLiteral(ARENA_I18N_DIR "/en.json"));
    ASSERT_TRUE(src.exists());
    ASSERT_TRUE(src.open(QIODevice::ReadOnly));
    const QByteArray bytes = src.readAll();
    QByteArray fixed = bytes;
    fixed.replace("\"bcp47\": \"en\"", "\"bcp47\": \"zz-test\"");
    fixed.replace("\"nativeName\": \"English\"", "\"nativeName\": \"Zz Test\"");
    QFile out(tmp.filePath("zz-test.json"));
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(fixed);
    out.close();
    I18n::instance().loadDirectory(tmp.path());
    QStringList bcp47;
    for (const auto &l : I18n::instance().locales()) bcp47 << l.bcp47;
    EXPECT_TRUE(bcp47.contains("zz-test"));
    // Restore the real catalog for other tests.
    I18n::instance().loadDirectory(ARENA_I18N_DIR);
}

// M5.4: the eight shipped catalogs must stay in lockstep — same key set as
// English, and branding strings never localized.
TEST_F(I18nTest, ShippedCatalogsHaveKeyParity)
{
    const QStringList locs = {QStringLiteral("en"), QStringLiteral("ur"),
                              QStringLiteral("ar"), QStringLiteral("es"),
                              QStringLiteral("fr"), QStringLiteral("hi"),
                              QStringLiteral("zh"), QStringLiteral("ja")};
    QHash<QString, QJsonObject> strings;
    for (const QString &loc : locs) {
        QFile f(QStringLiteral(ARENA_I18N_DIR) + "/" + loc + QStringLiteral(".json"));
        ASSERT_TRUE(f.open(QIODevice::ReadOnly)) << loc.toUtf8().constData();
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        strings[loc] = root.value(QStringLiteral("strings")).toObject();
    }
    const QStringList enList = strings[QStringLiteral("en")].keys();
    const QSet<QString> enKeys(enList.begin(), enList.end());
    for (const QString &loc : locs) {
        if (loc == QLatin1String("en")) continue;
        const QStringList locList = strings[loc].keys();
        const QSet<QString> locKeys(locList.begin(), locList.end());
        EXPECT_EQ(locKeys, enKeys) << loc.toUtf8().constData();
    }
    for (const QString &loc : locs) {
        const QJsonObject &s = strings[loc];
        EXPECT_EQ(s.value(QStringLiteral("app.name")).toString(),
                  QStringLiteral("Arena Edit"))
            << loc.toUtf8().constData();
        EXPECT_EQ(s.value(QStringLiteral("credit.madeBy")).toString(),
                  QStringLiteral("Made by arenatechofficial.pages.dev"))
            << loc.toUtf8().constData();
        EXPECT_EQ(s.value(QStringLiteral("dlg.about.credit")).toString(),
                  QStringLiteral("Made by arenatechofficial.pages.dev"))
            << loc.toUtf8().constData();
    }
}
