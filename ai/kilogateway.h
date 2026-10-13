#pragma once
// Kilo Gateway client (OpenAI-compatible).
// Base URL: https://api.kilo.ai/api/gateway
// - GET  {base}/models
// - POST {base}/chat/completions  (streaming SSE)
// Auth: Authorization: Bearer <KILO_API_KEY> when a key is set; anonymous
// free-model use is allowed at a low rate limit (~200 req/hour per IP).
// Behavior: 30 s timeout, exponential backoff + jitter on 429/5xx, request
// queue, per-hour budget counter, local response cache keyed by
// hash(model, system, user). API keys are never logged.
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QStringList>
#include <QHash>
#include <functional>

namespace arena {

class Settings;

struct KiloModel {
    QString id;
    QString name;
    bool isFree = false;
};

// Per-hour request budget, persisted in settings; drives the status-bar meter.
class AIBudget : public QObject {
    Q_OBJECT
public:
    explicit AIBudget(Settings &settings, QObject *parent = nullptr);
    bool canSpend() const;
    void spend();
    int used() const;
    int limit() const;

signals:
    void changed(int used, int limit);

private:
    Settings &m_settings;
    int windowStartMs() const;
};

class KiloClient : public QObject {
    Q_OBJECT
public:
    static constexpr char kBaseUrl[] = "https://api.kilo.ai/api/gateway";

    explicit KiloClient(Settings &settings, QObject *parent = nullptr);

    // Models
    void refreshModels();
    QList<KiloModel> models() const { return m_models; }
    bool modelsLoaded() const { return m_modelsLoaded; }
    QStringList freeModelIds() const;
    QString currentModel() const; // honors settings; "auto" -> first free model
    void setCurrentModel(const QString &id);

    // Chat (streaming). Queues concurrent calls; one active at a time.
    // maxTokens caps generation; reasoning models spend it on chain-of-thought
    // first, so structured-output callers pass per-task budgets (aifeatures).
    void chat(const QString &systemPrompt, const QString &userPrompt,
              std::function<void(const QString &)> onDelta,
              std::function<void(const QString &fullText, const QString &error)> onDone,
              int maxTokens = 1500);
    void cancel();

    // Key + cache
    bool apiKeyConfigured() const;
    // Remove the cached response for one exact request. Call before
    // resending a prompt whose previous response failed validation —
    // otherwise the retry is served from cache and can never succeed.
    void invalidateCache(const QString &systemPrompt, const QString &userPrompt);
    static QString cacheDirectory();
    static void clearCache();
    AIBudget &budget() { return m_budget; }

signals:
    void modelsReady(const QList<KiloModel> &models);

private:
    struct ChatCall {
        QString system;
        QString user;
        int maxTokens = 1500;
        std::function<void(const QString &)> onDelta;
        std::function<void(const QString &, const QString &)> onDone;
        QByteArray buffer;
        QString streamed;
        bool done = false;
        int retries = 0;
        bool firstByteReceived = false;
    };

    void startNext();
    void sendChat(ChatCall *call);
    void onChatError(ChatCall *call, const QString &error, int httpStatus);
    void finishCall(ChatCall *call, const QString &error);
    static QString cachePath(const QString &model, const QString &systemPrompt,
                             const QString &userPrompt);
    bool cacheGet(const ChatCall &call, QString &out) const;
    void cachePut(const ChatCall &call, const QString &content);

    Settings &m_settings;
    QNetworkAccessManager *m_nam;
    QList<KiloModel> m_models;
    bool m_modelsLoaded = false;
    AIBudget m_budget;
    QList<ChatCall *> m_queue;
    ChatCall *m_active = nullptr;
    QNetworkReply *m_reply = nullptr;
};

} // namespace arena
