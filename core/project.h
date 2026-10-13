#pragma once
// Project model: JSON-serializable, schema-versioned, non-destructive.
// Edits are stored as operations over source media; source files are never modified.
#include <QString>
#include <QList>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include <memory>

namespace arena {

enum class TrackType { Video, Audio, Subtitle, Adjustment };

struct Clip {
    QString id;
    QString sourceRef;   // relative path into the bin / .aedit bundle
    qint64 in = 0;       // frames in source
    qint64 out = 0;      // frames in source (exclusive)
    qint64 offset = 0;   // position on the timeline, frames
    double speed = 1.0;
    bool enabled = true;
    QJsonObject effects; // effect graph (Phase M3)

    QJsonObject toJson() const;
    static Clip fromJson(const QJsonObject &o);
    qint64 duration() const { return out - in; }
};

struct Marker {
    qint64 time = 0; // frames
    QString label;
    QJsonObject toJson() const;
    static Marker fromJson(const QJsonObject &o);
};

struct TimelineCue {
    qint64 startFrame = 0;  // timeline frames (project fps)
    qint64 endFrame = 0;    // exclusive
    QString text;           // may contain \n for multi-line captions
    QJsonObject toJson() const;
    static TimelineCue fromJson(const QJsonObject &o);
};

struct SubtitleTrack {
    QString id;
    QString language;      // BCP-47, e.g. "en", "ur", "ar"
    QString name;
    bool burnIn = false;   // M5: render onto exported frames
    QList<TimelineCue> cues;
    QJsonObject toJson() const;
    static SubtitleTrack fromJson(const QJsonObject &o);
};

struct Track {
    QString id;
    TrackType type = TrackType::Video;
    QString name;
    bool collapsed = false;
    QList<Clip> clips;
    QJsonObject toJson() const;
    static Track fromJson(const QJsonObject &o);
};

struct ProxySettings {
    bool enabled = true;
    int height = 540;
    QJsonObject toJson() const;
    static ProxySettings fromJson(const QJsonObject &o);
};

struct Project {
    static constexpr int kSchemaVersion = 1;

    QString id;
    QString name = QStringLiteral("Untitled");
    double fps = 30.0;
    int width = 1920;
    int height = 1080;
    QString aspect = QStringLiteral("16:9");
    int sampleRate = 48000;
    ProxySettings proxy;
    QList<Track> tracks;
    QList<Marker> markers;
    QList<SubtitleTrack> subtitleTracks;
    QString filePath; // empty until saved

    QJsonObject toJson() const;
    static Project fromJson(const QJsonObject &o);

    // Returns a loadable object; runs migrations for older schema versions.
    // On unknown/future versions returns a null id and sets *error.
    static Project loadFile(const QString &path, QString *error = nullptr);
    bool saveFile(const QString &path, QString *error = nullptr) const;

    static QString trackTypeName(TrackType t);
    static TrackType trackTypeFromName(const QString &s);
};

} // namespace arena
