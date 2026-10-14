// M9 release benchmark: export encode FPS through the *real* ExportQueue
// worker path (the same stateless fetchers the UI uses), so the number in
// PERFORMANCE.md is the shipped pipeline, not a synthetic frame source.
//
// Usage:
//   arena_export_bench <media-file> [preset ...]
// e.g.:
//   arena_export_bench tests/fixtures/clip01.mp4 mp4-h264-balanced webm-vp9
//
// Prints, per preset: frames, wall time, encode FPS, and output size.
// Skips cleanly when built without FFmpeg (no-op exporter).
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QVector>
#include <cstdio>
#include <memory>

#include "exporter.h"
#include "exportqueue.h"
#include "media.h"
#include "project.h"

using namespace arena;

namespace {

// Build a one-clip project from a real media file: the clip spans the whole
// source at the probed resolution/fps.
std::unique_ptr<Project> makeProject(const MediaInfo &info)
{
    auto p = std::make_unique<Project>();
    p->width = info.width > 0 ? info.width : 1920;
    p->height = info.height > 0 ? info.height : 1080;
    p->fps = info.fps > 0.0 ? info.fps : 30.0;
    p->name = QStringLiteral("export-bench");

    Track t;
    t.id = QStringLiteral("V1");
    t.type = TrackType::Video;
    t.name = QStringLiteral("V1");

    Clip c;
    c.id = QStringLiteral("c1");
    c.sourceRef = info.path;
    c.in = 0;
    const double fps = p->fps;
    c.out = info.durationMs > 0
                ? qMax<qint64>(1, qRound(info.durationMs / 1000.0 * fps))
                : qMax<qint64>(1, qRound(30.0 * 1.0)); // ~3 s default
    c.offset = 0;
    t.clips.append(c);
    p->tracks.append(t);
    return p;
}

// Frame fetcher identical in shape to MainWindow::openExportDialog: a
// stateless decoder opened per call (export never touches the playback engine).
Compositor::FrameFetcher makeFrameFetcher()
{
    return [](const QString &source, qint64 frame) -> QImage {
        QImage img;
        VideoDecoder dec;
        if (dec.open(source)) {
            QImage tmp;
            if (dec.frameAtSec(double(frame) / qMax(0.1, dec.fps()), &tmp))
                img = tmp;
        }
        return img;
    };
}

Exporter::AudioFetcher makeAudioFetcher(const MediaInfo &info, double projectFps)
{
    const double srcFps = info.fps > 0.0 ? info.fps : projectFps;
    return [srcFps](const QString &source, qint64 inFrame, qint64 outFrame, int rate,
                    int channels, QVector<float> *out) {
        const double secIn = double(inFrame) / srcFps;
        const double secOut = double(outFrame) / srcFps;
        AudioReader reader;
        if (!reader.open(source))
            return false;
        reader.setOutput(rate, channels);
        if (!reader.seekSec(secIn))
            return false;
        const int total = int((secOut - secIn) * rate + 0.5);
        out->resize(total * channels);
        int got = 0;
        while (got < total * channels) {
            const int n = reader.readInterleaved(out->data() + got, total * channels - got);
            if (n <= 0)
                break;
            got += n;
        }
        if (got < total * channels)
            out->resize(got);
        return got > 0;
    };
}

bool runOne(const MediaInfo &info, const QString &preset, QTemporaryDir *tmp)
{
    auto project = makeProject(info);
    const ExportSettings s = Exporter::preset(preset, *project);
    const QString out = tmp->filePath(preset + QStringLiteral(".") + s.container);

    ExportQueue q;
    ExportQueue::Job job;
    job.name = preset;
    job.settings = s;
    job.settings.outputPath = out;
    job.project = *project;
    job.fetch = makeFrameFetcher();
    job.audio = makeAudioFetcher(info, project->fps);
    const int idx = q.addJob(std::move(job));

    bool done = false;
    bool ok = false;
    QString err;
    QObject::connect(&q, &ExportQueue::jobFinished, &q,
                     [&](int i, bool good, const QString &e) {
                         if (i == idx) {
                             ok = good;
                             err = e;
                             done = true;
                         }
                     });
    q.start();
    QElapsedTimer wall;
    wall.start();
    QEventLoop loop;
    QObject::connect(&q, &ExportQueue::queueFinished, &loop, &QEventLoop::quit);
    // Safety: bail after 20 min so a stuck encode can't hang CI forever.
    QTimer::singleShot(20 * 60 * 1000, &loop, &QEventLoop::quit);
    while (!done && wall.elapsed() < 20 * 60 * 1000)
        loop.processEvents(QEventLoop::WaitForMoreEvents, 100);
    const qint64 ms = wall.elapsed();

    const ExportQueue::Job result = q.job(idx);
    const qint64 frames = result.framesTotal > 0 ? result.framesTotal : result.framesDone;
    const double fps = ms > 0 ? double(frames) / (ms / 1000.0) : 0.0;
    const QFileInfo fi(out);
    const qint64 bytes = ok ? fi.size() : -1;
    printf("  %-22s frames=%lld  wall=%6.2f s  encodeFPS=%7.2f  out=%s\n",
           preset.toUtf8().constData(), (long long)frames, ms / 1000.0, fps,
           ok ? fi.fileName().toUtf8().constData() : "(failed)");
    printf("        output: %s (%lld bytes)  state=%s  %s\n",
           ok ? "OK" : "FAIL", (long long)bytes,
           ok ? "Done" : result.error.toUtf8().constData(),
           err.toUtf8().constData());
    return ok && result.state == ExportQueue::Job::State::Done;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        fprintf(stderr, "usage: arena_export_bench <media-file> [preset ...]\n");
        return 2;
    }
    const QString media = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    if (!QFile::exists(media)) {
        fprintf(stderr, "media file not found: %s\n", media.toUtf8().constData());
        return 2;
    }
    if (!Exporter::isAvailable()) {
        printf("exporter not available (built without FFmpeg) — skipping\n");
        return 0;
    }

    MediaInfo info;
    QString err;
    if (!MediaProbe::probeFull(media, &info, &err)) {
        fprintf(stderr, "probe failed: %s\n", err.toUtf8().constData());
        return 2;
    }
    printf("== Arena Edit export benchmark (real pipeline) ==\n");
    printf("media: %s  %dx%d @ %.3f fps  %lld ms  v:%s a:%s\n",
           media.toUtf8().constData(), info.width, info.height, info.fps,
           (long long)info.durationMs, info.videoCodec.toUtf8().constData(),
           info.audioCodec.toUtf8().constData());

    QStringList presets;
    for (int i = 2; i < argc; ++i)
        presets.append(QString::fromLocal8Bit(argv[i]));
    if (presets.isEmpty())
        presets = {QStringLiteral("mp4-h264-balanced"), QStringLiteral("webm-vp9")};

    QTemporaryDir tmp;
    bool allOk = true;
    for (const QString &preset : presets) {
        if (!Exporter::presetNames().contains(preset)) {
            printf("  %-22s (unknown preset — available: %s)\n", preset.toUtf8().constData(),
                   Exporter::presetNames().join(",").toUtf8().constData());
            continue;
        }
        allOk = allOk && runOne(info, preset, &tmp);
    }
    return allOk ? 0 : 1;
}
