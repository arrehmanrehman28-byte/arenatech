#pragma once
// M6: response parsing + validation for the four AI features (subtitle
// translation, chapters, titles, cut suggestions). Pure and offline:
// models emit raw text, and these functions decide whether it is
// structurally safe to show (and, after confirmation, apply). Anything
// that fails validation is rejected — never executed.
//
// Batch policy (spec): translation runs in batches of ~40 entries; a
// failed batch is retried once and only the still-failed batch is kept
// out — valid batches are never re-sent.
#include <QJsonObject>
#include <QPair>
#include <QVector>
#include <QString>
#include <QStringList>

namespace arena {

struct ChapterSuggestion {
    double startSec = 0.0;
    QString title;
};

struct TitleResult {
    QString title;
    QString description;
};

struct CutSuggestion {
    double startSec = 0.0;
    double endSec = 0.0;
    QString reason; // "silence" | "filler"
    double confidence = 0.0;
};

namespace aifeatures {

// Spec limits (the prompts demand exactly these).
constexpr int kTranslateBatchSize = 40; // entries per translation request

// Per-task token budgets for the gateway request. Free-tier models that
// think out loud (delta.reasoning) spend the budget on chain-of-thought
// before emitting the JSON, so the biggest tasks need headroom; sized from
// live runs (2026-10-13, see AI_INTEGRATION.md).
constexpr int kTitleMaxTokens = 2000;
constexpr int kChaptersMaxTokens = 4000;
constexpr int kTranslateBatchMaxTokens = 6000;
constexpr int kCutsMaxTokens = 4000;
constexpr int kMinChapters = 3;
constexpr int kMaxChapters = 10;
constexpr int kMaxCuts = 20;
constexpr int kTitleMaxChars = 60;
constexpr int kDescriptionMaxChars = 300;
constexpr int kChapterTitleMaxWords = 6;

// Consecutive [first,last) ranges covering `count` entries.
QVector<QPair<int, int>> batchRanges(int count, int batchSize = kTranslateBatchSize);

// Clock parsing. fps == 0 accepts only "HH:MM:SS"; fps > 0 also accepts
// "HH:MM:SS:FF", "HH:MM:SS,FF" and "HH:MM:SSFF" (frames via fps).
bool parseClock(const QString &s, double fps, double *sec);

// Translation response: a JSON array of exactly `expectedCount` non-empty
// strings, in order. Code fences / surrounding prose tolerated.
bool parseTranslation(const QString &raw, int expectedCount,
                      QStringList *out, QString *err);

// Chapters response: 3..10 {start,title} in non-decreasing order, starts
// within [0, durationSec], titles <= 6 words.
bool parseChapters(const QString &raw, double durationSec,
                   QVector<ChapterSuggestion> *out, QString *err);

// Title response: {title,description} with the length limits above.
bool parseTitle(const QString &raw, TitleResult *out, QString *err);

// Cuts response: <= kMaxCuts {start,end,reason,confidence}; 0 <= start <
// end <= durationSec; reason in {silence,filler}; confidence in [0,1].
bool parseCuts(const QString &raw, double durationSec, double fps,
               QVector<CutSuggestion> *out, QString *err);

// Prompt builders: fill the templates from prompts.h (placeholders only —
// never concatenate translated fragments here; these are machine prompts).
QString translatePrompt(const QVector<QJsonObject> &entries, const QString &targetLang);
QString chaptersPrompt(const QString &transcriptLines);
QString titlePrompt(const QString &context);
QString cutsPrompt(const QString &transcriptLines, const QString &silenceMap,
                   double fps = 30.0);

} // namespace aifeatures
} // namespace arena
