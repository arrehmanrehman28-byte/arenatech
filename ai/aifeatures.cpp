// M6: AI feature response parsing + validation (pure, offline).
#include "aifeatures.h"
#include "prompts.h"
#include "actionvalidator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>

namespace arena {
namespace aifeatures {

namespace {

int wordCount(const QString &s)
{
    int n = 0;
    bool in = false;
    for (const QChar &c : s) {
        if (c.isSpace()) {
            in = false;
            continue;
        }
        if (!in) {
            ++n;
            in = true;
        }
    }
    return n;
}

// First balanced {...} object in raw text (string/escape aware).
bool extractJsonObject(const QString &raw, QJsonObject *out)
{
    const int start = raw.indexOf(QLatin1Char('{'));
    if (start < 0) return false;
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    for (int i = start; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (inStr) {
            if (esc) esc = false;
            else if (c == QLatin1Char('\\')) esc = true;
            else if (c == QLatin1Char('"')) inStr = false;
            continue;
        }
        if (c == QLatin1Char('"')) inStr = true;
        else if (c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char('}')) {
            --depth;
            if (depth == 0) {
                const QString obj = raw.mid(start, i - start + 1);
                QJsonParseError perr;
                const QJsonDocument doc = QJsonDocument::fromJson(obj.toUtf8(), &perr);
                if (perr.error != QJsonParseError::NoError || !doc.isObject())
                    return false;
                *out = doc.object();
                return true;
            }
        }
    }
    return false;
}

bool isFinite(double v) { return std::isfinite(v); }

} // namespace

QVector<QPair<int, int>> batchRanges(int count, int batchSize)
{
    QVector<QPair<int, int>> ranges;
    if (count <= 0 || batchSize <= 0) return ranges;
    for (int first = 0; first < count; first += batchSize) {
        const int last = qMin(count, first + batchSize);
        ranges.append(qMakePair(first, last));
    }
    return ranges;
}

bool parseClock(const QString &s, double fps, double *sec)
{
    const QString t = s.trimmed();
    // Comma frame variant: "HH:MM:SS,FF" -> "HH:MM:SS:FF"
    QString norm = t;
    if (norm.contains(QLatin1Char(',')))
        norm.replace(QLatin1Char(','), QLatin1Char(':'));
    const QStringList p = norm.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (p.size() < 3 || p.size() > 4) return false;
    bool ok = false;
    const int h = p[0].toInt(&ok); if (!ok) return false;
    const int m = p[1].toInt(&ok); if (!ok) return false;
    // Seconds field; a 4-digit field with fps>0 is "SSFF".
    QString sf = p[2];
    int ff = 0;
    if (sf.size() == 4) {
        if (fps <= 0) return false;
        if (!sf.mid(2).toInt(&ok) && !sf.mid(2).isEmpty()) return false;
        ff = sf.mid(2).toInt(&ok);
        sf = sf.left(2);
    } else if (sf.size() != 2) {
        return false;
    }
    const int ss = sf.toInt(&ok); if (!ok) return false;
    if (m > 59 || ss > 59 || h < 0 || m < 0 || ss < 0) return false;
    if (p.size() == 4) {
        if (fps <= 0) return false;
        ff = p[3].toInt(&ok); if (!ok) return false;
    }
    if (ff < 0 || (fps > 0 && ff >= int(fps + 0.5))) return false;
    double v = double(h) * 3600.0 + double(m) * 60.0 + double(ss);
    if (fps > 0 && ff > 0) v += double(ff) / fps;
    if (!isFinite(v) || v < 0) return false;
    if (sec) *sec = v;
    return true;
}

bool parseTranslation(const QString &raw, int expectedCount, QStringList *out,
                      QString *err)
{
    auto fail = [&](const QString &e) { if (err) *err = e; return false; };
    out->clear();
    if (expectedCount <= 0) return fail(QStringLiteral("invalid expected count"));
    const QJsonArray arr = actionValidator::extractJsonArray(raw);
    if (arr.size() != expectedCount)
        return fail(QStringLiteral("expected %1 entries, got %2")
                        .arg(expectedCount)
                        .arg(arr.size()));
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonValue v = arr.at(i);
        if (!v.isString())
            return fail(QStringLiteral("entry %1 is not a string").arg(i));
        const QString s = v.toString();
        if (s.trimmed().isEmpty())
            return fail(QStringLiteral("entry %1 is empty").arg(i));
        out->append(s);
    }
    return true;
}

bool parseChapters(const QString &raw, double durationSec,
                   QVector<ChapterSuggestion> *out, QString *err)
{
    auto fail = [&](const QString &e) { if (err) *err = e; return false; };
    out->clear();
    const QJsonArray arr = actionValidator::extractJsonArray(raw);
    if (arr.size() < kMinChapters || arr.size() > kMaxChapters)
        return fail(QStringLiteral("chapter count %1 outside %2..%3")
                        .arg(arr.size())
                        .arg(kMinChapters)
                        .arg(kMaxChapters));
    double prev = -1.0;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        if (!o.contains(QStringLiteral("start")) || !o.contains(QStringLiteral("title")))
            return fail(QStringLiteral("chapter %1 missing start/title").arg(i));
        double start = 0.0;
        if (!parseClock(o.value(QStringLiteral("start")).toString(), 0.0, &start))
            return fail(QStringLiteral("chapter %1 bad start timecode").arg(i));
        if (start > durationSec + 0.001)
            return fail(QStringLiteral("chapter %1 starts beyond video end").arg(i));
        if (start < prev - 0.001)
            return fail(QStringLiteral("chapters not in increasing order"));
        const QString title = o.value(QStringLiteral("title")).toString().trimmed();
        if (title.isEmpty())
            return fail(QStringLiteral("chapter %1 has empty title").arg(i));
        if (wordCount(title) > kChapterTitleMaxWords)
            return fail(QStringLiteral("chapter %1 title exceeds %2 words")
                            .arg(i)
                            .arg(kChapterTitleMaxWords));
        out->append({start, title});
        prev = start;
    }
    return true;
}

bool parseTitle(const QString &raw, TitleResult *out, QString *err)
{
    auto fail = [&](const QString &e) { if (err) *err = e; return false; };
    QJsonObject o;
    if (!extractJsonObject(raw, &o))
        return fail(QStringLiteral("no JSON object found"));
    if (!o.contains(QStringLiteral("title")) || !o.contains(QStringLiteral("description")))
        return fail(QStringLiteral("missing title/description field"));
    const QString title = o.value(QStringLiteral("title")).toString().trimmed();
    const QString desc = o.value(QStringLiteral("description")).toString().trimmed();
    if (title.isEmpty() || desc.isEmpty())
        return fail(QStringLiteral("title and description must be non-empty"));
    if (title.size() > kTitleMaxChars)
        return fail(QStringLiteral("title exceeds %1 characters").arg(kTitleMaxChars));
    if (desc.size() > kDescriptionMaxChars)
        return fail(QStringLiteral("description exceeds %1 characters").arg(kDescriptionMaxChars));
    out->title = title;
    out->description = desc;
    return true;
}

bool parseCuts(const QString &raw, double durationSec, double fps,
               QVector<CutSuggestion> *out, QString *err)
{
    auto fail = [&](const QString &e) { if (err) *err = e; return false; };
    out->clear();
    const QJsonArray arr = actionValidator::extractJsonArray(raw);
    if (arr.isEmpty()) return fail(QStringLiteral("no cuts returned"));
    if (arr.size() > kMaxCuts)
        return fail(QStringLiteral("more than %1 cuts suggested").arg(kMaxCuts));
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        const QString startS = o.value(QStringLiteral("start")).toString();
        const QString endS = o.value(QStringLiteral("end")).toString();
        double s = 0.0, e = 0.0;
        if (!parseClock(startS, fps, &s))
            return fail(QStringLiteral("cut %1 bad start timecode").arg(i));
        if (!parseClock(endS, fps, &e))
            return fail(QStringLiteral("cut %1 bad end timecode").arg(i));
        if (e <= s)
            return fail(QStringLiteral("cut %1 end is not after start").arg(i));
        if (fps > 0.0) {
            // Frame-exact: the end timecode must point at a frame that exists.
            // A cut ending on the first frame past the final frame is invalid.
            const long long totalFrames = static_cast<long long>(qRound(durationSec * fps));
            const long long endFrame = static_cast<long long>(qRound(e * fps));
            if (totalFrames > 0 && endFrame >= totalFrames)
                return fail(QStringLiteral("cut %1 ends beyond video end").arg(i));
        } else if (e > durationSec + 0.001) {
            return fail(QStringLiteral("cut %1 ends beyond video end").arg(i));
        }
        const QString reason = o.value(QStringLiteral("reason")).toString().trimmed();
        if (reason != QStringLiteral("silence") && reason != QStringLiteral("filler"))
            return fail(QStringLiteral("cut %1 has unknown reason '%2'").arg(i).arg(reason));
        const double conf = o.value(QStringLiteral("confidence")).toDouble();
        if (!isFinite(conf) || conf < 0.0 || conf > 1.0)
            return fail(QStringLiteral("cut %1 confidence out of range").arg(i));
        out->append({s, e, reason, conf});
    }
    return true;
}

QString translatePrompt(const QVector<QJsonObject> &entries, const QString &targetLang)
{
    QJsonArray arr;
    for (const QJsonObject &e : entries) arr.append(e);
    QString p = prompts::translateSubtitles();
    p.replace(QStringLiteral("{targetLang}"), targetLang);
    p.replace(QStringLiteral("{entries}"),
              QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    return p;
}

QString chaptersPrompt(const QString &transcriptLines)
{
    QString p = prompts::generateChapters();
    p.replace(QStringLiteral("{transcript}"), transcriptLines);
    return p;
}

QString titlePrompt(const QString &context)
{
    QString p = prompts::smartTitleAndDescription();
    p.replace(QStringLiteral("{context}"), context);
    return p;
}

QString cutsPrompt(const QString &transcriptLines, const QString &silenceMap)
{
    QString p = prompts::suggestCuts();
    p.replace(QStringLiteral("{transcript}"), transcriptLines);
    p.replace(QStringLiteral("{silences}"), silenceMap);
    return p;
}

} // namespace aifeatures
} // namespace arena
