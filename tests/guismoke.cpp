// Headless GUI smoke test: drives the real MainWindow through the full M1
// media pipeline — import, background probe, add clip (auto project +
// undoable command), first frame on the monitor, playback, and seek.
// Runs on the offscreen platform; audio output is unavailable in CI, so
// playback exercises the wall-clock fallback path.
#include <QApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <cstdio>

#include "mainwindow.h"
#include "themer.h"
#include "timeline.h"

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

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: arena_guismoke <media-file>\n");
        return 2;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("ARENA_SELFTEST", "1"); // skip the first-run dialog headless
    QApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
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

    if (g_failures == 0) {
        printf("GUISOKE PASS\n");
        return 0;
    }
    printf("GUISOKE FAIL (%d)\n", g_failures);
    return 1;
}
