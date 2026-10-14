// M6.3 live-session driver: exercises the four AI features (titles,
// chapters, translation in batches, cut suggestions) against the LIVE Kilo
// Gateway using a synthetic, text-only session. Every response is validated
// offline (aifeatures) exactly as the app does, and a markdown log is
// printed for docs/AI_INTEGRATION.md.
//
// Ground rules honored here and in the app:
// - only text ever leaves the machine (this driver sends captions + metadata);
// - no API key is logged (presence only); model id is configurable, never
//   hardcoded (the client's ":free" default resolves from the live model list);
// - nothing is applied anywhere: this driver only logs.
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QVector>

#include "aifeatures.h"
#include "i18n.h"
#include "kilogateway.h"
#include "project.h"
#include "settings.h"
#include "util.h"

using namespace arena;

// --- dev-tool settings isolation ---------------------------------------------
// The driver enables AI for the session; keep that out of the user's real
// profile by pointing user-scope settings at a throwaway directory. Must run
// before Settings is first constructed, hence static initializers.
static QTemporaryDir g_isolatedSettingsDir;
struct DriverSettingsRedirect {
    DriverSettingsRedirect()
    {
        if (g_isolatedSettingsDir.isValid())
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                               g_isolatedSettingsDir.path());
    }
};
static DriverSettingsRedirect g_settingsRedirect;

static QTextStream out(stdout);
static bool g_anyFailed = false;
static bool g_anyNetworkError = false;

static QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

// Truncate long text for the log, keeping both ends.
static QString shortText(const QString &s, int max = 400)
{
    if (s.size() <= max) return s;
    return s.left(max / 2) + QStringLiteral(" […] ") + s.right(max / 2);
}

struct Cue {
    double startSec;
    double endSec;
    QString text;
};

// Synthetic 45-line session ("how to make tea"), ~116 s at 30 fps, with a
// deliberate 3.5 s gap after cue 20 and filler words in a few lines so the
// cut-suggestion task has real material to work with.
static QVector<Cue> makeSession()
{
    const QStringList lines = {
        QStringLiteral("Welcome back to the kitchen."),
        QStringLiteral("Today we make the perfect cup of tea."),
        QStringLiteral("Start by boiling fresh water."),
        QStringLiteral("Use a kettle, not the stove."),
        QStringLiteral("While it heats, grab your favorite mug."),
        QStringLiteral("Ceramic holds heat better than glass."),
        QStringLiteral("Measure one generous teaspoon of leaves."),
        QStringLiteral("Black tea for a strong brew."),
        QStringLiteral("Green tea if you like it lighter."),
        QStringLiteral("um, and do not skip the warm-up step."),
        QStringLiteral("Rinse the mug with hot water first."),
        QStringLiteral("This keeps the temperature steady."),
        QStringLiteral("You know that cold mug shock kills the flavor."),
        QStringLiteral("Now pour the hot water over the leaves."),
        QStringLiteral("Aim for ninety-five degrees if you can."),
        QStringLiteral("That is just off the boil."),
        QStringLiteral("Let it steep for three minutes."),
        QStringLiteral("Two for green tea, four for black."),
        QStringLiteral("Set a timer, trust it."),
        QStringLiteral("Do not stir while it steeps."),
        QStringLiteral("That bruises the leaves."),
        QStringLiteral("You should see the color darken slowly."),
        QStringLiteral("Beautiful, is it not?"),
        QStringLiteral("Now give it a gentle swirl."),
        QStringLiteral("Pour the tea slowly down the side."),
        QStringLiteral("This reduces aeration and bitterness."),
        QStringLiteral("Add milk or honey to taste."),
        QStringLiteral("Or drink it plain, the purist way."),
        QStringLiteral("A slice of lemon is nice too."),
        QStringLiteral("Stir once, never more."),
        QStringLiteral("Let it cool for a minute."),
        QStringLiteral("Then take your first sip."),
        QStringLiteral("Feel the warmth spread out."),
        QStringLiteral("This is the part you are waiting for."),
        QStringLiteral("Perfect temperature, perfect strength."),
        QStringLiteral("Serve it with a warm biscuit."),
        QStringLiteral("Shortbread is the classic partner."),
        QStringLiteral("Now clean up the kettle."),
        QStringLiteral("Rinse the mug before it dries."),
        QStringLiteral("Store dry leaves in a sealed tin."),
        QStringLiteral("Keep them away from light."),
        QStringLiteral("That keeps the flavor fresh."),
        QStringLiteral("See you in the next episode."),
        QStringLiteral("Thanks for brewing with us."),
        QStringLiteral("Stay warm out there.")};
    QVector<Cue> cues;
    double t = 1.5; // leading gap
    for (const QString &line : lines) {
        const double dur = 2.4;
        cues.append({t, t + dur, line});
        t += dur + 0.2;
        if (cues.size() == 21) t += 3.5; // deliberate long gap
    }
    return cues;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Model id is never hardcoded into the product; for this dev driver the
    // default is the last model verified stable on the anonymous free tier.
    // 2026-10-13: kilo-auto/free routed to an overloaded upstream, ling-3.1
    // was rate-limited, and lfm-2.5-2.6b dropped entries on the 40-line batch;
    // nemotron-3-super-120b produced exact 40/40 and valid timecodes.
    // See AI_INTEGRATION.md.
    const QString modelId =
        argc > 1 ? QString::fromLocal8Bit(argv[1])
                 : QStringLiteral("nvidia/nemotron-3-super-120b-a12b:free");
    const bool nlOnly = argc > 2 && QString::fromLocal8Bit(argv[2]) == "nl";

#ifdef ARENA_I18N_DIR
    { // translatable action descriptions in the record (English)
        QString i18nError;
        I18n::instance().loadDirectory(QStringLiteral(ARENA_I18N_DIR), &i18nError);
    }
#endif
    Settings &settings = Settings::instance();
    settings.setAiEnabled(true);
    settings.setAiPrivacyAccepted();
    settings.setAiModel(modelId);
    KiloClient::clearCache(); // force real network traffic for the record
    KiloClient client(settings);

    out << "# Kilo Gateway live session — M6 AI features\n\n";
    out << "- UTC: " << nowIso() << "\n";
    out << "- base URL: `" << KiloClient::kBaseUrl << "`\n";
    out << "- api key: "
        << (client.apiKeyConfigured() ? "configured (value never logged)"
                                      : "none (anonymous free-model use)")
        << "\n";

    // --- model list --------------------------------------------------------
    QEventLoop modelLoop;
    QTimer modelTimer;
    modelTimer.setSingleShot(true);
    QObject::connect(&client, &KiloClient::modelsReady, &modelLoop, &QEventLoop::quit);
    QObject::connect(&modelTimer, &QTimer::timeout, &modelLoop, &QEventLoop::quit);
    client.refreshModels();
    modelTimer.start(15000);
    modelLoop.exec();
    const QString model = client.currentModel();
    out << "\n## Model list\n\n";
    out << "- model used: `" << model << "`\n";
    const QStringList freeIds = client.freeModelIds();
    out << "- free models (" << freeIds.size() << "): " << freeIds.join(", ") << "\n";

    // --- synthetic session ---------------------------------------------------
    const double fps = 30.0;
    const QVector<Cue> cues = makeSession();
    const double durationSec = cues.last().endSec + 2.0; // trailing gap
    out << "\n## Session (synthetic, text only)\n\n";
    out << "- project: \"The Perfect Cup of Tea (AI Demo)\" 1920x1080 @"
        << QString::number(fps, 'g', 3) << " fps, "
        << QString::number(durationSec, 'f', 1) << " s, " << cues.size()
        << " caption cues\n";

    // Transcript + silence map shared by chapters/cuts.
    QStringList transcript;
    for (const Cue &c : cues)
        transcript << framesToTimecode(qRound(c.startSec * fps), fps) + " " + c.text;
    const QString transcriptText = transcript.join('\n');

    QStringList silences;
    silences << QStringLiteral("%1 - %2 (%3s)")
                 .arg(framesToTimecode(0, fps),
                      framesToTimecode(qRound(cues.first().startSec * fps), fps),
                      QString::number(cues.first().startSec, 'f', 1));
    for (int i = 1; i < cues.size(); ++i) {
        const double gap = cues[i].startSec - cues[i - 1].endSec;
        if (gap >= 1.0)
            silences << QStringLiteral("%1 - %2 (%3s)")
                         .arg(framesToTimecode(qRound(cues[i - 1].endSec * fps), fps),
                              framesToTimecode(qRound(cues[i].startSec * fps), fps),
                              QString::number(gap, 'f', 1));
    }
    silences << QStringLiteral("%1 - %2 (%3s)")
                 .arg(framesToTimecode(qRound(cues.last().endSec * fps), fps),
                      framesToTimecode(qRound(durationSec * fps), fps),
                      QString::number(durationSec - cues.last().endSec, 'f', 1));
    const QString silenceMap = silences.join('\n');
    out << "- silence map:\n\n```\n" << silenceMap << "\n```\n";

    const QString kSystem = QStringLiteral(
        "You are Arena Edit's structured-output assistant. "
        "Follow the requested output format exactly and output the "
        "machine-readable content only, with no commentary.");
    out << "\nSystem prompt (all tasks):\n\n```\n" << kSystem << "\n```\n";

    // --- helper: run one chat call with timing + raw capture ----------------
    struct RunResult {
        QString full;
        QString error;
        qint64 elapsedMs = 0;
    };
    // Wall clock for log stamps (QElapsedTimer deltas misbehave in this
    // sandbox: the monotonic clock is past signed-ns range).
    const qint64 sessionStartMs = QDateTime::currentMSecsSinceEpoch();
    auto stamp = [&]() {
        return QStringLiteral("[%1s]")
            .arg((QDateTime::currentMSecsSinceEpoch() - sessionStartMs) / 1000.0, 0, 'f', 1);
    };
    auto runChat = [&](const QString &user, const QString &label,
                       int maxTokens = 1500) -> RunResult {
        out << "\n" << stamp() << " " << label << " — user prompt (" << user.size()
            << " chars):\n\n```\n"
            << shortText(user, 500) << "\n```\n\n";
        RunResult r;
        QEventLoop loop;
        QElapsedTimer t;
        t.start();
        client.chat(kSystem, user,
                    [](const QString &) {}, // streaming deltas not logged
                    [&loop, &r, &t](const QString &full, const QString &err) {
                        r.full = full;
                        r.error = err;
                        r.elapsedMs = t.elapsed();
                        loop.quit();
                    },
                    maxTokens);
        QTimer::singleShot(120000, &loop, &QEventLoop::quit); // hard cap
        loop.exec();
        r.elapsedMs = t.elapsed(); // authoritative (covers the timer path too)
        if (r.error.isEmpty() && r.full.isEmpty())
            r.error = QStringLiteral("timed out (no response)");
        if (!r.error.isEmpty()) {
            g_anyNetworkError = true;
            out << "ERROR: " << r.error << "\n";
        }
        return r;
    };
    auto logRaw = [&](const QString &full, const QString &note = QString()) {
        out << "Raw response (" << full.size() << " chars"
            << (note.isEmpty() ? QString() : ", " + note) << "):\n\n```\n"
            << full << "\n```\n";
    };
    auto mark = [&](bool ok, const QString &what) {
        out << (ok ? "- [x] " : "- [ ] ") << what << "\n";
        if (!ok) g_anyFailed = true;
    };

    if (!nlOnly) {
        // --- task 1: smart title + description ----------------------------------
        if (nlOnly) out << "\n(tasks 1-4 skipped: nl-only mode)\n";
        out << "\n## Task 1 — smart title & description\n";
        {
            const QString context = QStringLiteral(
                "Project name: The Perfect Cup of Tea (AI Demo)\n"
                "Duration: %1 s\nResolution: 1920x1080 @ 30 fps\n"
                "Captions (first 20 lines):\n%2")
                                        .arg(QString::number(durationSec, 'f', 1))
                                        .arg(transcript.mid(0, 20).join('\n'));
            const RunResult r = runChat(aifeatures::titlePrompt(context),
                                        "### Title (1 request)", aifeatures::kTitleMaxTokens);
            if (r.error.isEmpty()) {
                logRaw(r.full);
                TitleResult t;
                QString verr;
                const bool ok = aifeatures::parseTitle(r.full, &t, &verr);
                mark(ok, QStringLiteral("parsed in %1 ms").arg(r.elapsedMs) +
                            (ok ? QString() : " — " + verr));
                if (ok) {
                    out << "- title (" << t.title.size() << " chars): \"" << t.title
                        << "\"\n";
                    out << "- description (" << t.description.size()
                        << " chars): \"" << shortText(t.description, 200) << "\"\n";
                }
            } else {
                mark(false, "request failed");
            }
        }

        // --- task 2: chapters -----------------------------------------------------
        out << "\n## Task 2 — chapters\n";
        {
            const RunResult r = runChat(aifeatures::chaptersPrompt(transcriptText),
                                        "### Chapters (1 request)",
                                        aifeatures::kChaptersMaxTokens);
            if (r.error.isEmpty()) {
                logRaw(r.full);
                QVector<ChapterSuggestion> ch;
                QString verr;
                const bool ok = aifeatures::parseChapters(r.full, durationSec, &ch, &verr);
                mark(ok, QStringLiteral("parsed in %1 ms").arg(r.elapsedMs) +
                            (ok ? QString() : " — " + verr));
                if (ok)
                    for (const ChapterSuggestion &c : ch)
                        out << "- " << framesToTimecode(qRound(c.startSec * fps), fps)
                            << "  " << c.title << "\n";
            } else {
                mark(false, "request failed");
            }
        }

        // --- task 3: subtitle translation (batches of ~40) -------------------------
        out << "\n## Task 3 — subtitle translation to Spanish (batches of "
            << aifeatures::kTranslateBatchSize << ")\n";
        {
            const int n = cues.size();
            const QVector<QPair<int, int>> ranges = aifeatures::batchRanges(n);
            out << "- " << n << " entries → " << ranges.size() << " batches\n";
            QStringList results;
            results.resize(n);
            bool allOk = true;
            for (int bi = 0; bi < ranges.size(); ++bi) {
                const int first = ranges[bi].first;
                const int last = ranges[bi].second;
                QVector<QJsonObject> entries;
                for (int i = first; i < last; ++i) {
                    QJsonObject o;
                    o[QStringLiteral("index")] = i;
                    o[QStringLiteral("start")] =
                        framesToTimecode(qRound(cues[i].startSec * fps), fps);
                    o[QStringLiteral("end")] = framesToTimecode(qRound(cues[i].endSec * fps),
                                                                fps);
                    o[QStringLiteral("text")] = cues[i].text;
                    entries.append(o);
                }
                const RunResult r = runChat(aifeatures::translatePrompt(entries, "es"),
                                            QStringLiteral("### Batch %1/%2 (entries %3–%4)")
                                                .arg(bi + 1)
                                                .arg(ranges.size())
                                                .arg(first + 1)
                                                .arg(last),
                                            aifeatures::kTranslateBatchMaxTokens);
                if (r.error.isEmpty()) {
                    logRaw(r.full, bi == 0 ? QStringLiteral("full text")
                                           : QStringLiteral("summarized in docs"));
                    QStringList batch;
                    QString verr;
                    const bool ok = aifeatures::parseTranslation(r.full, last - first, &batch,
                                                                 &verr);
                    mark(ok, QStringLiteral("batch %1 parsed in %2 ms").arg(bi + 1).arg(r.elapsedMs) +
                                (ok ? QString() : " — " + verr));
                    if (ok) {
                        for (int i = 0; i < batch.size() && first + i < n; ++i)
                            results[first + i] = batch[i];
                    } else {
                        // Retry the failed batch once (app behavior). Drop the
                        // cached (unparseable) response first, or the retry is
                        // served from cache and fails identically.
                        out << "- retrying batch " << bi + 1 << " once…\n";
                        client.invalidateCache(kSystem, aifeatures::translatePrompt(entries, "es"));
                        const RunResult r2 = runChat(aifeatures::translatePrompt(entries, "es"),
                                                     QStringLiteral("### Batch %1/%2 retry")
                                                         .arg(bi + 1)
                                                         .arg(ranges.size()),
                                                     aifeatures::kTranslateBatchMaxTokens);
                        QStringList batch2;
                        QString verr2;
                        const bool ok2 =
                            aifeatures::parseTranslation(r2.full, last - first, &batch2, &verr2);
                        mark(ok2, QStringLiteral("batch %1 retry parsed in %2 ms")
                                      .arg(bi + 1)
                                      .arg(r2.elapsedMs) + (ok2 ? QString() : " — " + verr2));
                        if (ok2) {
                            for (int i = 0; i < batch2.size() && first + i < n; ++i)
                                results[first + i] = batch2[i];
                        } else {
                            allOk = false;
                        }
                    }
                } else {
                    allOk = false;
                    mark(false, QStringLiteral("batch %1 request failed").arg(bi + 1));
                }
            }
            if (allOk) {
                int sample = 0;
                for (int i = 0; i < n && sample < 5; ++i) {
                    out << "- line " << i + 1 << ": \"" << cues[i].text << "\" → \""
                        << results[i] << "\"\n";
                    ++sample;
                }
                out << "- … (" << n << " lines total, all validated non-empty)\n";
            }
        }

        // --- task 4: cut suggestions ---------------------------------------------
        out << "\n## Task 4 — cut suggestions\n";
        {
            const RunResult r = runChat(aifeatures::cutsPrompt(transcriptText, silenceMap, fps),
                                        "### Cuts (1 request)", aifeatures::kCutsMaxTokens);
            if (r.error.isEmpty()) {
                logRaw(r.full);
                QVector<CutSuggestion> cuts;
                QString verr;
                const bool ok = aifeatures::parseCuts(r.full, durationSec, fps, &cuts, &verr);
                mark(ok, QStringLiteral("parsed in %1 ms").arg(r.elapsedMs) +
                            (ok ? QString() : " — " + verr));
                if (ok)
                    for (const CutSuggestion &c : cuts)
                        out << "- "
                            << framesToTimecode(qRound(c.startSec * fps), fps) << " → "
                            << framesToTimecode(qRound(c.endSec * fps), fps) << "  "
                            << c.reason << " (" << QString::number(int(c.confidence * 100))
                            << "%)\n";
                if (cuts.isEmpty())
                    out << "- (no cuts suggested)\n";
            } else {
                mark(false, "request failed");
            }
        }

    }

    // --- task 5: NL command bar (strict JSON action schema) ------------------
    // Same context shape + validator the app uses (AiPanel::commandContext /
    // aifeatures::parseActions). The synthetic project is the two-clip cut of
    // this session (cut at 30 s = frame 900).
    out << "\n## Task 5 — NL command bar (3 requests)\n";
    {
        const qint64 durationFrames = qRound(durationSec * fps);
        QJsonObject ctx;
        ctx[QStringLiteral("fps")] = fps;
        ctx[QStringLiteral("durationFrames")] = durationFrames;
        QJsonArray clips;
        for (const auto &seg : { QPair<qint64, qint64>{0, 900},
                                 QPair<qint64, qint64>{900, durationFrames - 900} }) {
            QJsonObject cj;
            cj[QStringLiteral("index")] = clips.size();
            cj[QStringLiteral("startFrame")] = seg.first;
            cj[QStringLiteral("durationFrames")] = seg.second;
            clips.append(cj);
        }
        ctx[QStringLiteral("mainVideoTrackClips")] = clips;
        ctx[QStringLiteral("markers")] = QJsonArray();
        const QString ctxText =
            QString::fromUtf8(QJsonDocument(ctx).toJson(QJsonDocument::Compact));

        struct NlCase {
            QString request;
            QString expectedOp;
            int expectedIndex; // -2 = not asserted
        };
        const QVector<NlCase> cases = {
            { QStringLiteral("Add a fade out to the last clip"),
              QStringLiteral("add_transition"), -1 },
            { QStringLiteral("Make the first clip play at 1.5x speed"),
              QStringLiteral("set_clip_speed"), 0 },
            { QStringLiteral("Add a marker labeled \"Tea tips\" at 12 seconds"),
              QStringLiteral("add_marker"), -2 },
        };
        for (int i = 0; i < cases.size(); ++i) {
            const NlCase &c = cases[i];
            out << "\n### NL command " << (i + 1) << ": \"" << c.request << "\"\n";
            const RunResult r =
                runChat(aifeatures::actionPrompt(ctxText, c.request),
                        QStringLiteral("### NL command %1").arg(i + 1),
                        aifeatures::kActionsMaxTokens);
            if (!r.error.isEmpty()) {
                mark(false, "request failed: " + r.error);
                continue;
            }
            logRaw(r.full);
            QJsonArray actions;
            QString verr;
            const bool ok = aifeatures::parseActions(r.full, &actions, &verr);
            mark(ok, QStringLiteral("validated in %1 ms").arg(r.elapsedMs) +
                        (ok ? QString() : " — " + verr));
            if (ok) {
                bool opOk = false;
                int idxOk = 1;
                for (const QJsonValue &v : actions) {
                    const QJsonObject a = v.toObject();
                    const QString desc =
                        aifeatures::actionDescription(a, fps, 2);
                    out << "- "
                        << (desc.isEmpty()
                                ? QString::fromUtf8(
                                      QJsonDocument(a).toJson(QJsonDocument::Compact))
                                : desc)
                        << "\n";
                    if (a.value(QStringLiteral("op")).toString() == c.expectedOp)
                        opOk = true;
                    if (c.expectedIndex >= 0) {
                        const int idx = static_cast<int>(a
                            .value(QStringLiteral("target")).toObject()
                            .value(QStringLiteral("index")).toDouble(0));
                        if (idx != c.expectedIndex) idxOk = 0;
                    }
                }
                if (!opOk) mark(false, "expected op '" + c.expectedOp + "' not found");
                if (c.expectedIndex >= 0 && !idxOk)
                    mark(false, "expected clip index " + QString::number(c.expectedIndex));
            }
        }
    }

    out << "\n## Summary\n\n";
    out << "- network errors: " << (g_anyNetworkError ? "yes" : "no") << "\n";
    out << "- validation failures: " << (g_anyFailed ? "yes" : "no") << "\n";
    out << "- exit: " << (g_anyFailed ? "FAIL" : "PASS") << "\n";
    return (g_anyFailed || g_anyNetworkError) ? 1 : 0;
}
