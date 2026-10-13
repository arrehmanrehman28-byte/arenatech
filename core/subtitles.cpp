#include "subtitles.h"

#include <QChar>

#include <algorithm>
#include <cmath>

namespace arena {
namespace {

constexpr int kMaxLineChars = 42;  // spec: reading-speed warning threshold
constexpr double kMaxCps = 17.0;   // spec: characters per second

// Strip a UTF-8 BOM if present.
QString stripBom(const QString &s)
{
    if (!s.isEmpty() && s.at(0) == QChar(0xFEFF))
        return s.mid(1);
    return s;
}

// Split on blank lines, keeping line endings normalized to \n.
QVector<QString> blocks(const QString &content)
{
    QString tmp = content;
    const QString norm = tmp.replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
                            .replace(QStringLiteral("\r"), QStringLiteral("\n"));
    QVector<QString> out;
    QString cur;
    for (const QString &line : norm.split('\n', Qt::KeepEmptyParts)) {
        if (line.trimmed().isEmpty()) {
            if (!cur.trimmed().isEmpty())
                out.append(cur);
            cur.clear();
        } else {
            cur += line + '\n';
        }
    }
    if (!cur.trimmed().isEmpty())
        out.append(cur);
    return out;
}

QVector<QString> linesOf(const QString &block)
{
    return block.split('\n', Qt::SkipEmptyParts);
}

bool parseTimeCore(const QString &s, double *sec, int fractionDigits,
                   QChar fractionSep, int hoursMinDigits)
{
    const QString t = s.trimmed();
    // Split fraction.
    QString hmsPart = t;
    double frac = 0.0;
    const int fi = t.indexOf(fractionSep);
    if (fi >= 0) {
        hmsPart = t.left(fi);
        const QString fs = t.mid(fi + 1).trimmed();
        if (fs.isEmpty() || fs.size() > fractionDigits + 2) // truncate long fractions
            return false;
        bool ok = false;
        frac = fs.left(fractionDigits).toDouble(&ok) / std::pow(10.0, fractionDigits);
        if (!ok)
            return false;
    }
    const QList<QString> parts = hmsPart.split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 3)
        return false;
    // Digits only.
    for (const QString &p : parts) {
        if (p.isEmpty())
            return false;
        for (const QChar &c : p)
            if (!c.isDigit())
                return false;
    }
    bool ok = true;
    double hh = 0, mm = 0, ss = 0;
    if (parts.size() == 3) {
        if (parts[0].size() < hoursMinDigits)
            return false;
        hh = parts[0].toDouble(&ok);
        mm = parts[1].toDouble(&ok);
        ss = parts[2].toDouble(&ok);
    } else {
        mm = parts[0].toDouble(&ok);
        ss = parts[1].toDouble(&ok);
    }
    if (!ok || mm < 0 || mm >= 60 || ss < 0 || ss >= 60)
        return false;
    *sec = hh * 3600.0 + mm * 60.0 + ss + frac;
    return true;
}

QString formatTimeCore(double sec, int fractionDigits, QChar fractionSep, int minHours)
{
    if (sec < 0)
        sec = 0;
    const double scale = std::pow(10.0, fractionDigits);
    // Round to the fraction step, then split whole/fraction.
    const double rounded = std::llround(sec * scale) / scale;
    const qint64 whole = qint64(rounded);
    const qint64 frac =
        qint64(std::llround((rounded - double(whole)) * scale));
    const qint64 h = whole / 3600;
    const qint64 m = (whole % 3600) / 60;
    const qint64 s = whole % 60;
    QString hs = QString::number(h);
    if (hs.size() < minHours)
        hs.prepend(QString(minHours - hs.size(), QLatin1Char('0')));
    const QString ms = QString::number(m).rightJustified(2, QLatin1Char('0'));
    const QString ss = QString::number(s).rightJustified(2, QLatin1Char('0'));
    const QString fs = QString::number(frac).rightJustified(fractionDigits, QLatin1Char('0'));
    return hs + QLatin1Char(':') + ms + QLatin1Char(':') + ss + fractionSep + fs;
}

bool isRtlBase(const QString &lang)
{
    static const QStringList kRtl = {
        QStringLiteral("ar"),  QStringLiteral("he"), QStringLiteral("fa"),
        QStringLiteral("ur"),  QStringLiteral("sd"), QStringLiteral("ps"),
        QStringLiteral("dv"),  QStringLiteral("ku"), QStringLiteral("ug"),
        QStringLiteral("yi"),  QStringLiteral("iw"), QStringLiteral("arc"),
    };
    const QString l = lang.toLower().section('-', 0, 0);
    return kRtl.contains(l);
}

bool charIsRtlStrong(QChar c)
{
    const uint cp = c.unicode();
    // Arabic block + Supplement + Extended-A, Hebrew block, Syriac, Thaana,
    // NKo, Samaritan, Mandean, Arabic presentation forms (covers Urdu's
    // extra letters in the main Arabic block).
    return (cp >= 0x0590 && cp <= 0x05FF) // Hebrew
        || (cp >= 0x0600 && cp <= 0x06FF) // Arabic
        || (cp >= 0x0700 && cp <= 0x074F) // Syriac
        || (cp >= 0x0750 && cp <= 0x077F) // Arabic Supplement
        || (cp >= 0x08A0 && cp <= 0x08FF) // Arabic Extended-A
        || (cp >= 0xFB1D && cp <= 0xFDFF) // Arabic presentation forms
        || (cp >= 0xFE70 && cp <= 0xFEFF); // Arabic presentation forms-B
}

} // namespace

// --- time codes ------------------------------------------------------------------

bool Subtitles::parseSrtTime(const QString &s, double *sec)
{
    return parseTimeCore(s, sec, 3, QLatin1Char(','), 2);
}
bool Subtitles::parseVttTime(const QString &s, double *sec)
{
    return parseTimeCore(s, sec, 3, QLatin1Char('.'), 2);
}
bool Subtitles::parseAssTime(const QString &s, double *sec)
{
    return parseTimeCore(s, sec, 2, QLatin1Char('.'), 1);
}
QString Subtitles::formatSrtTime(double sec)
{
    return formatTimeCore(sec, 3, QLatin1Char(','), 2);
}
QString Subtitles::formatVttTime(double sec)
{
    return formatTimeCore(sec, 3, QLatin1Char('.'), 2);
}
QString Subtitles::formatAssTime(double sec)
{
    return formatTimeCore(sec, 2, QLatin1Char('.'), 1);
}

qint64 Subtitles::secToFrame(double sec, double fps)
{
    if (fps <= 0) return 0;
    return qint64(std::llround(sec * fps));
}
double Subtitles::frameToSec(qint64 frame, double fps)
{
    if (fps <= 0) return 0.0;
    return double(frame) / fps;
}

// --- SRT ---------------------------------------------------------------------------

bool Subtitles::parseSrt(const QString &content, QVector<SubtitleCue> *out, QString *err)
{
    out->clear();
    const QString body = stripBom(content);
    const QVector<QString> blks = blocks(body);
    int skipped = 0;
    for (const QString &blk : blks) {
        const QVector<QString> lines = linesOf(blk);
        int i = 0;
        // Optional integer index line.
        const QString firstLine = lines[i].trimmed();
        if (i < lines.size() && !firstLine.isEmpty() && firstLine.at(0).isDigit()
            && std::all_of(firstLine.constBegin(), firstLine.constEnd(), [](const QChar &c) { return c.isDigit(); }))
            ++i;
        if (i >= lines.size() || !lines[i].contains(QLatin1String("-->"))) {
            ++skipped;
            continue;
        }
        const QStringList ts = lines[i].split(QLatin1String(" --> "), Qt::SkipEmptyParts);
        if (ts.size() < 2) {
            ++skipped;
            continue;
        }
        double a = 0, b = 0;
        if (!parseSrtTime(ts[0], &a) || !parseSrtTime(ts[1], &b) || b <= a) {
            ++skipped;
            continue;
        }
        SubtitleCue c;
        c.startSec = a;
        c.endSec = b;
        for (int j = i + 1; j < lines.size(); ++j)
            c.text += (j > i + 1 ? QStringLiteral("\n") : QString()) + lines[j];
        if (!c.text.trimmed().isEmpty())
            out->append(c);
        else
            ++skipped;
    }
    if (out->isEmpty() && !skipped) {
        *err = QStringLiteral("No cues found");
        return false;
    }
    if (skipped && err)
        err->append(QStringLiteral("(%1 block(s) skipped)").arg(skipped));
    return true;
}

QString Subtitles::encodeSrt(const QVector<SubtitleCue> &cues)
{
    QString out;
    for (int i = 0; i < cues.size(); ++i) {
        const SubtitleCue &c = cues[i];
        // Record lines end in CRLF; stored line breaks (\n) inside the text
        // are emitted as bare LF, matching the canonical sample fixtures.
        out += QString::number(i + 1) + QStringLiteral("\r\n");
        out += formatSrtTime(c.startSec) + QLatin1String(" --> ") + formatSrtTime(c.endSec)
             + QStringLiteral("\r\n");
        out += c.text + QStringLiteral("\r\n");
        out += QStringLiteral("\r\n");
    }
    return out;
}

// --- VTT ---------------------------------------------------------------------------

bool Subtitles::parseVtt(const QString &content, QVector<SubtitleCue> *out, QString *err)
{
    out->clear();
    QString body = stripBom(content);
    const QVector<QString> allLines =
        body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
            .split('\n', Qt::KeepEmptyParts);
    int i = 0;
    // First line must be WEBVTT.
    if (i >= allLines.size() || !allLines[i].trimmed().startsWith(QLatin1String("WEBVTT"))) {
        *err = QStringLiteral("Missing WEBVTT header");
        return false;
    }
    ++i;
    // Header metadata until first blank line.
    while (i < allLines.size() && !allLines[i].trimmed().isEmpty())
        ++i;
    const int n = allLines.size();
    while (i < n) {
        while (i < n && allLines[i].trimmed().isEmpty())
            ++i;
        if (i >= n)
            break;
        // Skip NOTE / STYLE / REGION blocks.
        const QString first = allLines[i].trimmed();
        if (first.startsWith(QLatin1String("NOTE")) || first.startsWith(QLatin1String("STYLE"))
            || first.startsWith(QLatin1String("REGION"))) {
            ++i;
            while (i < n && !allLines[i].trimmed().isEmpty())
                ++i;
            continue;
        }
        // Optional cue identifier line (a line without --> before the timing).
        if (!first.contains(QLatin1String("-->"))) {
            ++i;
            if (i >= n)
                break;
        }
        const QString timing = allLines[i].trimmed();
        if (!timing.contains(QLatin1String("-->"))) {
            *err = QStringLiteral("Malformed cue (no timing line)");
            return false;
        }
        const QStringList ts = timing.split(QLatin1String(" --> "), Qt::SkipEmptyParts);
        double a = 0, b = 0;
        if (ts.size() < 2 || !parseVttTime(ts[0], &a)
            || !parseVttTime(ts[1].section(' ', 0, 0), &b) || b <= a) {
            *err = QStringLiteral("Malformed cue timing");
            return false;
        }
        SubtitleCue c;
        c.startSec = a;
        c.endSec = b;
        int j = i + 1;
        bool firstLine = true;
        while (j < n && !allLines[j].trimmed().isEmpty()) {
            c.text += (firstLine ? QString() : QStringLiteral("\n")) + allLines[j].trimmed();
            firstLine = false;
            ++j;
        }
        if (c.text.trimmed().isEmpty()) {
            *err = QStringLiteral("Cue with empty text");
            return false;
        }
        out->append(c);
        i = j;
    }
    if (out->isEmpty()) {
        *err = QStringLiteral("No cues found");
        return false;
    }
    return true;
}

QString Subtitles::encodeVtt(const QVector<SubtitleCue> &cues)
{
    QString out = QStringLiteral("WEBVTT\r\n\r\n");
    for (const SubtitleCue &c : cues) {
        // Cue header lines end in CRLF; stored line breaks (\n) inside the
        // text are emitted as bare LF, matching the canonical sample fixtures.
        out += formatVttTime(c.startSec) + QLatin1String(" --> ") + formatVttTime(c.endSec)
             + QStringLiteral("\r\n");
        out += c.text + QStringLiteral("\r\n");
        out += QStringLiteral("\r\n");
    }
    return out;
}

// --- ASS ---------------------------------------------------------------------------

bool Subtitles::parseAss(const QString &content, QVector<SubtitleCue> *out, QString *err)
{
    out->clear();
    QString body = stripBom(content);
    const QVector<QString> allLines =
        body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
            .split('\n', Qt::KeepEmptyParts);
    // Field order from the last Format: line inside [Events].
    QVector<QString> order = {
        QStringLiteral("Layer"),     QStringLiteral("Start"),
        QStringLiteral("End"),       QStringLiteral("Style"),
        QStringLiteral("Name"),      QStringLiteral("MarginL"),
        QStringLiteral("MarginR"),   QStringLiteral("MarginV"),
        QStringLiteral("Effect"),    QStringLiteral("Text")};
    int textIdx = 9;
    int startIdx = 1;
    int endIdx = 2;
    bool inEvents = false;
    bool seenFormat = false;
    bool seenDialogue = false;
    for (const QString &raw : allLines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            inEvents = line.compare(QStringLiteral("[Events]"), Qt::CaseInsensitive) == 0;
            if (inEvents)
                seenFormat = false;
            continue;
        }
        if (!inEvents)
            continue;
        if (line.startsWith(QLatin1String("Format:"))) {
            const QStringList fields = line.mid(7).split(',', Qt::SkipEmptyParts);
            order.clear();
            for (const QString &f : fields)
                order.append(f.trimmed());
            for (int k = 0; k < order.size(); ++k) {
                if (order[k].compare(QStringLiteral("Text"), Qt::CaseInsensitive) == 0)
                    textIdx = k;
                else if (order[k].compare(QStringLiteral("Start"), Qt::CaseInsensitive) == 0)
                    startIdx = k;
                else if (order[k].compare(QStringLiteral("End"), Qt::CaseInsensitive) == 0)
                    endIdx = k;
            }
            seenFormat = true;
            continue;
        }
        if (line.startsWith(QLatin1String("Dialogue:"))) {
            seenDialogue = true;
            const QStringList fields = line.mid(9).split(',', Qt::KeepEmptyParts);
            if (fields.size() <= qMax(textIdx, qMax(startIdx, endIdx))) {
                *err = QStringLiteral("Malformed Dialogue line");
                return false;
            }
            double a = 0, b = 0;
            if (!parseAssTime(fields[startIdx], &a) || !parseAssTime(fields[endIdx], &b)
                || b <= a) {
                *err = QStringLiteral("Malformed Dialogue timing");
                return false;
            }
            // The Text field is last, so rejoin its comma-separated pieces.
            QString text = fields.mid(textIdx).join(QLatin1Char(','));
            QString clean;
            bool inTag = false;
            for (const QChar &ch : text) {
                if (ch == QLatin1Char('{'))
                    inTag = true;
                else if (ch == QLatin1Char('}'))
                    inTag = false;
                else if (!inTag)
                    clean += ch;
            }
            clean.replace(QLatin1String("\\N"), QStringLiteral("\n"));
            if (clean.trimmed().isEmpty())
                continue; // comment/blank dialogue
            SubtitleCue c;
            c.startSec = a;
            c.endSec = b;
            c.text = clean.trimmed();
            out->append(c);
        }
    }
    if (!seenDialogue) {
        *err = QStringLiteral("No Dialogue lines found");
        return false;
    }
    Q_UNUSED(seenFormat);
    return true;
}

QString Subtitles::encodeAss(const QVector<SubtitleCue> &cues)
{
    QString out;
    out += QStringLiteral("[Script Info]\r\n");
    out += QStringLiteral("Title: Arena Edit\r\n");
    out += QStringLiteral("ScriptType: v4.00+\r\n");
    out += QStringLiteral("PlayResX: 1920\r\n");
    out += QStringLiteral("PlayResY: 1080\r\n");
    out += QStringLiteral("\r\n");
    out += QStringLiteral("[V4+ Styles]\r\n");
    out += QStringLiteral(
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
        "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
        "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n");
    out += QStringLiteral(
        "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,"
        "100,0,0,1,2,0,2,10,10,10,1\r\n");
    out += QStringLiteral("\r\n");
    out += QStringLiteral("[Events]\r\n");
    out += QStringLiteral(
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n");
    for (const SubtitleCue &c : cues) {
        // ASS line breaks are \N inside a single Dialogue line.
        QString text = c.text;
        text = text.replace(QStringLiteral("\n"), QLatin1String("\\N"));
        out += QStringLiteral("Dialogue: 0,") + formatAssTime(c.startSec) + ','
             + formatAssTime(c.endSec) + QStringLiteral(",Default,,0,0,0,,") + text
             + QStringLiteral("\r\n");
    }
    return out;
}

// --- dispatch ------------------------------------------------------------------------

bool Subtitles::parse(const QString &content, const QString &fileName,
                      QVector<SubtitleCue> *out, QString *err)
{
    const QString head = stripBom(content).trimmed().left(256);
    bool isVtt = head.startsWith(QLatin1String("WEBVTT"));
    bool isAss = head.startsWith(QLatin1String("["))
                 && (head.contains(QLatin1String("[Events]"))
                     || head.contains(QLatin1String("[Script Info]")));
    if (!isVtt && !isAss) {
        const QString ext = fileName.section(QLatin1Char('.'), 1, 1).toLower();
        if (ext == QLatin1String("vtt")) isVtt = true;
        else if (ext == QLatin1String("ass") || ext == QLatin1String("ssa")) isAss = true;
    }
    if (isVtt)
        return parseVtt(content, out, err);
    if (isAss)
        return parseAss(content, out, err);
    return parseSrt(content, out, err);
}

QString Subtitles::encode(const QVector<SubtitleCue> &cues, const QString &fileName)
{
    const QString ext = fileName.section(QLatin1Char('.'), 1, 1).toLower();
    if (ext == QLatin1String("vtt"))
        return encodeVtt(cues);
    if (ext == QLatin1String("ass") || ext == QLatin1String("ssa"))
        return encodeAss(cues);
    return encodeSrt(cues);
}

// --- reading speed ---------------------------------------------------------------

Subtitles::CueWarning Subtitles::checkCue(const QString &text, double startSec, double endSec)
{
    CueWarning w;
    for (const QChar &c : text)
        if (!c.isSpace())
            ++w.visibleChars;
    const double dur = endSec - startSec;
    if (dur > 0)
        w.cps = double(w.visibleChars) / dur;
    w.tooLong = w.visibleChars > kMaxLineChars;
    w.tooFast = w.cps > kMaxCps;
    return w;
}

// --- direction -----------------------------------------------------------------------

bool Subtitles::isRtlLanguage(const QString &bcp47)
{
    if (bcp47.isEmpty())
        return false;
    if (isRtlBase(bcp47))
        return true;
    const QString sub = bcp47.section('-', 1, 1).toLower();
    return sub == QLatin1String("arab") || sub == QLatin1String("hebr");
}

bool Subtitles::textIsRtl(const QString &text)
{
    for (const QChar &c : text) {
        if (c.isSpace())
            continue;
        // First strong character decides.
        const uint cp = c.unicode();
        const bool rtl = charIsRtlStrong(c);
        const bool ltr = (cp >= 0x0041 && cp <= 0x005A) || (cp >= 0x0061 && cp <= 0x007A)
                         || (cp >= 0x00C0 && cp <= 0x024F) || (cp >= 0x0400 && cp <= 0x04FF);
        if (rtl || ltr)
            return rtl;
    }
    return false;
}

} // namespace arena
