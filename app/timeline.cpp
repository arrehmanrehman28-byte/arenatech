#include "timeline.h"
#include "i18n.h"
#include "util.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QWheelEvent>
#include <QPushButton>
#include <QLabel>

namespace arena {

// --- TimelineCanvas ----------------------------------------------------------

TimelineCanvas::TimelineCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(400, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Spec 3.6: media timelines stay LTR in every language.
    setLayoutDirection(Qt::LeftToRight);
}

void TimelineCanvas::setFps(double fps) { m_fps = fps; update(); }
void TimelineCanvas::setZoomFramesPerPx(double fpp) { m_framesPerPx = fpp; }
void TimelineCanvas::setZoom(double zoom)
{
    m_zoom = qBound(0.125, zoom, 16.0);
    update();
}
void TimelineCanvas::setTracks(const QStringList &ids, const QStringList &names)
{
    m_trackIds = ids;
    m_trackNames = names;
    update();
}
void TimelineCanvas::setHasProject(bool has) { m_hasProject = has; update(); }

void TimelineCanvas::wheelEvent(QWheelEvent *e)
{
    const bool ctrl = (e->modifiers() & Qt::ControlModifier) != 0;
    if (ctrl && e->angleDelta().y() != 0) {
        setZoom(m_zoom * (e->angleDelta().y() > 0 ? 1.25 : 0.8));
        e->accept();
    } else {
        QWidget::wheelEvent(e);
    }
}

void TimelineCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().window().color());
    const int headerW = 56;
    const int rulerH = 24;

    if (!m_hasProject) {
        p.setPen(palette().windowText().color());
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("—"));
        return;
    }

    // Ruler with frame ticks.
    const double fpp = m_framesPerPx * m_zoom; // frames per pixel
    p.fillRect(0, 0, width(), rulerH, palette().mid().color().darker(110));
    p.setPen(palette().windowText().color());
    // Choose tick spacing: 1, 5, 10, 30, 60, 300 frames so ticks are >= 40 px.
    int tickFrames = 1;
    for (int candidate : {1, 5, 10, 30, 60, 300, 900}) {
        if (double(candidate) / fpp >= 40.0) { tickFrames = candidate; break; }
        tickFrames = candidate;
    }
    for (double x = 0; x < width() - headerW; x += 1.0) {
        const qint64 frame = qint64(x * fpp);
        if (frame % tickFrames == 0) {
            const bool major = (frame % (tickFrames * 5) == 0);
            const int h = major ? rulerH - 6 : rulerH / 2;
            p.drawLine(int(x) + headerW, rulerH, int(x) + headerW, rulerH - h);
            if (major) {
                p.drawText(QRectF(x + headerW + 2, 2, 70, rulerH - 4),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           framesToTimecode(frame, m_fps));
            }
        }
    }
    p.setPen(QPen(palette().windowText().color(), 1, Qt::DashLine));
    p.drawLine(headerW, 1, width(), 1);

    // Track lanes.
    const int laneH = qMax(40, (height() - rulerH) / qMax(1, m_trackIds.size()));
    for (int i = 0; i < m_trackIds.size(); ++i) {
        const int y = rulerH + i * laneH;
        p.fillRect(0, y, headerW, laneH - 1, palette().mid().color());
        p.setPen(palette().windowText().color());
        p.drawText(QRect(2, y, headerW - 4, laneH), Qt::AlignRight | Qt::AlignVCenter,
                   m_trackNames.value(i, m_trackIds.at(i)));
        p.fillRect(headerW, y, width() - headerW, laneH - 1, palette().base().color());
    }
}

// --- TimelineWidget ----------------------------------------------------------

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    // Keep the whole timeline panel LTR (media time flows left to right).
    setLayoutDirection(Qt::LeftToRight);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(6);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(4);
    m_snap = new QToolButton;
    m_snap->setCheckable(true);
    m_snap->setChecked(true);
    m_snap->setText(arenaTr("timeline.snap"));
    m_magnet = new QToolButton;
    m_magnet->setCheckable(true);
    m_magnet->setText(arenaTr("timeline.magnet"));
    m_magnet->setEnabled(false); // clips phase enables
    m_markers = new QToolButton;
    m_markers->setCheckable(true);
    m_markers->setText(arenaTr("timeline.markers"));
    m_markers->setEnabled(false);
    m_zoomIn = new QToolButton;
    m_zoomIn->setText(QStringLiteral("+"));
    m_zoomOut = new QToolButton;
    m_zoomOut->setText(QStringLiteral("−"));
    m_trackHeight = new QSlider(Qt::Horizontal);
    m_trackHeight->setRange(32, 160);
    m_trackHeight->setValue(64);
    m_trackHeight->setEnabled(false);
    m_trackHeight->setMaximumWidth(120);
    bar->addWidget(m_snap);
    bar->addWidget(m_magnet);
    bar->addWidget(m_markers);
    bar->addSpacing(12);
    bar->addWidget(m_zoomOut);
    bar->addWidget(m_zoomIn);
    bar->addSpacing(12);
    bar->addWidget(m_trackHeight);
    bar->addStretch(1);
    lay->addLayout(bar);

    m_canvas = new TimelineCanvas;
    lay->addWidget(m_canvas, 1);

    connect(m_zoomIn, &QToolButton::clicked, this, [this] {
        m_canvas->setZoom(m_canvas->zoom() * 1.25);
        emit zoomChanged(m_canvas->zoom());
    });
    connect(m_zoomOut, &QToolButton::clicked, this, [this] {
        m_canvas->setZoom(m_canvas->zoom() * 0.8);
        emit zoomChanged(m_canvas->zoom());
    });
}

void TimelineWidget::retranslateUi()
{
    m_snap->setText(arenaTr("timeline.snap"));
    m_magnet->setText(arenaTr("timeline.magnet"));
    m_markers->setText(arenaTr("timeline.markers"));
}

void TimelineWidget::setProject(bool has, double fps)
{
    m_canvas->setHasProject(has);
    m_canvas->setFps(fps);
    if (has) {
        m_canvas->setTracks({QStringLiteral("V1"), QStringLiteral("A1")},
                            {QStringLiteral("V1"), QStringLiteral("A1")});
        m_magnet->setEnabled(true);
        m_markers->setEnabled(true);
        m_trackHeight->setEnabled(true);
    }
}

} // namespace arena
