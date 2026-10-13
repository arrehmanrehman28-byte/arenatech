#pragma once
// M5 subtitles: SRT / VTT / ASS parsing and canonical encoding
// (format-native seconds), time-code helpers, reading-speed checks, and
// RTL direction helpers for burn-in. The project layer (SubtitleTrack)
// stores cues in timeline frames and converts at the boundary via
// secToFrame / frameToSec.
//
// Parsers are lenient where the formats are (BOM, CRLF/LF, blank lines,
// VTT NOTE/STYLE regions, ASS override tags) and strict where they must
// be (time codes, cue order). Encoders emit the canonical form the
// byte-stable round-trip tests (tests/fixtures/subs) are built against.
#include <QVector>
#include <QString>

namespace arena {

// One caption line/paragraph. startSec/endSec in seconds (format-native).
struct SubtitleCue {
    double startSec = 0.0;
    double endSec = 0.0;
    QString text; // may contain \n for multi-line cues

    bool isValid() const { return endSec > startSec && !text.trimmed().isEmpty(); }
};

class Subtitles {
public:
    // --- parsing -------------------------------------------------------------
    // Failures set *err (human readable) and return false. SRT keeps the
    // parseable blocks and reports skipped ones; VTT/ASS fail on any
    // malformed cue.
    static bool parseSrt(const QString &content, QVector<SubtitleCue> *out, QString *err);
    static bool parseVtt(const QString &content, QVector<SubtitleCue> *out, QString *err);
    static bool parseAss(const QString &content, QVector<SubtitleCue> *out, QString *err);
    // Auto-detect: WEBVTT header -> VTT, [Events]/[Script Info] -> ASS,
    // otherwise SRT. `fileName` breaks ties (extension fallback).
    static bool parse(const QString &content, const QString &fileName,
                      QVector<SubtitleCue> *out, QString *err);

    // --- canonical encoding (byte-stable for the sample set) ------------------
    // SRT: CRLF; VTT: CRLF (spec-mandated); ASS: CRLF with a minimal
    // [Script Info] / [V4+ Styles] / [Events] scaffold.
    static QString encodeSrt(const QVector<SubtitleCue> &cues);
    static QString encodeVtt(const QVector<SubtitleCue> &cues);
    static QString encodeAss(const QVector<SubtitleCue> &cues);
    // Dispatch on the fileName extension (.srt/.vtt/.ass).
    static QString encode(const QVector<SubtitleCue> &cues, const QString &fileName);

    // --- time codes -------------------------------------------------------------
    // "HH:MM:SS,mmm" (SRT) / "HH:MM:SS.mmm" (VTT, "MM:SS.mmm" accepted) /
    // "H:MM:SS.cc" (ASS, centiseconds).
    static bool parseSrtTime(const QString &s, double *sec);
    static bool parseVttTime(const QString &s, double *sec);
    static bool parseAssTime(const QString &s, double *sec);
    static QString formatSrtTime(double sec);
    static QString formatVttTime(double sec);
    static QString formatAssTime(double sec);

    // Frame <-> second at the project fps (nearest frame).
    static qint64 secToFrame(double sec, double fps);
    static double frameToSec(qint64 frame, double fps);

    // --- reading speed (spec: > 42 chars or > 17 cps) ---------------------------
    struct CueWarning {
        int visibleChars = 0; // non-space characters
        double cps = 0.0;     // visible chars per second over the cue span
        bool tooLong = false; // > 42 visible characters
        bool tooFast = false; // > 17 cps
        bool any() const { return tooLong || tooFast; }
    };
    static CueWarning checkCue(const QString &text, double startSec, double endSec);

    // --- direction (burn-in) ----------------------------------------------------
    // True for RTL base languages (ar/he/fa/ur/…) or an explicit -Arab/-Hebr
    // script subtag.
    static bool isRtlLanguage(const QString &bcp47);
    // First strong character decides (Arabic/Hebrew blocks incl. Urdu
    // extensions); LTR when the text has no strong RTL character.
    static bool textIsRtl(const QString &text);
};

} // namespace arena
