#include "kilogateway.h"
#include "keystore.h"
#include "settings.h"
#include "logger.h"
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDateTime>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QMetaObject>
#include <QTimer>

namespace arena {

// --- AIBudget ----------------------------------------------------------------

AIBudget::AIBudget(Settings &settings, QObject *parent)
    : QObject(parent), m_settings(settings) {}

int AIBudget::windowStartMs() const
{
    return m_settings.raw(QStringLiteral("ai/budgetWindowStart")).toLongLong(0);
}

int AIBudget::used() const
{
    if (windowStartMs() == 0) return 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - windowStartMs() >= 3600 * 1000) return 0; // window rolled over
    return m_settings.raw(QStringLiteral("ai/budgetUsed")).toInt(0);
}

int AIBudget::limit() const { return m_settings.aiHourlyBudget(); }

bool AIBudget::canSpend() const { return used() < limit(); }

void AIBudget::spend()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (windowStartMs() == 0 || now - windowStartMs() >= 3600 * 1000) {
        m_settings.setRaw(QStringLiteral("ai/budgetWindowStart"), now);
        m_settings.setRaw(QStringLiteral("ai/budgetUsed"), 1);
    } else {
        m_settings.setRaw(QStringLiteral("ai/budgetUsed"), used() + 1);
    }
    emit changed(used(), limit());
}

// --- KiloClient ---------------------------------------------------------------

KiloClient::KiloClient(Settings &settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_nam(new QNetworkAccessManager(this)),
      m_budget(settings)
{
    m_nam->setProxy(QNetworkProxy::NoProxy);
}

void KiloClient::refreshModels()
{
    QNetworkRequest req(QUrl(kBaseUrl + QStringLiteral("/models")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QString key = Keystore::readKey();
    if (!key.isEmpty()) req.setRawHeader("Authorization", ("Bearer " + key).toUtf8());
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            logError(QStringLiteral("Kilo gateway /models failed: %1")
                         .arg(reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QList<KiloModel> list;
        for (const QJsonValue &v : doc.object().value(QStringLiteral("data")).toArray()) {
            const QJsonObject o = v.toObject();
            KiloModel m;
            m.id = o.value(QStringLiteral("id")).toString();
            m.name = o.value(QStringLiteral("name")).toString(m.id);
            m.isFree = o.value(QStringLiteral("isFree")).toBool(false)
                       || m.id.endsWith(QLatin1String(":free"));
            if (!m.id.isEmpty()) list.append(m);
        }
        m_models = list;
        m_modelsLoaded = !list.isEmpty();
        logInfo(QStringLiteral("Kilo gateway models: %1 (free: %2)")
                    .arg(list.size(), freeModelIds().size()));
        emit modelsReady(list);
    });
}

QStringList KiloClient::freeModelIds() const
{
    QStringList out;
    for (const KiloModel &m : m_models)
        if (m.isFree) out.append(m.id);
    return out;
}

QString KiloClient::currentModel() const
{
    const QString cfg = m_settings.aiModel();
    if (cfg != QLatin1String("auto") && !cfg.isEmpty()) return cfg;
    const QStringList free = freeModelIds();
    return free.isEmpty() ? QStringLiteral("auto") : free.first();
}

void KiloClient::setCurrentModel(const QString &id) { m_settings.setAiModel(id); }

bool KiloClient::apiKeyConfigured() const { return !Keystore::readKey().isEmpty(); }

QString KiloClient::cacheDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("ai-cache"));
}

void KiloClient::clearCache()
{
    QDir(cacheDirectory()).removeRecursively();
    logInfo(QStringLiteral("AI cache cleared"));
}

void KiloClient::chat(const QString &systemPrompt, const QString &userPrompt,
                      std::function<void(const QString &)> onDelta,
                      std::function<void(const QString &, const QString &)> onDone)
{
    if (!m_settings.aiEnabled()) {
        if (onDone) onDone(QString(), QStringLiteral("AI is disabled"));
        return;
    }
    if (!m_budget.canSpend()) {
        if (onDone) onDone(QString(), QStringLiteral("hourly budget exhausted"));
        return;
    }
    auto *call = new ChatCall;
    call->system = systemPrompt;
    call->user = userPrompt;
    call->onDelta = std::move(onDelta);
    call->onDone = std::move(onDone);
    m_queue.append(call);
    startNext();
}

void KiloClient::startNext()
{
    if (m_active || m_queue.isEmpty()) return;
    m_active = m_queue.takeFirst();
    QString cached;
    if (cacheGet(*m_active, cached)) {
        if (m_active->onDelta) m_active->onDelta(cached);
        finishCall(m_active, QString());
        return;
    }
    sendChat(m_active);
}

void KiloClient::sendChat(ChatCall *call)
{
    Q_ASSERT(!m_reply);
    QNetworkRequest req(QUrl(kBaseUrl + QStringLiteral("/chat/completions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(30 * 1000); // 30 s default timeout
    const QString key = Keystore::readKey();
    if (!key.isEmpty()) req.setRawHeader("Authorization", ("Bearer " + key).toUtf8());

    QJsonObject body;
    body[QStringLiteral("model")] = currentModel();
    QJsonArray messages;
    if (!call->system.isEmpty()) {
        QJsonObject s;
        s[QStringLiteral("role")] = QStringLiteral("system");
        s[QStringLiteral("content")] = call->system;
        messages.append(s);
    }
    QJsonObject u;
    u[QStringLiteral("role")] = QStringLiteral("user");
    u[QStringLiteral("content")] = call->user;
    messages.append(u);
    body[QStringLiteral("messages")] = messages;
    body[QStringLiteral("stream")] = true;
    body[QStringLiteral("max_tokens")] = 1500;

    m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, call, reply] {
        call->buffer += reply->readAll();
        call->firstByteReceived = true;
        // SSE: lines of "data: {json}", terminated by [DONE].
        while (const int nl = call->buffer.indexOf('\n')) {
            const QByteArray line = call->buffer.left(nl).trimmed();
            call->buffer.remove(0, nl + 1);
            if (!line.startsWith("data:")) continue;
            const QByteArray payload = line.mid(5).trimmed();
            if (payload == QLatin1String("[DONE]")) {
                if (!call->done) { call->done = true; }
                continue;
            }
            const QJsonDocument d = QJsonDocument::fromJson(payload);
            if (!d.isObject()) continue;
            const QJsonArray choices = d.object().value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) continue;
            const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
            // Real gateway finding: reasoning models stream delta.reasoning
            // and may leave delta.content null — surface both so the user
            // always sees text.
            const QString content = delta.value(QStringLiteral("content")).toString();
            const QString reasoning = delta.value(QStringLiteral("reasoning")).toString();
            if (!content.isEmpty() && call->onDelta) {
                call->streamed += content;
                call->onDelta(content);
            } else if (!reasoning.isEmpty() && call->onDelta) {
                call->streamed += reasoning;
                call->onDelta(reasoning);
            }
            const QString finish = choices.first().toObject().value(QStringLiteral("finish_reason")).toString();
            if (!finish.isEmpty()) call->done = true;
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, call, reply] {
        reply->deleteLater();
        if (m_reply == reply) m_reply = nullptr;
        if (call->done) {
            m_budget.spend();
            finishCall(call, QString());
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool noBody = !call->firstByteReceived;
        // Backoff + jitter on 429 / 5xx, only before any content arrived.
        if (noBody && call->retries < 3 && (status == 429 || (status >= 500 && status < 600))) {
            const int base = 1000 * (1 << call->retries); // 1s, 2s, 4s
            const int delay = base + int(QRandomGenerator::global()->bounded(base / 2));
            ++call->retries;
            QTimer::singleShot(delay, this, [this, call] {
                if (m_active == call) sendChat(call);
            });
            return;
        }
        const QString err = reply->error() == QNetworkReply::NoError
                                ? QStringLiteral("incomplete stream")
                                : reply->errorString();
        if (!call->streamed.isEmpty()) {
            // Partial content is still useful; report success with a note.
            finishCall(call, QStringLiteral("stream interrupted: ") + err);
        } else {
            finishCall(call, err);
        }
    });
}

void KiloClient::onChatError(ChatCall *, const QString &, int) {}

void KiloClient::finishCall(ChatCall *call, const QString &error)
{
    if (m_active == call) m_active = nullptr;
    if (!error.isEmpty() && call->streamed.isEmpty())
        logError(QStringLiteral("AI chat failed"), error);
    if (error.isEmpty() && !call->streamed.isEmpty())
        cachePut(*call, call->streamed);
    if (call->onDone) call->onDone(call->streamed, error);
    delete call;
    startNext();
}

void KiloClient::cancel()
{
    if (m_reply) {
        m_reply->abort();
        // 'finished' handler will clean up the active call.
    }
    // Drop queued (not yet started) calls.
    while (!m_queue.isEmpty()) {
        ChatCall *c = m_queue.takeFirst();
        if (c->onDone) c->onDone(QString(), QStringLiteral("cancelled"));
        delete c;
    }
}

bool KiloClient::cacheGet(const ChatCall &call, QString &out) const
{
    const QByteArray keyBytes = QCryptographicHash::hash(
        (currentModel() + QLatin1Char('|') + call.system + QLatin1Char('|') + call.user).toUtf8(),
        QCryptographicHash::Sha1);
    const QString path = QDir(cacheDirectory()).filePath(QString::fromLatin1(keyBytes.toHex()) + QStringLiteral(".json"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    out = d.object().value(QStringLiteral("content")).toString();
    return !out.isEmpty();
}

void KiloClient::cachePut(const ChatCall &call, const QString &content)
{
    QDir dir(cacheDirectory());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return;
    const QByteArray keyBytes = QCryptographicHash::hash(
        (currentModel() + QLatin1Char('|') + call.system + QLatin1Char('|') + call.user).toUtf8(),
        QCryptographicHash::Sha1);
    QFile f(dir.filePath(QString::fromLatin1(keyBytes.toHex()) + QStringLiteral(".json")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonObject o;
        o[QStringLiteral("model")] = currentModel();
        o[QStringLiteral("content")] = content;
        o[QStringLiteral("ts")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
}

} // namespace arena
