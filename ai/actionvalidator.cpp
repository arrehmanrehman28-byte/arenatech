#include "actionvalidator.h"
#include <QJsonDocument>

namespace arena {
namespace actionValidator {

QStringList supportedOps()
{
    return {QStringLiteral("add_transition"), QStringLiteral("set_clip_speed"),
            QStringLiteral("add_marker"), QStringLiteral("export")};
}

QStringList transitions()
{
    return {QStringLiteral("cross_dissolve"), QStringLiteral("wipe"),
            QStringLiteral("push"), QStringLiteral("fade_to_black")};
}

QStringList exportPresets()
{
    return {QStringLiteral("youtube_1080p"), QStringLiteral("instagram_reels"),
            QStringLiteral("web_720p"), QStringLiteral("prores_master"),
            QStringLiteral("audio_podcast"), QStringLiteral("webm_web")};
}

namespace {

bool needTarget(const QJsonObject &target, const QStringList &types,
                const QString &op, QStringList &errors)
{
    const QString t = target.value(QStringLiteral("type")).toString();
    if (!types.contains(t)) {
        errors << QStringLiteral("%1: target.type must be one of %2").arg(op, types.join(QStringLiteral(", ")));
        return false;
    }
    if (target.value(QStringLiteral("index")).isDouble()) return true;
    errors << QStringLiteral("%1: target.index (integer) is required").arg(op);
    return false;
}

void validateOne(const QJsonObject &a, QStringList &errors)
{
    const QString op = a.value(QStringLiteral("op")).toString();
    if (op.isEmpty()) {
        errors << QStringLiteral("action missing 'op'");
        return;
    }
    if (!supportedOps().contains(op)) {
        errors << QStringLiteral("unknown op '%1' (supported: %2)").arg(op, supportedOps().join(QStringLiteral(", ")));
        return;
    }
    if (op == QLatin1String("add_transition")) {
        const QJsonObject t = a.value(QStringLiteral("target")).toObject();
        if (!needTarget(t, {QStringLiteral("clip")}, op, errors)) return;
        const QString tr = a.value(QStringLiteral("transition")).toString();
        if (!transitions().contains(tr))
            errors << QStringLiteral("add_transition: transition must be one of %2").arg(transitions().join(QStringLiteral(", ")));
        if (a.contains(QStringLiteral("duration_ms"))) {
            const double d = a.value(QStringLiteral("duration_ms")).toDouble(-1);
            if (d < 10 || d > 10000)
                errors << QStringLiteral("add_transition: duration_ms must be 10..10000");
        }
    } else if (op == QLatin1String("set_clip_speed")) {
        const QJsonObject t = a.value(QStringLiteral("target")).toObject();
        if (!needTarget(t, {QStringLiteral("clip")}, op, errors)) return;
        const double s = a.value(QStringLiteral("speed")).toDouble(-1);
        if (s < 0.1 || s > 16)
            errors << QStringLiteral("set_clip_speed: speed must be 0.1..16");
    } else if (op == QLatin1String("add_marker")) {
        if (!a.value(QStringLiteral("time")).isDouble() || a.value(QStringLiteral("time")).toDouble(-1) < 0)
            errors << QStringLiteral("add_marker: time (non-negative integer frames/seconds) is required");
        const QString label = a.value(QStringLiteral("label")).toString();
        if (label.size() > 120)
            errors << QStringLiteral("add_marker: label too long (max 120)");
    } else if (op == QLatin1String("export")) {
        const QString preset = a.value(QStringLiteral("preset")).toString();
        if (!exportPresets().contains(preset))
            errors << QStringLiteral("export: preset must be one of %2").arg(exportPresets().join(QStringLiteral(", ")));
        if (a.contains(QStringLiteral("subtitles"))) {
            const QString sub = a.value(QStringLiteral("subtitles")).toString();
            if (!QStringList{QStringLiteral("burn"), QStringLiteral("sidecar"), QStringLiteral("none")}.contains(sub))
                errors << QStringLiteral("export: subtitles must be burn|sidecar|none");
        }
    }
}

} // namespace

ValidationResult validateActions(const QJsonArray &actions)
{
    ValidationResult r;
    if (actions.isEmpty()) {
        r.errors << QStringLiteral("no actions found");
        return r;
    }
    for (int i = 0; i < actions.size(); ++i) {
        const QJsonValue v = actions.at(i);
        if (!v.isObject()) {
            r.errors << QStringLiteral("action %1 is not an object").arg(i);
            continue;
        }
        const int before = r.errors.size();
        validateOne(v.toObject(), r.errors);
        // Prefix errors with the action index for readability.
        for (int k = before; k < r.errors.size(); ++k)
            r.errors[k] = QStringLiteral("action %1: %2").arg(QString::number(i), r.errors[k]);
    }
    r.ok = r.errors.isEmpty();
    return r;
}

QJsonArray extractJsonArray(const QString &rawModelText)
{
    const int start = rawModelText.indexOf(QLatin1Char('['));
    if (start < 0) return {};
    int depth = 0;
    bool inString = false;
    for (int i = start; i < rawModelText.size(); ++i) {
        const QChar c = rawModelText.at(i);
        if (inString) {
            if (c == QLatin1Char('\\')) ++i;
            else if (c == QLatin1Char('"')) inString = false;
        } else if (c == QLatin1Char('"')) {
            inString = true;
        } else if (c == QLatin1Char('[')) {
            ++depth;
        } else if (c == QLatin1Char(']')) {
            if (--depth == 0) {
                QJsonParseError pe{};
                QJsonDocument doc = QJsonDocument::fromJson(rawModelText.mid(start, i - start + 1).toUtf8(), &pe);
                if (pe.error == QJsonParseError::NoError && doc.isArray())
                    return doc.array();
                return {};
            }
        }
    }
    return {};
}

} // namespace actionValidator
} // namespace arena
