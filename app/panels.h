#pragma once
// Workspace panels: Program Monitor (with overlay surface), Media Bin,
// Inspector, Subtitle manager.
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVector>
#include <QStringList>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QPushButton>
#include <QFormLayout>

#include "media.h"
#include "project.h"

namespace arena {

// BCP-47 choices for subtitle tracks (the eight supported UI languages plus
// "und"). Shared by the Subtitles widget and the AI panel (M6.2).
inline QStringList subtitleLanguages()
{
    return {QStringLiteral("en"), QStringLiteral("ur"), QStringLiteral("ar"),
            QStringLiteral("es"), QStringLiteral("fr"), QStringLiteral("hi"),
            QStringLiteral("zh"), QStringLiteral("ja"), QStringLiteral("und")};
}

// Draws safe-area / grid / thirds overlays on a letterboxed preview surface,
// with the decoded video frame letterboxed to the project aspect ratio.
class PreviewSurface : public QWidget {
    Q_OBJECT
public:
    explicit PreviewSurface(QWidget *parent = nullptr);
    void setShowGuides(bool on);
    void setShowGrid(bool on);
    void setShowThirds(bool on);
    void setAspect(double w, double h);
    void setFrame(const QImage &img); // cleared with an empty image
    const QImage &frame() const { return m_frame; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool m_guides = true;
    bool m_grid = false;
    bool m_thirds = false;
    double m_aspect = 16.0 / 9.0;
    QImage m_frame;
};

// Horizontal min/max waveform strip (accent-colored bars).
class WaveformStrip : public QWidget {
    Q_OBJECT
public:
    explicit WaveformStrip(QWidget *parent = nullptr);
    void setPeaks(const QVector<Peak> &peaks);
    const QVector<Peak> &peaks() const { return m_peaks; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<Peak> m_peaks;
};

// A row of evenly spaced source thumbnails (filmstrip).
class ThumbnailStrip : public QWidget {
    Q_OBJECT
public:
    explicit ThumbnailStrip(QWidget *parent = nullptr);
    void setThumbs(const QList<QImage> &thumbs);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QList<QImage> m_thumbs;
};

class MediaBinWidget : public QWidget {
    Q_OBJECT
public:
    explicit MediaBinWidget(QWidget *parent = nullptr);
    void retranslateUi();
    void addPaths(const QStringList &paths);
    QStringList paths() const;

    // Probe status updates (driven by the main window's background probes).
    void setItemProbing(const QString &path);
    void setItemInfo(const QString &path, const MediaInfo &info);
    // After a relink: swap the stored path and rename the row.
    void replacePath(const QString &oldPath, const QString &newPath);

signals:
    void itemDoubleClicked(const QString &path); // double-click = add to timeline
    void addToTimelineRequested(const QString &path);
    void relinkRequested(const QString &path);
    void mediaImported(const QStringList &paths);

private:
    void refreshBadges();
    QTreeWidgetItem *itemForPath(const QString &path) const;
    void setItemText(QTreeWidgetItem *item, const QString &text);
    QTreeWidget *m_tree;
    QWidget *m_empty = nullptr;
    QLabel *m_emptyTitle;
    QLabel *m_emptyBody;
    QPushButton *m_import;
    QStringList m_paths;
};

// Summary of the clip currently selected on the timeline (for the Inspector).
struct ClipSelection {
    QString clipId;       // project clip id (M3: effects editor)
    QString source;       // file name
    bool hasVideo = false;
    bool missing = false;
    qint64 in = 0;
    qint64 out = 0;
    qint64 offset = 0;
    double fps = 30.0;
    QJsonObject effects;  // M3: current clip effects
};

class InspectorWidget : public QWidget {
    Q_OBJECT
public:
    explicit InspectorWidget(QWidget *parent = nullptr);
    void retranslateUi();
    void setProjectName(const QString &name, double fps, int w, int h);
    void setClip(const ClipSelection &clip);
    void clearSelection();

signals:
    // M3: the user applied the effects editor (values -> effect JSON).
    void effectsEdited(const QString &clipId, const QJsonObject &fx);

private:
    QFormLayout *m_form;
    QString m_clipId;
    QLabel *m_emptyTitle;
    QLabel *m_emptyBody;
    QWidget *m_empty;
    QWidget *m_content;
};

// M5.3: full subtitle manager — track CRUD, SRT/VTT/ASS import/export,
// burn-in toggle, cue table with SMPTE time codes and reading-speed
// warnings (flagged cues are underlined + tinted). Mutates the project
// directly and emits subtitlesChanged() after every change (autosave).
class SubtitlesWidget : public QWidget {
    Q_OBJECT
public:
    explicit SubtitlesWidget(QWidget *parent = nullptr);
    void retranslateUi();
    // Attach/detach the project this panel edits (nullptr = no project).
    void setProject(Project *project);

signals:
    void subtitlesChanged();

private:
    void rebuildTracks();
    void rebuildCues();
    void addTrackDialog();
    void importTrack();
    void exportCurrentTrack();
    void deleteCurrentTrack();
    void addCue();
    void deleteCue();
    void editCue(int row);
    SubtitleTrack *currentTrack();
    int currentTrackIndex() const;

    Project *m_project = nullptr;
    double m_fps = 30.0;
    QTreeWidget *m_tree = nullptr;
    QTableWidget *m_cues = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_addTrack = nullptr;
    QPushButton *m_import = nullptr;
    QPushButton *m_export = nullptr;
    QPushButton *m_deleteTrack = nullptr;
    QPushButton *m_addCue = nullptr;
    QPushButton *m_deleteCue = nullptr;
    QWidget *m_empty = nullptr;
    QLabel *m_emptyTitle;
    QLabel *m_emptyBody;
};

} // namespace arena
