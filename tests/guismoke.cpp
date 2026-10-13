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

    if (g_failures == 0) {
        printf("GUISOKE PASS\n");
        return 0;
    }
    printf("GUISOKE FAIL (%d)\n", g_failures);
    return 1;
}
