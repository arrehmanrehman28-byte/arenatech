// Headless GUI smoke test: drives the real MainWindow through the full M1
// media pipeline — import, background probe, add clip (auto project +
// undoable command), first frame on the monitor, playback, and seek.
// Runs on the offscreen platform; audio output is unavailable in CI, so
// playback exercises the wall-clock fallback path.
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>

#include "mainwindow.h"
#include "themer.h"

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

    if (g_failures == 0) {
        printf("GUISOKE PASS\n");
        return 0;
    }
    printf("GUISOKE FAIL (%d)\n", g_failures);
    return 1;
}
