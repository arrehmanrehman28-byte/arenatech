// Dev tool: local STT end-to-end check (M7).
//
//   arena_stt_check [model] [audioPath]
//
// Downloads/verifies the whisper model (on demand), decodes the audio
// through the media pipeline, transcribes locally, and prints the segments
// + word spans as JSON along with wall time. Used for the M7 performance
// evidence ("bundled sample clip < 60 s on 4-core CPU") and as a smoke test
// of the on-demand model flow. Never touches the chat gateway.
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>

#include "whisper.h"
#include "media.h"

using namespace arena;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream sout(stdout);
    QTextStream err(stderr);

    const QString model = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                   : QStringLiteral("base");
    const QString audio = argc > 2
                              ? QString::fromLocal8Bit(argv[2])
                              : QStringLiteral("tests/fixtures/jfk.wav");

    if (!Transcriber::isAvailable()) {
        err << "whisper.cpp not compiled in (ARENA_ENABLE_WHISPER=OFF)\n";
        return 2;
    }

    if (!Transcriber::modelIsReady(model)) {
        err << "model '" << model << "' not ready — downloading to "
            << Transcriber::modelsDir() << " …\n";
        QString merr;
        if (!Transcriber::ensureModel(model, &merr, [](float f, qint64 done, qint64 total) {
                if (f == 0.0 || f == 1.0 || done - (static_cast<qint64>(f * total)) > (10 << 20))
                    QTextStream(stderr)
                        << QStringLiteral("\r  %1 / %2 MiB (%3%)")
                            .arg(done / (1024 * 1024))
                            .arg(total > 0 ? total / (1024 * 1024) : -1)
                            .arg(total > 0 ? int(f * 100) : 0)
                        << Qt::flush;
            })) {
            err << "download failed: " << merr << "\n";
            return 3;
        }
        err << "\nmodel ready.\n";
    }

    MediaInfo info;
    QString perr;
    if (MediaProbe::probeFull(audio, &info, &perr))
        err << "input: " << audio << " " << info.durationMs << " ms\n";
    else
        err << "input: " << audio << " (probe failed: " << perr << ")\n";

    Transcriber::Options opts;
    opts.model = model;
    QList<TranscriptSegment> out;
    QString terr;
    QString lang;
    QElapsedTimer t;
    t.start();
    const bool ok = Transcriber::transcribe(audio, opts, out, &terr, nullptr, &lang);
    const qint64 ms = t.elapsed();

    QJsonObject root;
    root[QStringLiteral("ok")] = ok;
    root[QStringLiteral("model")] = model;
    root[QStringLiteral("language")] = lang;
    root[QStringLiteral("elapsedMs")] = ms;
    root[QStringLiteral("threads")] = qMax(1, qMin(4, QThread::idealThreadCount()));
    QJsonArray segs;
    for (const TranscriptSegment &s : out) {
        QJsonObject so;
        so[QStringLiteral("startMs")] = s.startMs;
        so[QStringLiteral("endMs")] = s.endMs;
        so[QStringLiteral("text")] = s.text;
        QJsonArray ws;
        for (const WordSpan &w : s.words) {
            QJsonObject wo;
            wo[QStringLiteral("startMs")] = w.startMs;
            wo[QStringLiteral("endMs")] = w.endMs;
            wo[QStringLiteral("text")] = w.text;
            ws.append(wo);
        }
        so[QStringLiteral("words")] = ws;
        segs.append(so);
    }
    root[QStringLiteral("segments")] = segs;
    if (!terr.isEmpty()) root[QStringLiteral("error")] = terr;

    sout << QJsonDocument(root).toJson(QJsonDocument::Indented) << "\n";
    return ok ? 0 : 1;
}
