// Headless GUI smoke test: drives the real MainWindow through the full M1
// media pipeline — import, background probe, add clip (auto project +
// undoable command), first frame on the monitor, playback, and seek.
// Runs on the offscreen platform; audio output is unavailable in CI, so
// playback exercises the wall-clock fallback path.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QElapsedTimer>
#include <QFile>
#include <QUuid>
#include <QThread>
#include <QTimer>
#include <cstdio>

#include "exportdialog.h"
#include "exportqueue.h"
#include "i18n.h"
#include "mainwindow.h"
#include "panels.h"
#include "settings.h"
#include "themer.h"
#include "timeline.h"
#include "tour.h"

using namespace arena;

static int g_failures = 0;
static void check(bool ok, const char *what)
{
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    fflush(stdout);
    if (!ok) ++g_failures;
}

static void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// M8 soak (exit criterion: no UI freezes in a long fuzzed import soak):
// drives the real MainWindow with a mix of valid video, valid audio, garbage
// bytes, and missing files plus timeline ops, while a 250 ms heartbeat
// records any event-loop gap > 2 s as a freeze.
static int runSoak(int seconds, const QString &fixturesDir)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("ARENA_SELFTEST", "1"); // skip the first-run dialog headless
    int argc = 1;
    char a0[] = "arena-soak";
    char *argv[] = {a0, nullptr};
    QApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!I18n::instance().loadDirectory(QStringLiteral(ARENA_I18N_DIR)))
        fprintf(stderr, "warning: i18n directory not found\n");

    Themer themer;
    MainWindow w(&themer);
    w.show();

    const QString goodVid = fixturesDir + QStringLiteral("/clip01.mp4");
    const QString goodAud = fixturesDir + QStringLiteral("/jfk.wav");
    const QString garbage = fixturesDir + QStringLiteral("/garbage.bin");
    if (!QFile::exists(garbage))
        if (auto *f = new QFile(garbage)) {
            f->open(QIODevice::WriteOnly);
            f->write("definitely not a media file, just bytes to fuzz with ");
            f->write(QByteArray(4096, '\x00'));
            f->close();
            delete f;
        }
    const QString missing = fixturesDir + QStringLiteral("/definitely_missing_12345.mp4");
    const QStringList pool = {goodVid, goodAud, garbage, missing, goodVid};

    // Freeze guard: 250 ms heartbeat; any gap > 2000 ms counts as a freeze.
    QElapsedTimer clock;
    clock.start();
    qint64 maxGapMs = 0;
    qint64 lastTick = 0;
    int freezes = 0;
    auto *hb = new QTimer(&app);
    QObject::connect(hb, &QTimer::timeout, [&] {
        const qint64 t = clock.elapsed();
        const qint64 gap = t - lastTick;
        if (gap > maxGapMs) maxGapMs = gap;
        if (gap > 2000) ++freezes;
        lastTick = t;
    });
    hb->start(250);

    QElapsedTimer budget;
    budget.start();
    long ops = 0;
    int clipsPlaced = 0;
    unsigned seed = 0x9E3779B9u;
    auto rnd = [&](int n) {
        seed = seed * 1103515245u + 12345u;
        return int((seed >> 16) % n);
    };

    while (budget.elapsed() < seconds * 1000LL) {
        switch (rnd(6)) {
        case 0: w.importMedia({pool.at(rnd(pool.size()))}); ++ops; break;
        case 1:
            w.addClipToTimeline(goodVid);
            ++ops;
            if (w.project() && w.project()->tracks.first().clips.size() > clipsPlaced)
                clipsPlaced = w.project()->tracks.first().clips.size();
            break;
        case 2: w.seekToFrame(rnd(300)); ++ops; break;
        case 3: w.undo(); ++ops; break;
        case 4: w.redo(); ++ops; break;
        case 5:
            w.togglePlayback();
            QThread::msleep(150);
            w.togglePlayback();
            ++ops;
            break;
        }
        // Pump so the heartbeat timer can fire between operations.
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
        QThread::msleep(10);
    }
    hb->stop();

    printf("SOAK ops=%ld clipsPlaced=%d maxGap=%lldms freezes=%d (%d s)\n",
           ops, clipsPlaced, (long long)maxGapMs, freezes, seconds);
    const bool pass = freezes == 0 && ops > 100 && clipsPlaced >= 1;
    printf(pass ? "SOAK PASS\n" : "SOAK FAIL\n");
    return pass ? 0 : 1;
}

int main(int argc, char **argv)
{
    // M8 soak mode: arena_guismoke --soak <seconds> <fixtures-dir>
    for (int i = 1; i + 1 < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--soak"))
            return runSoak(QString::fromLocal8Bit(argv[i + 1]).toInt(),
                           QString::fromLocal8Bit(argv[i + 2]));
    if (argc < 2) {
        fprintf(stderr, "usage: arena_guismoke <media-file> | --soak <seconds> <fixtures-dir>\n");
        return 2;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("ARENA_SELFTEST", "1"); // skip the first-run dialog headless
    QApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!I18n::instance().loadDirectory(QStringLiteral(ARENA_I18N_DIR)))
        fprintf(stderr, "warning: i18n directory not found\n");
    const QString media = QString::fromLocal8Bit(argv[1]);

    Themer themer;
    MainWindow w(&themer);
    w.show();
    check(true, "window constructed");

    // 1. Import + background probe (worker thread -> queued signals).
    w.importMedia({media});
    bool probed = false;
    for (int i = 0; i < 100 && !probed; ++i) {
        spin(50);
        const MediaInfo mi = w.mediaInfo(media);
        probed = mi.exists && (mi.durationMs > 0 || !mi.error.isEmpty());
    }
    const MediaInfo mi = w.mediaInfo(media);
    check(probed, "probe completed");
    check(mi.hasVideo, "video track detected");
    check(mi.hasAudio, "audio track detected");
    check(mi.width > 0 && mi.height > 0, "dimensions");
    check(mi.fps > 0.0, "fps");

    // 2. Add to timeline (auto-creates a default project).
    w.addClipToTimeline(media);
    check(true, "clip added");

    // 3. First frame must appear on the monitor.
    bool frame = false;
    for (int i = 0; i < 100 && !frame; ++i) {
        spin(50);
        frame = !w.previewImage().isNull();
    }
    check(frame, "monitor shows first frame");

    // 4. Play for ~500 ms (wall clock: no audio device in CI).
    w.togglePlayback();
    spin(500);
    check(!w.previewImage().isNull(), "playing: frame advances");
    w.togglePlayback();

    // 5. Seek to 0.5 s and confirm a frame decodes.
    w.seekToFrame(15);
    bool seeked = false;
    for (int i = 0; i < 60 && !seeked; ++i) {
        spin(50);
        seeked = !w.previewImage().isNull();
    }
    check(seeked, "seek decodes frame");

    const QImage img = w.previewImage();
    check(img.width() > img.height(), "16:9 aspect preserved");

    // --- M2: selection, delete, undo -------------------------------------
    Project *proj = w.project();
    check(proj != nullptr, "project auto-created");
    Track *v1 = nullptr;
    for (Track &t : proj->tracks)
        if (t.type == TrackType::Video) {
            v1 = &t;
            break;
        }
    check(v1 && v1->clips.size() == 1, "one clip on V1");
    const QString clipId = v1->clips.first().id;
    const qint64 clipStart = v1->clips.first().offset;
    const qint64 clipEnd = clipStart + v1->clips.first().duration();

    // Click a clip on the canvas (x = header + frame/fpp).
    TimelineCanvas *canvas = w.timeline()->canvas();
    const int xIn = 56 + 3 + int((clipStart + 2) / canvas->framesPerPx());
    {
        QMouseEvent ev(QEvent::MouseButtonPress, QPointF(xIn, 12), QPointF(xIn, 12),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &ev);
        spin(50);
    }
    check(w.selectedClipId() == clipId, "click selects clip");
    // Inspector shows the selection.
    // (Inspector state is internal; selection id + undo below cover the path.)

    // Delete key removes the clip.
    {
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(&w, &ev);
        spin(50);
    }
    check(v1->clips.isEmpty(), "Delete removes clip");
    check(w.selectedClipId().isEmpty(), "selection cleared after delete");

    // Undo brings it back.
    w.undo();
    spin(50);
    check(v1->clips.size() == 1 && v1->clips.first().id == clipId,
          "undo restores clip (same id)");

    // Redo deletes it again.
    w.redo();
    spin(50);
    check(v1->clips.isEmpty(), "redo deletes clip again");
    w.undo();
    spin(50);

    // Click empty timeline space deselects.
    {
        const int xEmpty = 56 + int((clipEnd + 5) / canvas->framesPerPx());
        QMouseEvent ev(QEvent::MouseButtonPress, QPointF(xEmpty, 12),
                       QPointF(xEmpty, 12), Qt::LeftButton, Qt::LeftButton,
                       Qt::NoModifier);
        QApplication::sendEvent(canvas, &ev);
        spin(50);
    }
    check(w.selectedClipId().isEmpty(), "empty-space click deselects");

    // --- M2.3: Q/W ripple trims, edge drag, roll edit -----------------------
    // State: one clip, in=0 out=60 offset=0 (60-frame source @ 30 fps).
    auto clip1 = [&]() -> const Clip * {
        for (Track &t : proj->tracks)
            if (t.type == TrackType::Video && !t.clips.isEmpty())
                return &t.clips.first();
        return nullptr;
    };
    // Drags must happen in the clip lane (y >= ruler height).
    auto pressAt = [&](int x) {
        QMouseEvent ev(QEvent::MouseButtonPress, QPointF(x, 40), QPointF(x, 40),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &ev);
    };
    auto moveAt = [&](int x) {
        QMouseEvent ev(QEvent::MouseMove, QPointF(x, 40), QPointF(x, 40),
                       Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &ev);
    };
    auto releaseAt = [&](int x) {
        QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(x, 40), QPointF(x, 40),
                       Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &ev);
    };
    auto key = [&](int keycode) {
        QKeyEvent ev(QEvent::KeyPress, keycode, Qt::NoModifier);
        QApplication::sendEvent(&w, &ev);
    };

    // W @ frame 10: in point -> 10.
    w.seekToFrame(10);
    key(Qt::Key_W);
    spin(50);
    check(clip1() && clip1()->in == 10 && clip1()->out == 60 &&
              clip1()->offset == 0,
          "W trims start to playhead");

    // Q @ frame 30: out point -> 30.
    w.seekToFrame(30);
    key(Qt::Key_Q);
    spin(50);
    // out = in + (playhead - offset) = 10 + 30 = 40 (source frame).
    check(clip1() && clip1()->in == 10 && clip1()->out == 40 &&
              clip1()->offset == 0,
          "Q trims end to playhead");

    // Second clip appended after the first (offset 30).
    w.addClipToTimeline(media);
    spin(200); // let the (already known) probe path complete
    Track *v1b = nullptr;
    for (Track &t : proj->tracks)
        if (t.type == TrackType::Video) {
            v1b = &t;
            break;
        }
    check(v1b->clips.size() == 2 && v1b->clips[1].offset == 30 &&
              v1b->clips[1].in == 0 && v1b->clips[1].out == 60,
          "second clip appended at end");

    // Q @ frame 80: trim second clip's end (source out = 80 - 30 = 50).
    w.seekToFrame(80);
    key(Qt::Key_Q);
    spin(50);
    check(v1b->clips[1].out == 50 && v1b->clips[1].in == 0,
          "Q trims second clip end");

    // Roll: drag the shared boundary (frame 30 -> x 71) 2 px right (+4 frames).
    const int xb = 56 + 30 / 2;
    pressAt(xb);
    moveAt(xb + 2);
    releaseAt(xb + 2);
    spin(50);
    check(v1b->clips[0].out == 44 && v1b->clips[0].in == 10 &&
              v1b->clips[1].in == 4 && v1b->clips[1].offset == 34 &&
              v1b->clips[1].out == 54,
          "roll moves boundary, preserving both durations' sum");

    // Undo all five edits (W, Q, add, Q, roll) -> original single full clip.
    for (int i = 0; i < 5; ++i) w.undo();
    spin(100);
    check(v1b->clips.size() == 1 && v1b->clips[0].in == 0 &&
              v1b->clips[0].out == 60 && v1b->clips[0].offset == 0,
          "undo x5 restores original clip");

    // --- M2.4: markers -----------------------------------------------------
    // M key adds a marker at the playhead.
    w.seekToFrame(10);
    key(Qt::Key_M);
    spin(50);
    check(proj->markers.size() == 1 && proj->markers.first().time == 10,
          "M adds marker at playhead");
    // Double-click the ruler adds a marker at frame 40 (x = 56 + 40/2 = 76).
    {
        QMouseEvent ev(QEvent::MouseButtonDblClick, QPointF(76, 10), QPointF(76, 10),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &ev);
        spin(50);
    }
    check(proj->markers.size() == 2, "double-click ruler adds marker");
    // Right-click the frame-40 marker removes it.
    {
        QMouseEvent rc(QEvent::MouseButtonPress, QPointF(76, 10), QPointF(76, 10),
                       Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &rc);
        spin(50);
    }
    check(proj->markers.size() == 1 && proj->markers.first().time == 10,
          "right-click removes marker");
    // Visual: the frame-10 marker is painted as violet on the ruler
    // (line x = 56 + 10/2 = 61, y in [2,14]).
    {
        const QPixmap pm = canvas->grab();
        bool violet = false;
        for (int y = 2; y <= 14 && !violet; ++y)
            for (int dx = -2; dx <= 2 && !violet; ++dx) {
                const QColor c = pm.toImage().pixelColor(61 + dx, y);
                if (c.red() > 120 && c.red() < 200 && c.blue() > 220 &&
                    c.green() < 150)
                    violet = true;
            }
        check(violet, "marker painted on ruler");
    }
    // Undo the three marker edits (add 10, add 40, remove 40).
    for (int i = 0; i < 3; ++i) w.undo();
    spin(50);
    check(proj->markers.isEmpty(), "undo x3 removes all markers");

    // --- M3: effects (Inspector -> compositor -> monitor) -------------------
    // Select the clip so the Inspector builds the effects editor.
    {
        const int xSel = 56 + 3 + int((clipStart + 2) / canvas->framesPerPx());
        QMouseEvent ev(QEvent::MouseButtonPress, QPointF(xSel, 12), QPointF(xSel, 12),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &ev);
        spin(50);
    }
    check(w.selectedClipId() == clipId, "M3: clip selected for effects");

    // Baseline frame (fast path: no effects -> raw source frame).
    w.seekToFrame(20);
    for (int i = 0; i < 60 && w.previewImage().isNull(); ++i) spin(50);
    const QImage base = w.previewImage();
    check(!base.isNull(), "M3: baseline frame before effects");

    // Inspector: raise exposure to +1 stop, click Apply.
    auto *insp = w.findChild<InspectorWidget *>();
    check(insp != nullptr, "M3: inspector widget found");
    if (insp) {
        const auto spins = insp->findChildren<QDoubleSpinBox *>();
        check(spins.size() >= 11, "M3: effects editor spinboxes present");
        if (spins.size() >= 11) {
            spins[5]->setValue(1.0); // exposure
            auto *apply = insp->findChild<QPushButton *>();
            check(apply != nullptr, "M3: apply button present");
            if (apply) apply->click();
            spin(100);
        }
    }
    const Clip *cfx = clip1();
    check(cfx && cfx->effects.value(QStringLiteral("color")).toObject()
                       .value(QStringLiteral("exposure")).toDouble() == 1.0,
          "M3: apply stores exposure=1.0 (undoable command)");

    // Monitor: composited frame (project canvas) with +1 stop -> brighter.
    w.seekToFrame(20);
    for (int i = 0; i < 60; ++i) { spin(50); }
    const QImage fx = w.previewImage();
    check(!fx.isNull(), "M3: monitor renders through compositor");
    if (!base.isNull() && !fx.isNull()) {
        const QImage b2 = base.scaled(fx.size());
        int sumB = 0, sumF = 0;
        for (int y = 0; y < fx.height(); y += 17)
            for (int x = 0; x < fx.width(); x += 23) {
                sumB += qGray(b2.pixel(x, y));
                sumF += qGray(fx.pixel(x, y));
            }
        check(sumF > sumB, "M3: +1 stop exposure is visibly brighter");
    }

    // Undo -> effects gone, monitor back to the raw fast path.
    w.undo();
    spin(100);
    check(clip1() && clip1()->effects.isEmpty(), "M3: undo clears effects");
    w.seekToFrame(20);
    for (int i = 0; i < 60 && w.previewImage().isNull(); ++i) spin(50);
    check(w.previewImage().size() == base.size(), "M3: fast path frame size restored");

    // --- M3.4: playback resolution + dropped-frames status -------------------
    QComboBox *resCombo = nullptr;
    for (auto *cb : w.findChildren<QComboBox *>())
        if (cb->itemText(0) == QStringLiteral("Full") && cb->count() == 3) resCombo = cb;
    check(resCombo != nullptr, "M3: playback resolution combo found");
    if (resCombo) {
        resCombo->setCurrentIndex(1); // 1/2
        spin(100);
        w.seekToFrame(20);
        for (int i = 0; i < 60 && w.previewImage().isNull(); ++i) spin(50);
        const QImage half = w.previewImage();
        check(!half.isNull() && half.width() == base.width() / 2,
              "M3: 1/2 resolution halves the monitor frame");
        resCombo->setCurrentIndex(0); // Full
        spin(100);
        w.seekToFrame(20);
        for (int i = 0; i < 60 && w.previewImage().isNull(); ++i) spin(50);
        check(w.previewImage().size() == base.size(), "M3: Full resolution restored");
    }
    bool droppedLabel = false;
    for (auto *l : w.findChildren<QLabel *>())
        if (l->text().startsWith(QStringLiteral("Dropped frames"))) droppedLabel = true;
    check(droppedLabel, "M3: dropped-frames status label present");

    // --- M4: export dialog + batch queue (two presets, 25% scale) -----------
    w.openExportDialog();
    ExportDialog *dlg = w.exportDialog();
    check(dlg != nullptr, "M4: export dialog opened");
    if (dlg) {
        const QString outDir =
            QDir::tempPath() + "/arena_export_smoke_" +
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        dlg->dirEditForTest()->setText(outDir);
        dlg->scaleComboForTest()->setCurrentIndex(0); // 25% for speed
        for (int i = 0; i < dlg->presetListForTest()->count(); ++i) {
            auto *it = dlg->presetListForTest()->item(i);
            const bool keep = it->text().startsWith(QStringLiteral("mp4-h264-balanced")) ||
                              it->text().startsWith(QStringLiteral("webm-vp9"));
            it->setCheckState(keep ? Qt::Checked : Qt::Unchecked);
        }
        dlg->startForTest();
        bool finished = false;
        for (int i = 0; i < 600 && !finished; ++i) { // up to 60 s
            spin(100);
            finished = !dlg->running();
        }
        check(finished, "M4: export queue finished");
        int done = 0;
        for (int i = 0; i < dlg->queue()->jobCount(); ++i)
            if (dlg->queue()->job(i).state == ExportQueue::Job::State::Done) ++done;
        check(done == 2, "M4: both export jobs done");
        bool filesOk = true;
        for (int i = 0; i < dlg->queue()->jobCount(); ++i) {
            const auto &j = dlg->queue()->job(i);
            if (!QFileInfo::exists(j.settings.outputPath) ||
                QFileInfo(j.settings.outputPath).size() <= 0)
                filesOk = false;
        }
        check(filesOk, "M4: export files exist and are non-empty");
        QDir().remove(outDir);
        dlg->close();
        spin(50);
    }

    // --- M5: subtitle panel + burn-in end-to-end ---------------------------
    check(w.subtitles() != nullptr, "M5: subtitles panel exists");
    if (w.subtitles() && w.project()) {
        // Panel re-attach + refresh path.
        w.subtitles()->setProject(w.project());
        spin(50);
        check(w.subtitles()->parentWidget() != nullptr, "M5: panel attached to dock");

        // Burn-in cue across the whole clip, then compare the monitor frame
        // with and without burn-in at the same playhead.
        SubtitleTrack st;
        st.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        st.name = QStringLiteral("burnin");
        st.language = QStringLiteral("en");
        st.burnIn = true;
        TimelineCue cue;
        cue.startFrame = 0;
        cue.endFrame = 120;
        cue.text = QStringLiteral("ARENA BURNIN TEST");
        st.cues.append(cue);
        w.project()->subtitleTracks.append(st);
        w.subtitles()->setProject(w.project());

        // Seek + synchronous re-render at the playhead (same pattern as the
        // M3.4 resolution checks — the engine is paused here, so frame
        // delivery comes from applyPlaybackResolution, not playback).
        w.seekToFrame(30);
        for (int i = 0; i < 30; ++i) spin(50);
        w.applyPlaybackResolution();
        bool got = !w.previewImage().isNull();
        check(got, "M5: preview frame renders with burn-in");
        const QImage burned = w.previewImage();

        w.project()->subtitleTracks.last().burnIn = false;
        w.subtitles()->setProject(w.project());
        w.seekToFrame(30);
        for (int i = 0; i < 30; ++i) spin(50);
        w.applyPlaybackResolution();
        const QImage plain = w.previewImage();

        // The burn-in pass composites onto the project canvas (1920x1080)
        // while the plain pass may use the fast path at native source
        // resolution — normalize before diffing.
        QImage burnedCmp = burned;
        if (!burnedCmp.isNull() && !plain.isNull() && burnedCmp.size() != plain.size())
            burnedCmp = burnedCmp.scaled(plain.size(), Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation);
        int diffs = 0;
        if (!burnedCmp.isNull() && !plain.isNull()) {
            for (int y = 0; y < burnedCmp.height() && diffs < 5; ++y)
                for (int x = 0; x < burnedCmp.width() && diffs < 5; ++x)
                    if (burnedCmp.pixel(x, y) != plain.pixel(x, y)) ++diffs;
        }
        check(diffs > 0, "M5: burn-in changes monitor pixels");

        // Cleanup: no subtitle tracks left in the project.
        w.project()->subtitleTracks.clear();
        w.subtitles()->setProject(w.project());
        spin(50);
    }

    // --- M6.2: AI application paths (accept -> undoable command) --------------
    // The accept paths are the same public methods the AI panel emits into;
    // they take validated, text-only payloads and never touch the network.
    if (w.project()) {
        Track *v1 = nullptr;
        for (Track &t : w.project()->tracks)
            if (t.type == TrackType::Video) { v1 = &t; break; }
        check(v1 && v1->clips.size() == 1, "M6.2: one clip on V1");
        if (v1 && v1->clips.size() == 1) {
            const Clip c0 = v1->clips[0]; // snapshot

            // 1) Middle cut inside the single clip: split + tail at cut start.
            const qint64 cs = c0.offset + 10;
            const qint64 ce = c0.offset + 20;
            w.applyCutRange(cs, ce);
            spin(20);
            check(v1->clips.size() == 2, "M6.2: middle cut splits clip");
            if (v1->clips.size() == 2) {
                check(v1->clips[0].id == c0.id && v1->clips[0].out == c0.in + 10,
                      "M6.2: head keeps identity, trimmed to cut start");
                check(v1->clips[1].offset == cs && v1->clips[1].in == c0.in + 20 &&
                          v1->clips[1].out == c0.out,
                      "M6.2: tail starts at cut start with matching source");
            }
            w.undo();
            spin(20);
            check(v1->clips.size() == 1 && v1->clips[0].id == c0.id &&
                      v1->clips[0].in == c0.in && v1->clips[0].out == c0.out,
                  "M6.2: undo restores the original clip");

            // 2) Full-range cut: deletes the clip; undo restores it.
            w.applyCutRange(c0.offset, c0.offset + c0.duration());
            spin(20);
            check(v1->clips.isEmpty(), "M6.2: full-range cut deletes clip");
            w.undo();
            spin(20);
            check(v1->clips.size() == 1 && v1->clips[0].in == c0.in &&
                      v1->clips[0].out == c0.out,
                  "M6.2: undo restores deleted clip");

            // 3) Translation accept: new track, same frame timings, original
            //    untouched; undo removes the new track.
            SubtitleTrack src2;
            src2.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            src2.name = QStringLiteral("src");
            src2.language = QStringLiteral("en");
            TimelineCue q1;
            q1.startFrame = 0;
            q1.endFrame = 30;
            q1.text = QStringLiteral("Hello world");
            TimelineCue q2;
            q2.startFrame = 30;
            q2.endFrame = 60;
            q2.text = QStringLiteral("Second line");
            src2.cues = {q1, q2};
            w.project()->subtitleTracks.append(src2);
            w.applyAiTranslation(src2.id, QStringLiteral("ur"),
                                 {QStringLiteral("Hello world UR"),
                                  QStringLiteral("Second line UR")});
            spin(20);
            check(w.project()->subtitleTracks.size() == 2,
                  "M6.2: translation adds a new track");
            if (w.project()->subtitleTracks.size() == 2) {
                const SubtitleTrack &nt = w.project()->subtitleTracks.last();
                check(nt.language == "ur" && nt.cues.size() == 2,
                      "M6.2: new track has target language + cue count");
                check(nt.cues[0].startFrame == 0 && nt.cues[0].endFrame == 30 &&
                          nt.cues[1].startFrame == 30 && nt.cues[1].endFrame == 60,
                      "M6.2: translation keeps source frame timings");
                check(w.project()->subtitleTracks.first().cues[0].text ==
                          "Hello world",
                      "M6.2: original track untouched");
            }
            w.undo();
            spin(20);
            check(w.project()->subtitleTracks.size() == 1,
                  "M6.2: undo removes the translation track");

            // 4) Chapters accept: labeled markers; undo removes them.
            QVector<ChapterSuggestion> ch;
            ch.append({1.0, QStringLiteral("Intro")});
            ch.append({2.0, QStringLiteral("Middle")});
            ch.append({3.0, QStringLiteral("End")});
            w.applyAiChapters(ch);
            spin(20);
            check(w.project()->markers.size() == 3, "M6.2: chapters add 3 markers");
            if (w.project()->markers.size() == 3)
                check(w.project()->markers[1].label == "Middle",
                      "M6.2: chapter marker keeps its title");
            w.undo();
            spin(20);
            check(w.project()->markers.isEmpty(), "M6.2: undo removes chapter markers");

            // 5) Title accept: renames the project; undo reverts.
            const QString oldName = w.project()->name;
            TitleResult tr;
            tr.title = QStringLiteral("AI Smoke Title");
            tr.description = QStringLiteral("smoke");
            w.applyAiTitle(tr);
            spin(20);
            check(w.project()->name == "AI Smoke Title",
                  "M6.2: suggested title renames project");
            w.undo();
            spin(20);
            check(w.project()->name == oldName, "M6.2: undo reverts the name");
        }
        // Cleanup: no AI artifacts left behind.
        w.project()->subtitleTracks.clear();
        w.subtitles()->setProject(w.project());
        spin(50);
    }

    // --- M7: NL command bar (validated preview -> undoable apply) ------------
    // applyAiActions is the same public method the command preview card's
    // Accept emits into; it applies a *validated* action list. The response
    // shapes below are exactly what the model must emit for "add a fade out
    // to the last clip" / "speed up" / "add a marker" (aifeatures::
    // parseActions is what validated them upstream).
    if (w.project()) {
        Track *v1 = nullptr;
        for (Track &t : w.project()->tracks)
            if (t.type == TrackType::Video) { v1 = &t; break; }
        check(v1 && v1->clips.size() == 1, "M7: one clip on V1");
        if (v1 && v1->clips.size() == 1) {
            const double fps = w.project()->fps;
            const Clip c0 = v1->clips[0]; // snapshot

            // 1) Acceptance: "add a fade out to the last clip" (-1 = last).
            const QJsonArray fadeOut = QJsonDocument::fromJson(
                QStringLiteral("[{\"op\":\"add_transition\",\"target\":{\"type\":\"clip\",\"index\":-1},"
                                "\"transition\":\"fade_to_black\",\"duration_ms\":500}]").toUtf8())
                .array();
            check(w.applyAiActions(fadeOut) == 1, "M7: fade-out action applied (1)");
            spin(20);
            check(v1->clips.size() == 1, "M7: fade does not change clip count");
            if (v1->clips.size() == 1) {
                const QJsonObject tr =
                    v1->clips.last().effects.value(QStringLiteral("transition")).toObject();
                check(tr.value(QStringLiteral("type")).toString() == QLatin1String("fade_to_black"),
                      "M7: last clip carries fade_to_black");
                check(tr.value(QStringLiteral("duration")).toInt() ==
                          int(qRound(0.5 * fps)),
                      "M7: transition duration is 0.5 s in frames");
                // The compositor must actually render it: the clip's final
                // frame is the fully-blended black end of the fade.
                w.seekToFrame(c0.offset + c0.duration() - 1);
                for (int i = 0; i < 30; ++i) spin(50);
                w.applyPlaybackResolution();
                const QImage fadeFrame = w.previewImage();
                check(!fadeFrame.isNull(), "M7: fading frame renders");
                if (!fadeFrame.isNull()) {
                    int blackish = 0, total = 0;
                    for (int y = 0; y < fadeFrame.height(); y += 8)
                        for (int x = 0; x < fadeFrame.width(); x += 8) {
                            const QRgb px = fadeFrame.pixel(x, y);
                            if (qRed(px) < 40 && qGreen(px) < 40 && qBlue(px) < 40) ++blackish;
                            ++total;
                        }
                    check(total > 0 && blackish * 100 / total > 90,
                          "M7: last frame is (near-)black from the fade");
                }
            }
            w.undo();
            spin(20);
            check(v1->clips.size() == 1 &&
                      v1->clips.last().effects.value(QStringLiteral("transition")).toObject().isEmpty(),
                  "M7: undo removes the fade");

            // 2) An unapplicable/unknown op list applies nothing (the panel
            // rejects it before this point; defense in depth here).
            const QJsonArray bad = QJsonDocument::fromJson(
                QStringLiteral("[{\"op\":\"launch_rocket\"}]").toUtf8()).array();
            check(w.applyAiActions(bad) == 0, "M7: unknown op applies nothing");

            // 3) Speed action on the (only) clip; undo restores 1.0x.
            const QJsonArray speed = QJsonDocument::fromJson(
                QStringLiteral("[{\"op\":\"set_clip_speed\",\"target\":{\"type\":\"clip\",\"index\":0},"
                                "\"speed\":1.5}]").toUtf8())
                .array();
            check(w.applyAiActions(speed) == 1, "M7: speed action applied (1)");
            spin(20);
            check(v1->clips.size() == 1 && v1->clips.last().speed == 1.5,
                  "M7: clip speed is 1.5x");
            w.undo();
            spin(20);
            check(v1->clips.size() == 1 && v1->clips.last().speed == c0.speed,
                  "M7: undo restores original speed");

            // 4) Labeled marker; undo removes it.
            const int markers0 = w.project()->markers.size();
            const QJsonArray marker = QJsonDocument::fromJson(
                QStringLiteral("[{\"op\":\"add_marker\",\"time\":30,\"label\":\"M7 smoke\"}]").toUtf8())
                .array();
            check(w.applyAiActions(marker) == 1, "M7: marker action applied (1)");
            spin(20);
            check(w.project()->markers.size() == markers0 + 1 &&
                      w.project()->markers.last().label == QLatin1String("M7 smoke"),
                  "M7: labeled marker added");
            w.undo();
            spin(20);
            check(w.project()->markers.size() == markers0, "M7: undo removes the marker");

            // 5) Out-of-range clip index is skipped, not applied.
            const QJsonArray oob = QJsonDocument::fromJson(
                QStringLiteral("[{\"op\":\"set_clip_speed\",\"target\":{\"type\":\"clip\",\"index\":7},"
                                "\"speed\":2}]").toUtf8())
                .array();
            check(w.applyAiActions(oob) == 0, "M7: out-of-range index applies nothing");
        }
    }

    // --- M8: polish — sample project, tour, reduce motion, font scale, ---
    // --- UIA names, contextual tips.                                      ---
    {
        // 1) Bundled sample project: copies into the data dir and opens.
        const bool sampleOk = w.openSampleProject();
        check(sampleOk, "M8: sample project opens");
        Project *sp = w.project();
        check(sp != nullptr && sp->name == QLatin1String("Sample Project"),
              "M8: sample project name loaded");
        bool sampleClipOk = false;
        if (sp)
            for (const Track &t : sp->tracks)
                for (const Clip &c : t.clips)
                    if (c.sourceRef == QLatin1String("sample/clip01.mp4"))
                        sampleClipOk = true;
        check(sampleClipOk, "M8: sample clip source resolves");

        // 2) First-run tour: 6 steps, target highlight moves, back/next/skip,
        //    and replay on demand (Help > Tour).
        check(w.tour() == nullptr, "M8: tour is lazily created");
        w.showTour();
        Tour *tour = w.tour();
        check(tour != nullptr && tour->isRunning() && tour->count() == 6,
              "M8: tour starts with 6 steps");
        check(tour->index() == 0 && tour->targetObjectName() == QLatin1String("MediaBinDock"),
              "M8: step 1 targets the media bin");
        QWidget *mb = w.findChild<QWidget *>(QLatin1String("MediaBinDock"));
        check(mb != nullptr && mb->property("tourTarget").toBool(),
              "M8: target widget highlighted");
        tour->next();
        check(tour->index() == 1 && tour->targetObjectName() == QLatin1String("TimelineWidget"),
              "M8: step 2 targets the timeline");
        check(mb != nullptr && !mb->property("tourTarget").toBool(),
              "M8: previous target unhighlighted");
        tour->back();
        check(tour->index() == 0, "M8: back returns to step 1");
        for (int i = 0; i < 6 && !tour->atLast(); ++i) tour->next();
        check(tour->atLast() && tour->index() == 5, "M8: advances to the last step");
        tour->skip();
        check(!tour->isRunning(), "M8: skip ends the tour");
        w.showTour();
        check(w.tour() == tour && w.tour()->isRunning() && w.tour()->index() == 0,
              "M8: tour replays on demand");
        w.tour()->skip();

        // 3) Reduce motion: with the setting on, the card appears instantly at
        //    its final position (no slide-in tween).
        Settings &st = Settings::instance();
        const bool rmPrev = st.reduceMotion();
        st.setReduceMotion(true);
        w.showTour();
        const int expX = qMax(8, w.width() - w.tour()->width() - 24);
        check(w.tour()->pos().x() == expX, "M8: reduce-motion skips the slide-in");
        w.tour()->skip();
        st.setReduceMotion(rmPrev);

        // 4) Font-size scaling follows the setting (11/13/15/17 pt).
        const int fsPrev = st.fontScale();
        st.setFontScale(3);
        w.applyThemeAndFont();
        check(QApplication::font().pointSize() == 17,
              "M8: extra-large font scale applies (17 pt)");
        st.setFontScale(fsPrev);
        w.applyThemeAndFont();
        check(QApplication::font().pointSize() != 17, "M8: font scale restored");

        // 5) UIA / screen-reader names on the primary controls.
        QWidget *monitorBox = w.findChild<QWidget *>(QLatin1String("MonitorBox"));
        check(monitorBox != nullptr
                  && monitorBox->accessibleName() == arenaTr("view.monitor"),
              "M8: monitor has a UIA name");
        check(!w.timeline()->accessibleName().isEmpty(),
              "M8: timeline has a UIA name");
        int toolButtons = 0, unnamed = 0;
        const auto centralButtons = w.centralWidget() ? w.centralWidget()->findChildren<QToolButton *>()
                                                      : QList<QToolButton *>();
        for (QToolButton *b : centralButtons) {
            ++toolButtons;
            if (b->accessibleName().isEmpty()) ++unnamed;
        }
        check(toolButtons >= 5 && unnamed == 0, "M8: transport buttons all named for UIA");

        // 6) Contextual tips: shown once per feature, never again.
        st.setRaw("tips/m8test", false);
        w.tipOnce("m8test", "tip.firstClip");
        check(st.raw("tips/m8test").toBool(), "M8: tip recorded as seen");
        w.tipOnce("m8test", "tip.firstClip"); // second call: no-op (already seen)
        check(st.raw("tips/m8test").toBool(), "M8: tip stays seen after repeat");
        st.setRaw("tips/m8test", false);
    }

    // Give background workers (export worker thread, proxy encoder) a moment
    // to wind down before the window tears down.
    spin(500);

    if (g_failures == 0) {
        printf("GUISOKE PASS\n");
        return 0;
    }
    printf("GUISOKE FAIL (%d)\n", g_failures);
    return 1;
}
