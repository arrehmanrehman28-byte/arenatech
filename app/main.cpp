// Arena Edit — application entry point.
// Credit: Made by arenatechofficial.pages.dev
#include "mainwindow.h"
#include "i18n.h"
#include "logger.h"
#include "settings.h"
#include "themer.h"
#include "project.h"
#include "command.h"
#include "autosave.h"
#include "actionvalidator.h"
#include "playback.h"
#include "media.h"
#include <QApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QUuid>
#include <QTemporaryDir>
#include <cstdio>

using namespace arena;

static QString findFile(const char *compileDef, const QStringList &appDirCandidates)
{
    if (QFile::exists(compileDef)) return QString::fromUtf8(compileDef);
    for (const QString &d : appDirCandidates)
        if (QFile::exists(d)) return d;
    return QString();
}

int main(int argc, char **argv)
{
    // Detect --selftest before QApplication: the platform plugin is chosen
    // at construction time, and selftests must run headless (offscreen).
    bool selftest = false;
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--selftest"))
            selftest = true;
    if (selftest) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (selftest) qputenv("ARENA_SELFTEST", "1");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ArenaEdit"));
    QApplication::setApplicationName(QStringLiteral("ArenaEdit"));
    QApplication::setApplicationDisplayName(QStringLiteral("Arena Edit"));
    QApplication::setApplicationVersion(QStringLiteral(ARENA_VERSION));
    // App icon (M9): embedded via app.qrc so the window/title bar is branded
    // from any run directory; the .rc carries the same icon for the exe file.
    app.setWindowIcon(QIcon(QStringLiteral(":/icon/app_icon.png")));

    Logger::init();

    const QStringList appDir = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/i18n"),
        QStringLiteral("i18n"),
    };
    const QString i18nDir = findFile(ARENA_I18N_DIR, appDir);
    QString i18nError;
    if (!I18n::instance().loadDirectory(i18nDir, &i18nError)) {
        Logger::crashContext(QStringLiteral("i18n load failed"), i18nError);
        fprintf(stderr, "i18n load failed: %s\n", i18nError.toUtf8().constData());
        return 2;
    }

    const QString themeJson = findFile(ARENA_THEME_JSON,
                                       {QCoreApplication::applicationDirPath() + QStringLiteral("/assets/theme/theme.json")});
    Themer themer;
    QString themeError;
    if (!themer.load(themeJson, &themeError)) {
        Logger::crashContext(QStringLiteral("theme load failed"), themeError);
        fprintf(stderr, "theme load failed: %s\n", themeError.toUtf8().constData());
        return 3;
    }

    Settings &st = Settings::instance();
    // First launch: follow the OS theme (Windows light/dark); otherwise default dark.
    if (!st.firstRunDone()) {
#ifdef Q_OS_WIN
        QSettings os(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                     QSettings::NativeFormat);
        // QVariant::toInt takes a bool* out-parameter (no default-value
        // overload) — MSVC enforces that strictly.
        bool themeOk = false;
        if (os.value(QStringLiteral("AppsUseLightTheme")).toInt(&themeOk) == 1)
            st.setTheme(QStringLiteral("light"));
#endif
        // Note: firstRunDone is flipped by MainWindow::showWelcomeIfNeeded()
        // after the welcome dialog + first-run tour (M8), not here — flipping
        // it earlier suppressed the welcome flow on genuine first launch.
    }

    themer.apply(&app, st.theme(), st.accentColor(), st.fontScale());

    MainWindow w(&themer);
    w.show();

    // Headless self-test used by CI: exercises core paths, prints a verdict, exits.
    if (selftest) {
        int failures = 0;
        auto check = [&](bool ok, const QString &what) {
            printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.toUtf8().constData());
            if (!ok) ++failures;
        };
        // 1. i18n: locales loaded, RTL detection, fallback.
        const auto locales = I18n::instance().locales();
        check(locales.size() >= 3, QStringLiteral("i18n loaded %1 locales").arg(locales.size()));
        I18n::instance().setLocale(QStringLiteral("ar"));
        check(I18n::instance().isRtl(), QStringLiteral("ar detected as RTL"));
        check(!I18n::instance().translate(QStringLiteral("menu.file")).isEmpty()
                  && I18n::instance().translate(QStringLiteral("menu.file")) != QStringLiteral("menu.file"),
              QStringLiteral("ar translation resolves"));
        check(I18n::instance().translate(QStringLiteral("panel.ai.quota"), {{QStringLiteral("used"), 3}, {QStringLiteral("limit"), 200}})
                  .contains(QStringLiteral("3")),
              QStringLiteral("placeholder substitution"));
        I18n::instance().setLocale(QStringLiteral("en"));
        // 2. theme tokens.
        check(themer.hasTheme(QStringLiteral("dark")) && themer.hasTheme(QStringLiteral("light"))
                  && themer.hasTheme(QStringLiteral("high-contrast")),
              QStringLiteral("three themes present"));
        // 3. Media layer (FFmpeg): availability, probe, waveform, thumbnails, clock math.
        check(WaveformGenerator::isAvailable() && Thumbnailer::isAvailable() &&
                  ProxyGenerator::isAvailable(),
              QStringLiteral("FFmpeg media layer available"));
        check(PlaybackEngine::targetFrame(1.0, 30.0) == 30 &&
                  PlaybackEngine::targetFrame(0.49, 30.0) == 15 &&
                  qAbs(PlaybackEngine::secondsFromProcessed(48000, 48000) - 1.0) < 1e-9,
              QStringLiteral("playback clock math"));
        {
            const QByteArray env = qgetenv("ARENA_SELFTEST_MEDIA");
            if (!env.isEmpty()) {
                const QString mp4 = QString::fromLocal8Bit(env);
                MediaInfo info;
                QString merr;
                check(MediaProbe::probeFull(mp4, &info, &merr) && info.hasVideo,
                      QStringLiteral("probe video (%1)").arg(merr));
                QVector<Peak> peaks;
                check(WaveformGenerator::generate(mp4, 16, &peaks, &merr) && peaks.size() == 16,
                      QStringLiteral("waveform 16 buckets (%1)").arg(merr));
                QList<QImage> thumbs;
                check(Thumbnailer::generate(mp4, 3, 32, &thumbs, &merr) && thumbs.size() == 3 &&
                          !thumbs.first().isNull(),
                      QStringLiteral("thumbnails x3 (%1)").arg(merr));
            } else {
                printf("[SKIP] media probe (no ARENA_SELFTEST_MEDIA fixture)\n");
            }
        }
        // 4. project round-trip.
        QTemporaryDir tmp;
        auto project = std::make_unique<Project>();
        project->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        Track t;
        t.id = QStringLiteral("V1");
        t.type = TrackType::Video;
        t.name = QStringLiteral("V1");
        Clip c;
        c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.sourceRef = QStringLiteral("clip.mp4");
        c.in = 0;
        c.out = 900;
        c.offset = 0;
        t.clips.append(c);
        project->tracks.append(t);
        const QString pf = tmp.filePath(QStringLiteral("selftest.aep"));
        QString err;
        check(project->saveFile(pf, &err) && !err.isEmpty() == false, QStringLiteral("project save (%1)").arg(err));
        Project loaded = Project::loadFile(pf, &err);
        check(loaded.tracks.size() == 1 && loaded.tracks[0].clips.size() == 1
                  && loaded.tracks[0].clips[0].duration() == 900,
              QStringLiteral("project round-trip"));
        // 5. undo/redo command.
        UndoStack stack;
        stack.push(makeRenameProjectCommand(project.get(), QStringLiteral("Renamed")));
        check(project->name == QStringLiteral("Renamed"), QStringLiteral("command executes"));
        stack.undo();
        check(project->name != QStringLiteral("Renamed"), QStringLiteral("command undoes"));
        stack.redo();
        check(project->name == QStringLiteral("Renamed"), QStringLiteral("command redoes"));
        // 6. AI action validator.
        const QJsonArray good = QJsonDocument::fromJson(
            R"([{"op":"add_transition","target":{"type":"clip","index":-1},"transition":"fade_to_black"}])")
            .array();
        check(actionValidator::validateActions(good).ok, QStringLiteral("validator accepts valid action"));
        const QJsonArray bad =
            QJsonDocument::fromJson(R"([{"op":"fly_to_moon"}])").array();
        check(!actionValidator::validateActions(bad).ok, QStringLiteral("validator rejects unknown op"));
        // 7. window + palette exist.
        check(w.testWidgetReady(), QStringLiteral("main window constructed"));
        // 8. RTL layout direction applies.
        w.applyLanguage();
        I18n::instance().setLocale(QStringLiteral("ur"));
        w.applyLanguage();
        check(QGuiApplication::layoutDirection() == Qt::RightToLeft,
              QStringLiteral("ur switches app layout to RTL"));
        I18n::instance().setLocale(QStringLiteral("en"));
        w.applyLanguage();
        qApp->processEvents();
        if (failures == 0) {
            printf("SELFTEST PASS\n");
            return 0;
        }
        printf("SELFTEST FAIL (%d failures)\n", failures);
        return 1;
    }

    const int rc = app.exec();
    if (w.projectPath().isEmpty())
        Autosave::markCleanExit(w.projectPath());
    return rc;
}
