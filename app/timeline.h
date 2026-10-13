#pragma once
// Timeline panel: toolbar (snap/magnet/markers/zoom/track height) + ruler and
// track lanes. The media timeline is ALWAYS left-to-right (time flows LTR),
// even when the app is in RTL — only text-track content mirrors.
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVector>

#include "media.h"

namespace arena {

// One clip as the canvas should draw it (frames on its track).
struct ClipView {
    qint64 offsetFrames = 0;
    qint64 durationFrames = 0;
    QVector<Peak> waveform;
    bool selected = false; // clip currently shown in the monitor
    bool missing = false;  // source media missing
};

// Ruler + track bands drawn from the current zoom and fps.
class TimelineCanvas : public QWidget {
    Q_OBJECT
public:
    explicit TimelineCanvas(QWidget *parent = nullptr);
    void setFps(double fps);
    void setZoomFramesPerPx(double fpp); // frames per pixel at zoom 1.0
    void setZoom(double zoom);           // 0.125 .. 16
    void setTracks(const QStringList &trackIds, const QStringList &trackNames);
    void setHasProject(bool has);
    double zoom() const { return m_zoom; }
    // Clips live on the first (video) track.
    void setClips(const QList<ClipView> &clips);
    void setPlayhead(qint64 frame);
    double framesPerPx() const { return m_framesPerPx * m_zoom; }
    qint64 frameAtX(double x) const;

signals:
    void seekRequested(qint64 frame); // ruler scrub (press + drag)
    void clipClicked(qint64 frame);   // click a clip to select it

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

private:
    double m_fps = 30.0;
    double m_zoom = 1.0;
    double m_framesPerPx = 2.0; // at zoom 1
    QStringList m_trackIds;
    QStringList m_trackNames;
    bool m_hasProject = false;
    QList<ClipView> m_clips;
    qint64 m_playhead = 0;
    bool m_scrubbing = false;
};

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent = nullptr);
    void retranslateUi();
    void setProject(bool has, double fps);
    TimelineCanvas *canvas() const { return m_canvas; }
    QToolButton *snapToggle() const { return m_snap; }

signals:
    void zoomChanged(double zoom);
    void seekRequested(qint64 frame);
    void clipClicked(qint64 frame);

private:
    QToolButton *m_snap;
    QToolButton *m_magnet;
    QToolButton *m_markers;
    QToolButton *m_zoomIn;
    QToolButton *m_zoomOut;
    QSlider *m_trackHeight;
    TimelineCanvas *m_canvas;
};

} // namespace arena
