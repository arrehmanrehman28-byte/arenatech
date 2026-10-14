#include "timeline.h"
#include "i18n.h"
#include "util.h"
#include <cmath>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
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

void TimelineCanvas::setClips(const QList<ClipView> &clips)
{
    m_clips = clips;
    update();
}

void TimelineCanvas::setPlayhead(qint64 frame)
{
    m_playhead = frame;
    update();
}

void TimelineCanvas::setMarkers(const QList<qint64> &frames)
{
    m_markers = frames;
    update();
}

qint64 TimelineCanvas::frameAtX(double x) const
{
    const int headerW = 56;
    if (x < headerW) return 0;
    return qMax<qint64>(0, qint64(std::lround((x - headerW) * framesPerPx())));
}


double TimelineCanvas::xToFrame(double x) const
{
    const double fpp = framesPerPx();
    if (fpp <= 0.0) return 0;
    return (x - 56.0) * fpp;
}

int TimelineCanvas::edgeAt(double x, int y, int *edge) const
{
    if (m_trackIds.isEmpty()) return -1;
    const int rulerH = 24;
    const int laneH = qMax(40, (height() - rulerH) / qMax(1, m_trackIds.size()));
    const int laneTop = rulerH; // clips live on the first (video) lane
    if (y < laneTop || y >= laneTop + laneH) {
        *edge = -1;
        return -1;
    }
    // Nearest edge within tolerance wins.
    int best = -1;
    double bestDist = kEdgeTol + 0.5;
    for (int i = 0; i < m_clips.size(); ++i) {
        const ClipView &c = m_clips[i];
        const double lx = 56.0 + double(c.offsetFrames) / framesPerPx();
        const double rx = 56.0 + double(c.offsetFrames + c.durationFrames) / framesPerPx();
        const double dl = std::abs(x - lx);
        const double dr = std::abs(x - rx);
        // Must be horizontally inside the clip's span (tolerance either side).
        if (x < lx - kEdgeTol || x > rx + kEdgeTol) continue;
        if (dl <= dr) {
            if (dl < bestDist) {
                bestDist = dl;
                best = i;
                *edge = 0;
            }
        } else {
            if (dr < bestDist) {
                bestDist = dr;
                best = i;
                *edge = 1;
            }
        }
    }
    return best;
}

void TimelineCanvas::applyPreview(double x)
{
    if (m_drag == DragMode::None || m_dragClip < 0) return;
    const ClipView &c = m_clips.value(m_dragClip);
    if (c.clipId.isEmpty()) return;
    qint64 delta = qint64(std::lround(xToFrame(x) - xToFrame(m_pressX)));

    // Snap: the moving edge is pulled to the nearest clip edge or the
    // playhead within 4 px (spec M2: snap with visible feedback).
    if (m_snapEnabled) {
        const qint64 base = m_dragEdge == 0 ? m_oOffset
                                            : m_oOffset + (m_oOut - m_oIn);
        const double fpp = framesPerPx();
        qint64 best = -1;
        double bestDist = 4.0; // px
        auto consider = [&](qint64 target) {
            const double d = (double(target) - double(base)) * fpp -
                             (double(delta) * fpp);
            if (std::abs(d) < bestDist) {
                bestDist = std::abs(d);
                best = target - base;
            }
        };
        for (int i = 0; i < m_clips.size(); ++i) {
            if (i == m_dragClip || (m_drag == DragMode::Roll && i == m_rollClip))
                continue;
            consider(m_clips[i].offsetFrames);
            consider(m_clips[i].offsetFrames + m_clips[i].durationFrames);
        }
        consider(m_playhead);
        if (best != -1) {
            delta = best;
            m_snapLine = base + best;
            emit snapLineChanged(m_snapLine, true);
        } else if (m_snapLine >= 0) {
            emit snapLineChanged(m_snapLine, false);
            m_snapLine = -1;
        }
    }

    if (m_drag == DragMode::Trim) {
        if (m_dragEdge == 1) {
            // Right edge: change out (source), offset/in fixed.
            qint64 dur = m_oOut - m_oIn + delta;
            const qint64 maxDur = c.sourceDurationFrames > m_oIn
                                       ? c.sourceDurationFrames - m_oIn
                                       : m_oOut - m_oIn;
            if (dur > maxDur) dur = maxDur;
            if (dur < kMinDur) dur = kMinDur;
            m_pOffset = m_oOffset;
            m_pIn = m_oIn;
            m_pOut = m_oIn + dur;
        } else {
            // Left edge: change in (source) and offset together.
            qint64 in = m_oIn + delta;
            if (in < m_oIn - m_oOffset) in = m_oIn - m_oOffset; // offset >= 0
            if (in < 0) in = 0;
            qint64 dur = m_oOut - in;
            if (dur < kMinDur) {
                in = m_oOut - kMinDur;
                dur = kMinDur;
            }
            m_pOffset = m_oOffset + (in - m_oIn);
            m_pIn = in;
            m_pOut = m_oOut;
        }
    } else {
        // Roll: shared boundary moves; left clip out and right clip in/offset.
        qint64 d = delta;
        // Left clip keeps its duration >= 1; right clip keeps in >= 0.
        qint64 lo = kMinDur - (m_oOut - m_oIn);
        if (lo < -m_rollOIn) lo = -m_rollOIn;
        // Right clip keeps its duration >= 1.
        qint64 hi = (m_rollOOut - m_rollOIn) - kMinDur;
        // Source limits (when known).
        if (c.sourceDurationFrames > m_oOut) {
            const qint64 cap = c.sourceDurationFrames - m_oOut;
            if (hi > cap) hi = cap;
        }
        const ClipView &rn = m_clips.value(m_rollClip);
        if (rn.sourceDurationFrames > m_rollOOut) {
            const qint64 cap = rn.sourceDurationFrames - m_rollOOut;
            if (hi > cap) hi = cap;
        }
        d = qBound(lo, d, hi);
        m_pOffset = m_oOffset;
        m_pIn = m_oIn;
        m_pOut = m_oOut + d;
        m_pRollOffset = m_rollOOffset + d;
        m_pRollIn = m_rollOIn + d;
        m_pRollOut = m_rollOOut + d;
    }
    m_previewActive = true;
    m_moved = (m_pOffset != m_oOffset) || (m_pIn != m_oIn) || (m_pOut != m_oOut);
}

QList<ClipPatch> TimelineCanvas::buildPatches() const
{
    QList<ClipPatch> out;
    if (!m_previewActive) return out;
    ClipPatch a;
    a.clipId = m_clips.value(m_dragClip).clipId;
    a.offset = m_pOffset;
    a.in = m_pIn;
    a.out = m_pOut;
    out.append(a);
    if (m_drag == DragMode::Roll && m_rollClip >= 0) {
        ClipPatch b;
        b.clipId = m_clips.value(m_rollClip).clipId;
        b.offset = m_pRollOffset;
        b.in = m_pRollIn;
        b.out = m_pRollOut;
        out.append(b);
    }
    return out;
}

void TimelineCanvas::mousePressEvent(QMouseEvent *e)
{
    if (!m_hasProject) {
        QWidget::mousePressEvent(e);
        return;
    }
    // Right-click a ruler marker to remove it.
    if (e->button() == Qt::RightButton && e->position().y() < 24) {
        const double fpp = framesPerPx();
        for (qint64 m : m_markers) {
            const double mx = 56.0 + double(m) / fpp;
            if (std::abs(e->position().x() - mx) <= 4.0) {
                emit markerRemoveRequested(m);
                e->accept();
                return;
            }
        }
        QWidget::mousePressEvent(e);
        return;
    }
    if (e->button() != Qt::LeftButton || e->position().x() <= 56) {
        QWidget::mousePressEvent(e);
        return;
    }
    // Edge grab starts a trim (or roll on a shared boundary).
    int edge = -1;
    int hit = edgeAt(e->position().x(), e->position().y(), &edge);
    if (hit >= 0 && edge >= 0) {
        m_drag = DragMode::Trim;
        m_dragClip = hit;
        m_dragEdge = edge;
        m_rollClip = -1;
        m_pressX = int(e->position().x());
        // A shared boundary (right edge of N == left edge of N+1) is a roll:
        // it can be grabbed from either side.
        if (edge == 0 && hit > 0 &&
            m_clips[hit - 1].offsetFrames + m_clips[hit - 1].durationFrames ==
                m_clips[hit].offsetFrames) {
            hit = hit - 1;
            edge = 1;
        }
        const ClipView &c = m_clips[hit];
        m_oOffset = c.offsetFrames;
        m_oIn = c.inFrames;
        m_oOut = c.inFrames + c.durationFrames;
        if (edge == 1 && hit + 1 < m_clips.size()) {
            const ClipView &n = m_clips[hit + 1];
            if (n.offsetFrames == c.offsetFrames + c.durationFrames) {
                m_drag = DragMode::Roll;
                m_rollClip = hit + 1;
                m_rollOOffset = n.offsetFrames;
                m_rollOIn = n.inFrames;
                m_rollOOut = n.inFrames + n.durationFrames;
            }
        }
        m_previewActive = false;
        m_moved = false;
        m_snapLine = -1;
        setCursor(Qt::SplitHCursor);
        e->accept();
        return;
    }
    m_scrubbing = true;
    // Clip in the video track under the cursor gets the selection;
    // empty space deselects.
    const qint64 f = frameAtX(e->position().x());
    bool overClip = false;
    for (const ClipView &c : m_clips)
        if (f >= c.offsetFrames && f < c.offsetFrames + c.durationFrames) {
            emit clipClicked(c.offsetFrames);
            overClip = true;
            break;
        }
    if (!overClip) emit clipDeselected();
    emit seekRequested(f);
    e->accept();
}

void TimelineCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_drag != DragMode::None) {
        // Capture the patches before resetting the drag state.
        const QList<ClipPatch> patches = m_moved ? buildPatches() : QList<ClipPatch>();
        m_drag = DragMode::None;
        m_previewActive = false;
        setCursor(Qt::ArrowCursor);
        if (m_snapLine >= 0) emit snapLineChanged(m_snapLine, false);
        m_snapLine = -1;
        if (!patches.isEmpty()) emit trimRequested(patches);
        e->accept();
        return;
    }
    if (m_scrubbing) {
        m_scrubbing = false;
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void TimelineCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_hasProject && e->button() == Qt::LeftButton &&
        e->position().y() < 24 && e->position().x() > 56) {
        emit markerAddRequested(frameAtX(e->position().x()));
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void TimelineCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag != DragMode::None && (e->buttons() & Qt::LeftButton)) {
        applyPreview(e->position().x());
        update();
        e->accept();
        return;
    }
    if (m_scrubbing && (e->buttons() & Qt::LeftButton)) {
        emit seekRequested(frameAtX(e->position().x()));
        e->accept();
        return;
    }
    // Hover: show the split cursor over a clip edge.
    int edge = -1;
    const int hit = edgeAt(e->position().x(), e->position().y(), &edge);
    if (hit >= 0 && edge >= 0)
        setCursor(Qt::SplitHCursor);
    else
        setCursor(Qt::ArrowCursor);
    QWidget::mouseMoveEvent(e);
}

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

    // Clips (video track) — waveform texture, selection, missing-media badge.
    if (!m_clips.isEmpty() && !m_trackIds.isEmpty()) {
        const int laneY = rulerH;
        for (int ci = 0; ci < m_clips.size(); ++ci) {
            ClipView c = m_clips[ci];
            // Live preview geometry while trimming/rolling.
            if (m_previewActive) {
                if (ci == m_dragClip) {
                    c.offsetFrames = m_pOffset;
                    c.durationFrames = m_pOut - m_pIn;
                } else if (m_drag == DragMode::Roll && ci == m_rollClip) {
                    c.offsetFrames = m_pRollOffset;
                    c.durationFrames = m_pRollOut - m_pRollIn;
                }
            }
            const double x0 = headerW + double(c.offsetFrames) / fpp;
            const double w = double(c.durationFrames) / fpp;
            if (x0 + w < headerW || x0 > width()) continue;
            const QRectF r(x0 + 1, laneY + 2, qMax(6.0, w - 2), laneH - 5);
            const QColor base = palette().highlight().color();
            QColor fill = base;
            fill.setAlpha(c.selected ? 96 : 56);
            p.setPen(c.missing ? QPen(QColor(0xE5, 0x53, 0x4B), 2)
                               : QPen(base.lighter(140), 1));
            p.setBrush(fill);
            p.drawRoundedRect(r, 4, 4);
            if (!c.waveform.isEmpty() && r.width() > 16) {
                p.setPen(QPen(QColor(255, 255, 255, 96), 1));
                const double midY = r.center().y();
                const double maxH = r.height() * 0.38;
                const double step = r.width() / double(c.waveform.size());
                for (int i = 0; i < c.waveform.size(); i += 2) {
                    const double x = r.left() + i * step;
                    const double a = qMax(2.0, qAbs(double(c.waveform[i].hi)) * maxH);
                    p.drawLine(QPointF(x, midY - a), QPointF(x, midY + a * 0.6));
                }
            }
            if (c.missing) {
                p.setPen(QColor(0xE5, 0x53, 0x4B));
                p.drawText(r.adjusted(6, 2, -6, -2), Qt::AlignLeft | Qt::AlignVCenter,
                           QStringLiteral("⚠ "));
            }
        }
    }

    // Playhead.
    {
        const double x = headerW + double(m_playhead) / fpp;
        if (x < width()) {
            p.setPen(QPen(palette().highlight().color(), 1.4));
            p.drawLine(QPointF(x, rulerH), QPointF(x, height()));
            p.setBrush(palette().highlight().color());
            p.setPen(Qt::NoPen);
            QPolygonF tri;
            tri << QPointF(x - 5, 0) << QPointF(x + 5, 0) << QPointF(x, 8);
            p.drawPolygon(tri);
        }
    }

    // Ruler markers (violet, per spec's AI/violet accent for optional items).
    for (qint64 m : m_markers) {
        const double mx = 56.0 + double(m) / fpp;
        if (mx < 56 || mx > width()) continue;
        p.setPen(QPen(QColor(0x9B, 0x6D, 0xFF), 1.2));
        p.drawLine(QPointF(mx, 2), QPointF(mx, 14));
        p.setBrush(QColor(0x9B, 0x6D, 0xFF));
        p.setPen(Qt::NoPen);
        QPolygonF tri;
        tri << QPointF(mx - 4, 14) << QPointF(mx + 4, 14) << QPointF(mx, 20);
        p.drawPolygon(tri);
    }

    // Snap feedback: violet line while a trim edge is snapped (M2).
    if (m_snapLine >= 0) {
        const double sx = headerW + double(m_snapLine) / fpp;
        if (sx > 0 && sx < width()) {
            p.setPen(QPen(QColor(0x9B, 0x6D, 0xFF), 1.4));
            p.drawLine(QPointF(sx, rulerH), QPointF(sx, height()));
        }
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
    m_snap->setAccessibleName(arenaTr("timeline.snap")); // M8: UIA name
    m_magnet = new QToolButton;
    m_magnet->setCheckable(true);
    m_magnet->setText(arenaTr("timeline.magnet"));
    m_magnet->setAccessibleName(arenaTr("timeline.magnet")); // M8: UIA name
    m_magnet->setEnabled(false); // clips phase enables
    m_markers = new QToolButton;
    m_markers->setCheckable(true);
    m_markers->setText(arenaTr("timeline.markers"));
    m_markers->setAccessibleName(arenaTr("timeline.markers")); // M8: UIA name
    m_markers->setEnabled(false);
    m_zoomIn = new QToolButton;
    m_zoomIn->setText(QStringLiteral("+"));
    m_zoomIn->setAccessibleName(arenaTr("timeline.zoomIn")); // M8: UIA name (glyph text)
    m_zoomOut = new QToolButton;
    m_zoomOut->setText(QStringLiteral("−"));
    m_zoomOut->setAccessibleName(arenaTr("timeline.zoomOut")); // M8: UIA name (glyph text)
    m_trackHeight = new QSlider(Qt::Horizontal);
    m_trackHeight->setRange(32, 160);
    m_trackHeight->setValue(64);
    m_trackHeight->setEnabled(false);
    m_trackHeight->setMaximumWidth(120);
    m_trackHeight->setAccessibleName(arenaTr("timeline.trackHeight")); // M8: UIA name
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

    // Snap toggle drives the canvas's edge snapping.
    connect(m_snap, &QToolButton::toggled, m_canvas, &TimelineCanvas::setSnapEnabled);

    connect(m_zoomIn, &QToolButton::clicked, this, [this] {
        m_canvas->setZoom(m_canvas->zoom() * 1.25);
        emit zoomChanged(m_canvas->zoom());
    });
    connect(m_zoomOut, &QToolButton::clicked, this, [this] {
        m_canvas->setZoom(m_canvas->zoom() * 0.8);
        emit zoomChanged(m_canvas->zoom());
    });

    // Forward canvas interaction (scrub, select, deselect, trim, snap).
    connect(m_canvas, &TimelineCanvas::seekRequested, this, &TimelineWidget::seekRequested);
    connect(m_canvas, &TimelineCanvas::clipClicked, this, &TimelineWidget::clipClicked);
    connect(m_canvas, &TimelineCanvas::clipDeselected, this, &TimelineWidget::clipDeselected);
    connect(m_canvas, &TimelineCanvas::trimRequested, this, &TimelineWidget::trimRequested);
    connect(m_canvas, &TimelineCanvas::snapLineChanged, this, &TimelineWidget::snapLineChanged);
    connect(m_canvas, &TimelineCanvas::markerAddRequested,
            this, &TimelineWidget::markerAddRequested);
    connect(m_canvas, &TimelineCanvas::markerRemoveRequested,
            this, &TimelineWidget::markerRemoveRequested);
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
