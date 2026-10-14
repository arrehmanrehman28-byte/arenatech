#include "whisper.h"
#include "logger.h"
#include "media.h"

#ifdef ARENA_ENABLE_WHISPER
// The vendored whisper.cpp header is a *different file* that also happens to
// be named whisper.h; pull it in by path so it cannot shadow (or be shadowed
// by) the arena interface header above.
#include "../third_party/whisper/include/whisper.h"
#endif

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QDebug>
#include <QThread>
#include <QUrl>
#include <vector>

namespace arena {

namespace {

// Known-good model files from the official whisper.cpp Hugging Face repo
// (ggerganov/whisper.cpp, main). Size + SHA-256 pinned 2026-10-14 so an
// on-demand download is verifiable without trusting the CDN alone.
struct ModelSpec {
    const char *id;
    qint64 sizeBytes;
    const char *sha256;
};

const QList<ModelSpec> kModels = {
    { "tiny", 77691713,
      "be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21" },
    { "base", 147951465,
      "60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe" },
    { "small", 487601967,
      "1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b" },
};

const QString kModelUrlBase =
    QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-");

const ModelSpec *specFor(const QString &id)
{
    for (const ModelSpec &m : kModels)
        if (QString::fromLatin1(m.id) == id)
            return &m;
    return nullptr;
}

QString modelPath(const QString &id)
{
    return QDir(Transcriber::modelsDir()).filePath(QStringLiteral("ggml-%1.bin").arg(id));
}

// Verify a model file against its pinned size + SHA-256.
bool verifyModelFile(const QString &path, const ModelSpec &spec, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("cannot read model file");
        return false;
    }
    QCryptographicHash sha(QCryptographicHash::Sha256);
    qint64 size = 0;
    for (;;) {
        const QByteArray chunk = f.read(1 << 20);
        if (chunk.isEmpty()) break;
        sha.addData(chunk);
        size += chunk.size();
    }
    if (size != spec.sizeBytes) {
        if (error)
            *error = QObject::tr("model size mismatch (%1, expected %2)")
                         .arg(size)
                         .arg(spec.sizeBytes);
        return false;
    }
    if (sha.result().toHex() != spec.sha256) {
        if (error) *error = QObject::tr("model checksum mismatch");
        return false;
    }
    return true;
}

#ifdef ARENA_ENABLE_WHISPER
// The library logs model-load/init details to stderr by default; the app has
// its own logging, so redirect those lines to our logger at debug level.
void whisperLogSink(ggml_log_level, const char *text, void *)
{
    // Debug level only: visible when the app's debug logging is enabled.
    if (text && *text)
        qCDebug(arenaLog) << QString::fromUtf8(text).trimmed();
}

// Whisper token ids for the timestamp/special ranges (per model vocab).
bool isSpecialToken(whisper_context *ctx, whisper_token id)
{
    if (id < 0) return true;
    // v1.9.x layout: the special-token region *starts* at beg (e.g. 50364:
    // [_BEG_] and the [_TT_*] timestamp tokens) and extends upward, while
    // eot may live below it in the ordinary vocab (50257). So check the
    // upper-open range plus the individual control tokens.
    if (id >= whisper_token_beg(ctx))
        return true;
    return id == whisper_token_sot(ctx) || id == whisper_token_solm(ctx) ||
           id == whisper_token_nosp(ctx) || id == whisper_token_not(ctx) ||
           id == whisper_token_eot(ctx);
}
#endif

} // namespace

QString Transcriber::modelsDir()
{
    // Overridable for tests/CI: ARENA_WHISPER_MODELS=/some/dir
    if (const QByteArray env = qgetenv("ARENA_WHISPER_MODELS"); !env.isEmpty())
        return QString::fromLocal8Bit(env);
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("whisper"));
}

QList<WhisperModelInfo> Transcriber::models()
{
    QList<WhisperModelInfo> out;
    for (const ModelSpec &m : kModels)
        out.append({ QString::fromLatin1(m.id),
                     QStringLiteral("%1 (fastest)").arg(m.id), m.sizeBytes });
    return out;
}

bool Transcriber::isAvailable()
{
#ifdef ARENA_ENABLE_WHISPER
    return true;
#else
    return false;
#endif
}

bool Transcriber::modelIsReady(const QString &id)
{
    const ModelSpec *spec = specFor(id);
    if (!spec) return false;
    const QString path = modelPath(id);
    return QFile::exists(path) && verifyModelFile(path, *spec, nullptr);
}

bool Transcriber::ensureModel(const QString &id, QString *error,
                              std::function<void(float, qint64, qint64)> onProgress)
{
    const ModelSpec *spec = specFor(id);
    if (!spec) {
        if (error) *error = QObject::tr("unknown model '%1'").arg(id);
        return false;
    }
    const QString path = modelPath(id);

    if (QFile::exists(path)) {
        if (verifyModelFile(path, *spec, error))
            return true;
        QFile::remove(path); // corrupt/partial — re-download
    }

#ifdef ARENA_ENABLE_WHISPER
    QDir().mkpath(modelsDir());
    const QString partPath = path + QStringLiteral(".part");
    QFile::remove(partPath);
    QFile part(partPath);
    if (!part.open(QIODevice::WriteOnly)) {
        if (error) *error = QObject::tr("cannot create model file");
        return false;
    }

    QNetworkAccessManager nam;
    QEventLoop loop;
    struct State {
        bool ok = false;
        QString error;
        qint64 total = 0;
        qint64 done = 0;
        QCryptographicHash sha = QCryptographicHash(QCryptographicHash::Sha256);
    };
    State st;

    QNetworkRequest req(QUrl(kModelUrlBase + id + QStringLiteral(".bin")));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                     [&st, onProgress](qint64 done, qint64 total) {
                         st.total = total;
                         if (onProgress)
                             onProgress(total > 0 ? double(done) / total : 0.0, done, total);
                     });
    QObject::connect(reply, &QNetworkReply::readyRead, reply,
                     [&st, &part, reply]() {
                         const QByteArray data = reply->readAll();
                         part.write(data);
                         st.sha.addData(data);
                         st.done += data.size();
                     });
    QObject::connect(reply, &QNetworkReply::finished, &loop,
                     [&loop, &st, reply] {
                         st.ok = reply->error() == QNetworkReply::NoError;
                         if (!st.ok) st.error = reply->errorString();
                         reply->deleteLater();
                         loop.quit();
                     });
    loop.exec();
    part.close();

    if (!st.ok) {
        QFile::remove(partPath);
        if (error) *error = st.error;
        return false;
    }
    if (st.done != spec->sizeBytes) {
        QFile::remove(partPath);
        if (error)
            *error = QObject::tr("model download size mismatch (%1, expected %2)")
                         .arg(st.done)
                         .arg(spec->sizeBytes);
        return false;
    }
    if (st.sha.result().toHex() != spec->sha256) {
        QFile::remove(partPath);
        if (error) *error = QObject::tr("model checksum mismatch");
        return false;
    }
    if (!QFile::rename(partPath, path)) {
        if (error) *error = QObject::tr("cannot move model file into place");
        return false;
    }
    logInfo(QStringLiteral("whisper model ready: %1").arg(path));
    if (onProgress) onProgress(1.0, st.done, st.total);
    return true;
#else
    Q_UNUSED(onProgress)
    Q_UNUSED(id)
    if (error)
        *error = QObject::tr("whisper.cpp is not compiled into this build (enable ARENA_ENABLE_WHISPER)");
    return false;
#endif
}

bool Transcriber::transcribe(const QString &audioPath, const Options &opts,
                             QList<TranscriptSegment> &out, QString *error,
                             std::function<void(float)> onProgress,
                             QString *detectedLanguage)
{
    out.clear();
    if (detectedLanguage) detectedLanguage->clear();

    if (!isAvailable()) {
        if (error)
            *error = QObject::tr("whisper.cpp is not compiled into this build (enable ARENA_ENABLE_WHISPER)");
        return false;
    }
    if (audioPath.isEmpty() || !QFile::exists(audioPath)) {
        if (error) *error = QObject::tr("audio file not found: %1").arg(audioPath);
        return false;
    }
    if (!modelIsReady(opts.model) &&
        !ensureModel(opts.model, error,
                     [onProgress](float f, qint64, qint64) {
                         if (onProgress) onProgress(f);
                     }))
        return false;

    // Decode to 16 kHz mono float32 (whisper's native input).
    AudioReader reader;
    QString aerr;
    if (!reader.open(audioPath, &aerr)) {
        if (error) *error = QObject::tr("audio decode failed: %1").arg(aerr);
        return false;
    }
    reader.setOutput(16000, 1);
    std::vector<float> samples;
    {
        constexpr int kChunk = 16000 * 2; // 2 s at a time
        std::vector<float> buf(kChunk);
        for (;;) {
            const int n = reader.readInterleaved(buf.data(), kChunk);
            if (n <= 0) break;
            samples.insert(samples.end(), buf.begin(), buf.begin() + n);
        }
    }
    if (samples.empty()) {
        if (error) *error = QObject::tr("no audio data in %1").arg(audioPath);
        return false;
    }

#ifdef ARENA_ENABLE_WHISPER
    whisper_log_set(whisperLogSink, nullptr); // keep library chatter out of the user console
    const QString path = modelPath(opts.model);
    whisper_context_params cp = whisper_context_default_params();
    // This build is CPU-only (CUDA/Vulkan backends are not compiled in);
    // state that explicitly instead of relying on the runtime fallback.
    cp.use_gpu = false;
    cp.flash_attn = false;
    struct whisper_context *ctx =
        whisper_init_from_file_with_params(path.toUtf8().constData(), cp);
    if (!ctx) {
        if (error) *error = QObject::tr("failed to load whisper model");
        return false;
    }

    // Explicit language only. Auto-detection is deliberately unsupported:
    // whisper.cpp v1.9.5 returns zero segments with detect_language=true on
    // short English samples (verified 2026-10-14, jfk.wav, tiny model).
    const QString langId = opts.language.isEmpty() ? QStringLiteral("en")
                                                   : opts.language;
    const QByteArray lang = langId.toUtf8();
    whisper_full_params fp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    fp.n_threads = qMax(1, qMin(4, QThread::idealThreadCount()));
    fp.no_context = true;
    fp.token_timestamps = opts.wordTimestamps;
    fp.suppress_blank = true;
    fp.suppress_nst = true;
    fp.print_progress = false;
    fp.language = lang.constData();
    fp.detect_language = false;

    int rc = whisper_full(ctx, fp, samples.data(),
                          static_cast<int>(samples.size()));
    if (rc != 0) {
        if (error) *error = QObject::tr("transcription failed (code %1)").arg(rc);
        whisper_free(ctx);
        return false;
    }

    if (detectedLanguage)
        *detectedLanguage = langId; // explicit: this build does not auto-detect

    const int nSeg = whisper_full_n_segments(ctx);
    for (int s = 0; s < nSeg; ++s) {
        TranscriptSegment seg;
        seg.startMs = whisper_full_get_segment_t0(ctx, s) / 10; // centisec → ms
        seg.endMs = whisper_full_get_segment_t1(ctx, s) / 10;
        seg.text = QString::fromUtf8(whisper_full_get_segment_text(ctx, s)).trimmed();
        if (seg.text.isEmpty())
            continue;

        if (opts.wordTimestamps) {
            const int nTok = whisper_full_n_tokens(ctx, s);
            QString word;
            qint64 w0 = 0, w1 = 0;
            bool inWord = false;
            auto flushWord = [&]() {
                const QString t = word.trimmed();
                if (!t.isEmpty())
                    seg.words.append({ w0, w1, t });
                word.clear();
                inWord = false;
            };
            for (int t = 0; t < nTok; ++t) {
                const whisper_token_data td = whisper_full_get_token_data(ctx, s, t);
                if (isSpecialToken(ctx, td.id)) {
                    flushWord();
                    continue;
                }
                const char *raw = whisper_full_get_token_text(ctx, s, t);
                if (!raw || !*raw)
                    continue;
                // A leading space marks the start of a new word.
                if (inWord && raw[0] == QLatin1Char(' '))
                    flushWord();
                const QString piece = QString::fromUtf8(raw).trimmed();
                if (piece.isEmpty())
                    continue;
                if (!inWord) {
                    w0 = td.t0 / 10;
                    inWord = true;
                }
                w1 = td.t1 / 10;
                if (!word.isEmpty()) word += QLatin1Char(' ');
                word += piece;
            }
            flushWord();
        }
        out.append(seg);
    }
    whisper_free(ctx);
    if (onProgress) onProgress(1.0);
    return true;
#else
    Q_UNUSED(audioPath)
    Q_UNUSED(opts)
    Q_UNUSED(onProgress)
    if (error)
        *error = QObject::tr("whisper.cpp is not compiled into this build (enable ARENA_ENABLE_WHISPER)");
    return false;
#endif
}

} // namespace arena
