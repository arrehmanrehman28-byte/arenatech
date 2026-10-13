#include "project.h"
#include <QFile>
#include <QUuid>

namespace arena {

QJsonObject Clip::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("source"), sourceRef},
        {QStringLiteral("in"), qint64(in)},
        {QStringLiteral("out"), qint64(out)},
        {QStringLiteral("offset"), qint64(offset)},
        {QStringLiteral("speed"), speed},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("effects"), effects},
    };
}

Clip Clip::fromJson(const QJsonObject &o)
{
    Clip c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.sourceRef = o.value(QStringLiteral("source")).toString();
    c.in = o.value(QStringLiteral("in")).toVariant().toLongLong();
    c.out = o.value(QStringLiteral("out")).toVariant().toLongLong();
    c.offset = o.value(QStringLiteral("offset")).toVariant().toLongLong();
    c.speed = o.value(QStringLiteral("speed")).toDouble(1.0);
    c.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    c.effects = o.value(QStringLiteral("effects")).toObject();
    return c;
}

QJsonObject Marker::toJson() const
{
    return {{QStringLiteral("time"), qint64(time)}, {QStringLiteral("label"), label}};
}
Marker Marker::fromJson(const QJsonObject &o)
{
    Marker m;
    m.time = o.value(QStringLiteral("time")).toVariant().toLongLong();
    m.label = o.value(QStringLiteral("label")).toString();
    return m;
}

QJsonObject TimelineCue::toJson() const
{
    return {{QStringLiteral("startFrame"), startFrame},
            {QStringLiteral("endFrame"), endFrame},
            {QStringLiteral("text"), text}};
}
TimelineCue TimelineCue::fromJson(const QJsonObject &o)
{
    TimelineCue c;
    c.startFrame = o.value(QStringLiteral("startFrame")).toVariant().toLongLong();
    c.endFrame = o.value(QStringLiteral("endFrame")).toVariant().toLongLong();
    c.text = o.value(QStringLiteral("text")).toString();
    return c;
}

QJsonObject SubtitleTrack::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("language"), language);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("burnIn"), burnIn);
    QJsonArray cuesArr;
    for (const TimelineCue &c : cues)
        cuesArr.append(c.toJson());
    o.insert(QStringLiteral("cues"), cuesArr);
    return o;
}
SubtitleTrack SubtitleTrack::fromJson(const QJsonObject &o)
{
    SubtitleTrack t;
    t.id = o.value(QStringLiteral("id")).toString();
    t.language = o.value(QStringLiteral("language")).toString();
    t.name = o.value(QStringLiteral("name")).toString();
    t.burnIn = o.value(QStringLiteral("burnIn")).toBool(false);
    for (const QJsonValue &v : o.value(QStringLiteral("cues")).toArray())
        t.cues.append(TimelineCue::fromJson(v.toObject()));
    return t;
}

QJsonObject Track::toJson() const
{
    QJsonArray clipArray;
    for (const Clip &c : clips) clipArray.append(c.toJson());
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), Project::trackTypeName(type)},
        {QStringLiteral("name"), name},
        {QStringLiteral("collapsed"), collapsed},
        {QStringLiteral("clips"), clipArray},
    };
}

Track Track::fromJson(const QJsonObject &o)
{
    Track t;
    t.id = o.value(QStringLiteral("id")).toString();
    t.type = Project::trackTypeFromName(o.value(QStringLiteral("type")).toString());
    t.name = o.value(QStringLiteral("name")).toString();
    t.collapsed = o.value(QStringLiteral("collapsed")).toBool(false);
    for (const QJsonValue &v : o.value(QStringLiteral("clips")).toArray())
        t.clips.append(Clip::fromJson(v.toObject()));
    return t;
}

QJsonObject ProxySettings::toJson() const
{
    return {{QStringLiteral("enabled"), enabled}, {QStringLiteral("height"), height}};
}
ProxySettings ProxySettings::fromJson(const QJsonObject &o)
{
    ProxySettings p;
    p.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    p.height = o.value(QStringLiteral("height")).toInt(540);
    return p;
}

QJsonObject Project::toJson() const
{
    QJsonArray tr, mk, st;
    for (const Track &t : tracks) tr.append(t.toJson());
    for (const Marker &m : markers) mk.append(m.toJson());
    for (const SubtitleTrack &s : subtitleTracks) st.append(s.toJson());
    return {
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("fps"), fps},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("aspect"), aspect},
        {QStringLiteral("sampleRate"), sampleRate},
        {QStringLiteral("proxy"), proxy.toJson()},
        {QStringLiteral("tracks"), tr},
        {QStringLiteral("markers"), mk},
        {QStringLiteral("subtitleTracks"), st},
    };
}

Project Project::fromJson(const QJsonObject &o)
{
    Project p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.name = o.value(QStringLiteral("name")).toString(QStringLiteral("Untitled"));
    p.fps = o.value(QStringLiteral("fps")).toDouble(30.0);
    p.width = o.value(QStringLiteral("width")).toInt(1920);
    p.height = o.value(QStringLiteral("height")).toInt(1080);
    p.aspect = o.value(QStringLiteral("aspect")).toString(QStringLiteral("16:9"));
    p.sampleRate = o.value(QStringLiteral("sampleRate")).toInt(48000);
    p.proxy = ProxySettings::fromJson(o.value(QStringLiteral("proxy")).toObject());
    for (const QJsonValue &v : o.value(QStringLiteral("tracks")).toArray())
        p.tracks.append(Track::fromJson(v.toObject()));
    for (const QJsonValue &v : o.value(QStringLiteral("markers")).toArray())
        p.markers.append(Marker::fromJson(v.toObject()));
    for (const QJsonValue &v : o.value(QStringLiteral("subtitleTracks")).toArray())
        p.subtitleTracks.append(SubtitleTrack::fromJson(v.toObject()));
    return p;
}

QString Project::trackTypeName(TrackType t)
{
    switch (t) {
    case TrackType::Video: return QStringLiteral("video");
    case TrackType::Audio: return QStringLiteral("audio");
    case TrackType::Subtitle: return QStringLiteral("subtitle");
    case TrackType::Adjustment: return QStringLiteral("adjustment");
    }
    return QStringLiteral("video");
}

TrackType Project::trackTypeFromName(const QString &s)
{
    if (s == QLatin1String("audio")) return TrackType::Audio;
    if (s == QLatin1String("subtitle")) return TrackType::Subtitle;
    if (s == QLatin1String("adjustment")) return TrackType::Adjustment;
    return TrackType::Video;
}

Project Project::loadFile(const QString &path, QString *error)
{
    Project p;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not open %1").arg(path);
        return p;
    }
    QJsonParseError pe{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid project file: %1").arg(pe.errorString());
        return p;
    }
    QJsonObject o = doc.object();
    int version = o.value(QStringLiteral("schemaVersion")).toInt(0);
    // Migrations run oldest -> newest. v1 is the first shipped schema.
    if (version == 0) {
        // Legacy/unknown: attempt best-effort load, no migration defined.
        if (error) *error = QStringLiteral("Unknown schema version 0");
        return p;
    }
    if (version > kSchemaVersion) {
        if (error) *error = QStringLiteral("Project uses a newer schema (%1); update Arena Edit").arg(version);
        return p;
    }
    p = fromJson(o);
    p.filePath = path;
    return p;
}

bool Project::saveFile(const QString &path, QString *error) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Could not write %1").arg(path);
        return false;
    }
    QJsonDocument doc(toJson());
    f.write(doc.toJson(QJsonDocument::Indented));
    f.flush();
    f.close();
    return true;
}

} // namespace arena
