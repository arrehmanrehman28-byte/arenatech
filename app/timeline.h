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
    QString clipId;
    qint64 offsetFrames = 0;
    qint64 durationFrames = 0;
    qint64 inFrames = 0;                // source in point
    qint64 sourceDurationFrames = 0;    // full source length (trim clamp)
    QVector<Peak> waveform;
    bool selected = false; // currently selected clip
    bool missing = false;  // source media missing
};

// One clip's new timeline/source position after a trim or roll edit.
struct ClipPatch {
    QString clipId;
    qint64 offset = 0;
    qint64 in = 0;
    qint64 out = 0;
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
    void setSnapEnabled(bool on) { m_snapEnabled = on; }
    bool snapEnabled() const { return m_snapEnabled; }

signals:
    void seekRequested(qint64 frame);        // ruler scrub (press + drag)
    void clipClicked(qint64 frame);          // click a clip to select it
    void clipDeselected();                   // click on empty timeline space
    void trimRequested(const QList<ClipPatch> &patches); // edge drag / roll
    void snapLineChanged(qint64 frame, bool on);         // visual snap feedback

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

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

    // --- Trim / roll dragging -------------------------------------------
    enum class DragMode { None, Trim, Roll };
    DragMode m_drag = DragMode::None;
    int m_dragClip = -1;    // index into m_clips (video lane)
    int m_dragEdge = -1;    // 0 = left edge, 1 = right edge
    int m_pressX = 0;       // x where the drag started
    // Original values at drag start (restored each move, then patched).
    qint64 m_oOffset = 0, m_oIn = 0, m_oOut = 0;
    // Roll: the neighbour clip whose in/out/offset also move.
    int m_rollClip = -1;
    qint64 m_rollOOffset = 0, m_rollOIn = 0, m_rollOOut = 0;
    // Live preview while dragging (applied in paint).
    bool m_previewActive = false;
    qint64 m_pOffset = 0, m_pIn = 0, m_pOut = 0;
    qint64 m_pRollOffset = 0, m_pRollIn = 0, m_pRollOut = 0;
    bool m_snapEnabled = true;
    qint64 m_snapLine = -1; // frame the snap line is drawn at (-1 = off)
    bool m_moved = false;   // whether the drag actually changed values
    static constexpr int kEdgeTol = 5; // px grab tolerance on clip edges
    static constexpr qint64 kMinDur = 1; // frames

    // Hit-test a clip edge under (x, y). Returns clip index, or -1.
    // Sets *edge to 0/1 when on an edge, -1 when over the clip body.
    int edgeAt(double x, int y, int *edge) const;
    double xToFrame(double x) const;
    void applyPreview(double x); // recompute preview geometry from cursor x
    QList<ClipPatch> buildPatches() const;
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
    void clipDeselected();
    void trimRequested(const QList<ClipPatch> &patches);
    void snapLineChanged(qint64 frame, bool on);

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
