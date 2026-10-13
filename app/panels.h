#pragma once
// Workspace panels: Program Monitor (with overlay surface), Media Bin,
// Inspector, Subtitle manager.
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QTreeWidget>
#include <QVector>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFormLayout>

#include "media.h"

namespace arena {

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

class InspectorWidget : public QWidget {
    Q_OBJECT
public:
    explicit InspectorWidget(QWidget *parent = nullptr);
    void retranslateUi();
    void setProjectName(const QString &name, double fps, int w, int h);
    void clearSelection();

private:
    QFormLayout *m_form;
    QLabel *m_emptyTitle;
    QLabel *m_emptyBody;
    QWidget *m_empty;
    QWidget *m_content;
};

class SubtitlesWidget : public QWidget {
    Q_OBJECT
public:
    explicit SubtitlesWidget(QWidget *parent = nullptr);
    void retranslateUi();
    void addTrack(const QString &name, const QString &bcp47);
    void clearTracks();

signals:
    void trackAdded(const QString &name, const QString &bcp47);

private:
    void addTrackDialog();
    QTreeWidget *m_tree;
    QWidget *m_empty = nullptr;
    QLabel *m_emptyTitle;
    QLabel *m_emptyBody;
    QPushButton *m_add;
};

} // namespace arena
