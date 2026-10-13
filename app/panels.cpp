#include "panels.h"
#include "i18n.h"
#include "util.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QApplication>
#include <QStyle>

namespace arena {

// --- PreviewSurface ---------------------------------------------------------

PreviewSurface::PreviewSurface(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PreviewSurface::setShowGuides(bool on) { m_guides = on; update(); }
void PreviewSurface::setShowGrid(bool on) { m_grid = on; update(); }
void PreviewSurface::setShowThirds(bool on) { m_thirds = on; update(); }
void PreviewSurface::setAspect(double w, double h) { m_aspect = w / h; update(); }
void PreviewSurface::setFrame(const QImage &img)
{
    m_frame = img;
    update();
}

void PreviewSurface::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0));
    // Letterbox to the project aspect ratio.
    const double ar = m_aspect;
    const double w = std::min<double>(width(), height() * ar);
    const double h = w / ar;
    const QRectF surface((width() - w) / 2, (height() - h) / 2, w, h);
    p.fillRect(surface, QColor(24, 26, 32));
    if (!m_frame.isNull()) {
        // Fit the frame into the surface, preserving its own aspect ratio.
        const QSize target = surface.toRect().size();
        const QImage fitted = m_frame.scaled(target, Qt::KeepAspectRatio,
                                             Qt::FastTransformation);
        p.drawImage(QRectF((width() - fitted.width()) / 2, (height() - fitted.height()) / 2,
                           fitted.width(), fitted.height()),
                    fitted);
    }
    p.setPen(QColor(120, 130, 150, 140));
    p.drawRect(surface);

    if (m_grid || m_thirds) {
        p.setPen(QPen(QColor(255, 255, 255, 40), 1));
        const int div = m_thirds ? 3 : 8;
        for (int i = 1; i < div; ++i) {
            p.drawLine(QPointF(surface.left() + surface.width() * i / div, surface.top()),
                       QPointF(surface.left() + surface.width() * i / div, surface.bottom()));
            p.drawLine(QPointF(surface.left(), surface.top() + surface.height() * i / div),
                       QPointF(surface.right(), surface.top() + surface.height() * i / div));
        }
    }
    if (m_guides) {
        // 90% action safe and 80% title safe.
        p.setPen(QPen(QColor(120, 200, 255, 150), 1, Qt::DashLine));
        const QRectF action = surface.adjusted(surface.width() * 0.05, surface.height() * 0.05,
                                               -surface.width() * 0.05, -surface.height() * 0.05);
        p.drawRect(action);
        p.setPen(QPen(QColor(255, 200, 90, 150), 1, Qt::DashLine));
        const QRectF title = surface.adjusted(surface.width() * 0.10, surface.height() * 0.10,
                                              -surface.width() * 0.10, -surface.height() * 0.10);
        p.drawRect(title);
    }
}

// --- WaveformStrip -----------------------------------------------------------

WaveformStrip::WaveformStrip(QWidget *parent) : QWidget(parent)
{
    setMaximumHeight(44);
    setMinimumHeight(28);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void WaveformStrip::setPeaks(const QVector<Peak> &peaks)
{
    m_peaks = peaks;
    update();
}

void WaveformStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 160));
    if (m_peaks.isEmpty()) return;
    const double mid = height() / 2.0;
    const QColor accent = palette().highlight().color();
    const double step = double(width()) / m_peaks.size();
    const double barW = qMax(1.0, step * 0.7);
    p.setPen(QPen(accent, 1.0));
    for (int i = 0; i < m_peaks.size(); ++i) {
        const double x = i * step + (step - barW) / 2;
        const double hi = mid - qMax(0.02, double(m_peaks[i].hi)) * (mid - 2);
        const double lo = mid - qMin(-0.02, double(m_peaks[i].lo)) * (mid - 2);
        p.drawLine(QPointF(x, lo), QPointF(x, hi));
    }
}

// --- ThumbnailStrip ------------------------------------------------------------

ThumbnailStrip::ThumbnailStrip(QWidget *parent) : QWidget(parent)
{
    setMaximumHeight(56);
    setMinimumHeight(24);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ThumbnailStrip::setThumbs(const QList<QImage> &thumbs)
{
    m_thumbs = thumbs;
    update();
}

void ThumbnailStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 120));
    if (m_thumbs.isEmpty()) return;
    const int h = height() - 8;
    const double totalW = width();
    const double gap = 4.0;
    const double imgW = (totalW - gap * (m_thumbs.size() - 1)) / m_thumbs.size();
    for (int i = 0; i < m_thumbs.size(); ++i) {
        const double x = i * (imgW + gap);
        const QImage fitted =
            m_thumbs[i].scaled(QSize(int(imgW), h), Qt::KeepAspectRatio, Qt::FastTransformation);
        const double fx = x + (imgW - fitted.width()) / 2;
        p.drawImage(QRectF(fx, 4, fitted.width(), fitted.height()), fitted);
        p.setPen(QPen(QColor(255, 255, 255, 60), 1));
        p.drawRect(QRectF(x, 4, imgW, h));
    }
}

// --- Empty-state helper ------------------------------------------------------

namespace {
// Builds an empty-state card; returns the card and hands back the labels.
QWidget *makeEmptyState(const QString &titleKey, const QString &bodyKey,
                        QLabel **titleOut, QLabel **bodyOut)
{
    auto *card = new QWidget;
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(24, 24, 24, 24);
    lay->setSpacing(8);
    auto *title = new QLabel(arenaTr(titleKey));
    title->setAlignment(Qt::AlignCenter);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() + 2);
    title->setFont(tf);
    auto *body = new QLabel(arenaTr(bodyKey));
    body->setProperty("role", QStringLiteral("muted"));
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignCenter);
    *titleOut = title;
    *bodyOut = body;
    lay->addStretch(1);
    lay->addWidget(title);
    lay->addWidget(body);
    lay->addStretch(1);
    return card;
}
} // namespace

// --- MediaBinWidget ----------------------------------------------------------

MediaBinWidget::MediaBinWidget(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({arenaTr("common.name"), arenaTr("media.type"),
                             arenaTr("media.status")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 168);
    m_tree->header()->resizeSection(2, 80);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) {
        const QString path = it->data(0, Qt::UserRole).toString();
        emit itemDoubleClicked(path);
        emit addToTimelineRequested(path);
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_tree->itemAt(pos);
        if (!item) return;
        const QString path = item->data(0, Qt::UserRole).toString();
        QMenu menu;
        auto *addAct = menu.addAction(arenaTr("media.addToTimeline"));
        auto *relink = menu.addAction(arenaTr("panel.mediaBin.relink"));
        auto *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (chosen == addAct) {
            emit addToTimelineRequested(path);
        } else if (chosen == relink) {
            emit relinkRequested(path);
        }
    });
    lay->addWidget(m_tree, 1);

    auto *row = new QHBoxLayout;
    m_import = new QPushButton(arenaTr("panel.mediaBin.import"));
    connect(m_import, &QPushButton::clicked, this, [this] {
        const auto files = QFileDialog::getOpenFileNames(
            this, arenaTr("panel.mediaBin.import"), QString(),
            arenaTr("media.filter"));
        if (!files.isEmpty()) addPaths(files);
    });
    row->addWidget(m_import);
    lay->addLayout(row);

    // Empty state until items exist.
    QLabel *t = nullptr;
    QLabel *b = nullptr;
    m_empty = makeEmptyState(QStringLiteral("panel.mediaBin.empty.title"),
                             QStringLiteral("panel.mediaBin.empty.body"), &t, &b);
    m_emptyTitle = t;
    m_emptyBody = b;
    m_empty->show();
    lay->addWidget(m_empty, 1);
    m_import->hide();
    m_tree->hide();
}

void MediaBinWidget::retranslateUi()
{
    m_tree->setHeaderLabels({arenaTr("common.name"), arenaTr("media.type"),
                             arenaTr("media.status")});
    m_import->setText(arenaTr("panel.mediaBin.import"));
    if (m_emptyTitle) m_emptyTitle->setText(arenaTr("panel.mediaBin.empty.title"));
    if (m_emptyBody) m_emptyBody->setText(arenaTr("panel.mediaBin.empty.body"));
    // Refresh per-item status text for items not yet probed.
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        if (item->text(1).isEmpty() || item->text(1) == arenaTr("media.probing"))
            item->setText(1, arenaTr("media.probing"));
    }
}

void MediaBinWidget::addPaths(const QStringList &paths)
{
    QStringList fresh;
    m_paths += paths;
    for (const QString &p : paths) {
        if (itemForPath(p)) continue;
        fresh << p;
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, QFileInfo(p).fileName());
        item->setText(1, arenaTr("media.probing"));
        item->setData(0, Qt::UserRole, p);
    }
    m_empty->hide();
    m_tree->show();
    m_import->show();
    refreshBadges();
    if (!fresh.isEmpty()) emit mediaImported(fresh);
}

QStringList MediaBinWidget::paths() const { return m_paths; }

QTreeWidgetItem *MediaBinWidget::itemForPath(const QString &path) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == path) return item;
    }
    return nullptr;
}

void MediaBinWidget::setItemProbing(const QString &path)
{
    if (auto *item = itemForPath(path)) item->setText(1, arenaTr("media.probing"));
}

void MediaBinWidget::setItemInfo(const QString &path, const MediaInfo &info)
{
    auto *item = itemForPath(path);
    if (!item) return;
    QString meta;
    if (info.exists && !info.error.isEmpty()) {
        meta = arenaTr("media.loadError", {{QStringLiteral("file"), QFileInfo(path).fileName()}});
    } else if (!info.exists) {
        meta = arenaTr("media.missing");
    } else if (info.type == MediaType::Video) {
        meta = arenaTr("media.video") + QStringLiteral(" ") +
               QStringLiteral("%1×%2").arg(info.width).arg(info.height);
        if (info.fps > 0.0)
            meta += QStringLiteral(" · %1 fps").arg(info.fps, 0, 'g', 3);
    } else if (info.type == MediaType::Audio) {
        meta = arenaTr("media.audio") +
               QStringLiteral(" · %1 kHz").arg(info.sampleRate / 1000);
    } else {
        meta = arenaTr("media.unknownType");
    }
    item->setText(1, meta);
}

void MediaBinWidget::replacePath(const QString &oldPath, const QString &newPath)
{
    const int row = m_paths.indexOf(oldPath);
    auto *item = itemForPath(oldPath);
    if (row < 0 || !item) return;
    m_paths[row] = newPath;
    item->setText(0, QFileInfo(newPath).fileName());
    item->setData(0, Qt::UserRole, newPath);
    item->setText(1, arenaTr("media.probing"));
    refreshBadges();
}

void MediaBinWidget::refreshBadges()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        const QString path = item->data(0, Qt::UserRole).toString();
        const bool missing = !QFileInfo::exists(path);
        item->setText(2, missing ? arenaTr("media.missing") : arenaTr("media.ok"));
        item->setForeground(2, missing ? QBrush(QColor(0xE5, 0x53, 0x4B))
                                       : QBrush(QColor(0x3D, 0xBE, 0x7B)));
    }
}

// --- InspectorWidget ---------------------------------------------------------

InspectorWidget::InspectorWidget(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(8);

    m_empty = new QWidget;
    auto *el = new QVBoxLayout(m_empty);
    el->addStretch(1);
    m_emptyTitle = new QLabel(arenaTr("panel.inspector.empty.title"));
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyBody = new QLabel(arenaTr("panel.inspector.empty.body"));
    m_emptyBody->setProperty("role", QStringLiteral("muted"));
    m_emptyBody->setWordWrap(true);
    m_emptyBody->setAlignment(Qt::AlignCenter);
    el->addWidget(m_emptyTitle);
    el->addWidget(m_emptyBody);
    el->addStretch(1);
    lay->addWidget(m_empty, 1);

    m_content = new QWidget;
    m_form = new QFormLayout(m_content);
    m_form->setContentsMargins(12, 12, 12, 12);
    lay->addWidget(m_content, 1);
    m_content->hide();
}

void InspectorWidget::retranslateUi()
{
    m_emptyTitle->setText(arenaTr("panel.inspector.empty.title"));
    m_emptyBody->setText(arenaTr("panel.inspector.empty.body"));
}

void InspectorWidget::setProjectName(const QString &name, double fps, int w, int h)
{
    m_empty->hide();
    m_content->show();
    while (m_form->rowCount() > 0) m_form->removeRow(0);
    m_form->addRow(arenaTr("common.name"), new QLabel(name));
    m_form->addRow(arenaTr("dlg.newProject.fps"), new QLabel(QStringLiteral("%1 fps").arg(fps)));
    m_form->addRow(arenaTr("dlg.newProject.resolution"), new QLabel(QStringLiteral("%1×%2").arg(w).arg(h)));
    auto *note = new QLabel(arenaTr("inspector.laterNote"));
    note->setProperty("role", QStringLiteral("muted"));
    note->setWordWrap(true);
    m_form->addRow(note);
}

void InspectorWidget::setClip(const ClipSelection &clip)
{
    m_empty->hide();
    m_content->show();
    m_clipId = clip.clipId;
    while (m_form->rowCount() > 0) m_form->removeRow(0);
    m_form->addRow(arenaTr("common.name"), new QLabel(clip.source));
    m_form->addRow(arenaTr("inspector.clip.sourceType"),
                   new QLabel(clip.hasVideo ? arenaTr("media.video") : arenaTr("media.audio")));
    m_form->addRow(arenaTr("inspector.clip.in"),
                   new QLabel(framesToTimecode(clip.in, clip.fps)));
    m_form->addRow(arenaTr("inspector.clip.out"),
                   new QLabel(framesToTimecode(clip.out, clip.fps)));
    m_form->addRow(arenaTr("inspector.clip.start"),
                   new QLabel(framesToTimecode(clip.offset, clip.fps)));
    m_form->addRow(arenaTr("inspector.clip.duration"),
                   new QLabel(framesToTimecode(clip.out - clip.in, clip.fps)));
    if (clip.missing) {
        auto *warn = new QLabel(arenaTr("media.missing"));
        warn->setProperty("role", QStringLiteral("danger"));
        m_form->addRow(warn);
    }

    // --- M3: effects editor -------------------------------------------------
    auto *header = new QLabel(arenaTr("inspector.effects.title"));
    QFont hf = header->font();
    hf.setBold(true);
    header->setFont(hf);
    m_form->addRow(header);

    QDoubleSpinBox *x = new QDoubleSpinBox; x->setRange(-100, 100); x->setSuffix(QStringLiteral(" %"));
    QDoubleSpinBox *y = new QDoubleSpinBox; y->setRange(-100, 100); y->setSuffix(QStringLiteral(" %"));
    QDoubleSpinBox *scale = new QDoubleSpinBox; scale->setRange(1, 400); scale->setSuffix(QStringLiteral(" %"));
    QDoubleSpinBox *rot = new QDoubleSpinBox; rot->setRange(-180, 180); rot->setSuffix(QStringLiteral(" \u00B0"));
    QDoubleSpinBox *opacity = new QDoubleSpinBox; opacity->setRange(0, 100); opacity->setSuffix(QStringLiteral(" %"));
    QDoubleSpinBox *exposure = new QDoubleSpinBox; exposure->setRange(-4, 4); exposure->setDecimals(2);
    QDoubleSpinBox *contrast = new QDoubleSpinBox; contrast->setRange(0, 2); contrast->setDecimals(2);
    QDoubleSpinBox *saturation = new QDoubleSpinBox; saturation->setRange(0, 2); saturation->setDecimals(2);
    QDoubleSpinBox *temperature = new QDoubleSpinBox; temperature->setRange(-100, 100);
    QDoubleSpinBox *blur = new QDoubleSpinBox; blur->setRange(0, 50); blur->setDecimals(0);
    QComboBox *transition = new QComboBox;
    transition->addItem(arenaTr("inspector.fx.none"));
    transition->addItem(arenaTr("inspector.fx.dissolve"));
    transition->addItem(arenaTr("inspector.fx.fade"));
    transition->addItem(arenaTr("inspector.fx.wipe"));
    transition->addItem(arenaTr("inspector.fx.push"));
    QDoubleSpinBox *tdur = new QDoubleSpinBox; tdur->setRange(0.1, 10); tdur->setSingleStep(0.1); tdur->setSuffix(QStringLiteral(" s"));

    {
        QDoubleSpinBox *spin[12] = {x, y, scale, rot, opacity, exposure, contrast,
                                    saturation, temperature, blur, nullptr, tdur};
        const QJsonObject tf = clip.effects.value(QStringLiteral("transform")).toObject();
        const QJsonObject col = clip.effects.value(QStringLiteral("color")).toObject();
        const QJsonObject op = clip.effects.value(QStringLiteral("opacity")).toObject();
        const QJsonObject bl = clip.effects.value(QStringLiteral("blur")).toObject();
        const QJsonObject tr = clip.effects.value(QStringLiteral("transition")).toObject();
        spin[0]->setValue(qBound(-100.0, tf.value(QStringLiteral("x")).toDouble(0.0) * 100.0, 100.0));
        spin[1]->setValue(qBound(-100.0, tf.value(QStringLiteral("y")).toDouble(0.0) * 100.0, 100.0));
        spin[2]->setValue(qBound(1.0, tf.value(QStringLiteral("scale")).toDouble(1.0) * 100.0, 400.0));
        spin[3]->setValue(qBound(-180.0, tf.value(QStringLiteral("rotation")).toDouble(0.0), 180.0));
        spin[4]->setValue(qBound(0.0, op.value(QStringLiteral("value")).toDouble(1.0) * 100.0, 100.0));
        spin[5]->setValue(qBound(-4.0, col.value(QStringLiteral("exposure")).toDouble(0.0), 4.0));
        spin[6]->setValue(qBound(0.0, col.value(QStringLiteral("contrast")).toDouble(1.0), 2.0));
        spin[7]->setValue(qBound(0.0, col.value(QStringLiteral("saturation")).toDouble(1.0), 2.0));
        spin[8]->setValue(qBound(-100.0, col.value(QStringLiteral("temperature")).toDouble(0.0), 100.0));
        spin[9]->setValue(bl.value(QStringLiteral("radius")).toInt(0));
        const QString type = tr.value(QStringLiteral("type")).toString();
        transition->setCurrentIndex(type == QStringLiteral("cross_dissolve") ? 1
                                    : type == QStringLiteral("fade_to_black") ? 2
                                    : type == QStringLiteral("wipe") ? 3
                                    : type == QStringLiteral("push") ? 4 : 0);
        const double durSec = tr.isEmpty() ? 1.0
                                           : double(tr.value(QStringLiteral("duration")).toInt(30)) / qMax(1.0, clip.fps);
        spin[11]->setValue(qBound(0.1, durSec, 10.0));
    }

    m_form->addRow(arenaTr("inspector.fx.x"), x);
    m_form->addRow(arenaTr("inspector.fx.y"), y);
    m_form->addRow(arenaTr("inspector.fx.scale"), scale);
    m_form->addRow(arenaTr("inspector.fx.rotation"), rot);
    m_form->addRow(arenaTr("inspector.fx.opacity"), opacity);
    m_form->addRow(arenaTr("inspector.fx.exposure"), exposure);
    m_form->addRow(arenaTr("inspector.fx.contrast"), contrast);
    m_form->addRow(arenaTr("inspector.fx.saturation"), saturation);
    m_form->addRow(arenaTr("inspector.fx.temperature"), temperature);
    m_form->addRow(arenaTr("inspector.fx.blur"), blur);
    m_form->addRow(arenaTr("inspector.fx.transition"), transition);
    m_form->addRow(arenaTr("inspector.fx.transitionDuration"), tdur);

    auto *apply = new QPushButton(arenaTr("inspector.fx.apply"));
    connect(apply, &QPushButton::clicked, this, [this, x, y, scale, rot, opacity,
                                                 exposure, contrast, saturation,
                                                 temperature, blur, transition,
                                                 tdur, fps = clip.fps]() {
        QJsonObject fx;
        QJsonObject tf;
        const double xF = x->value() / 100.0;
        const double yF = y->value() / 100.0;
        const double sc = scale->value() / 100.0;
        const double ro = rot->value();
        if (qAbs(xF) > 1e-9) tf[QStringLiteral("x")] = xF;
        if (qAbs(yF) > 1e-9) tf[QStringLiteral("y")] = yF;
        if (qAbs(sc - 1.0) > 1e-9) tf[QStringLiteral("scale")] = sc;
        if (qAbs(ro) > 1e-9) tf[QStringLiteral("rotation")] = ro;
        if (!tf.isEmpty()) fx[QStringLiteral("transform")] = tf;
        const double op = opacity->value() / 100.0;
        if (qAbs(op - 1.0) > 1e-9)
            fx[QStringLiteral("opacity")] = QJsonObject{{QStringLiteral("value"), op}};
        QJsonObject col;
        if (qAbs(exposure->value()) > 1e-9) col[QStringLiteral("exposure")] = exposure->value();
        if (qAbs(contrast->value() - 1.0) > 1e-9) col[QStringLiteral("contrast")] = contrast->value();
        if (qAbs(saturation->value() - 1.0) > 1e-9) col[QStringLiteral("saturation")] = saturation->value();
        if (qAbs(temperature->value()) > 1e-9) col[QStringLiteral("temperature")] = temperature->value();
        if (!col.isEmpty()) fx[QStringLiteral("color")] = col;
        const int br = int(blur->value());
        if (br > 0) fx[QStringLiteral("blur")] = QJsonObject{{QStringLiteral("radius"), br}};
        const int ti = transition->currentIndex();
        if (ti > 0) {
            static const QString types[4] = {QStringLiteral("cross_dissolve"),
                                             QStringLiteral("fade_to_black"),
                                             QStringLiteral("wipe"),
                                             QStringLiteral("push")};
            fx[QStringLiteral("transition")] =
                QJsonObject{{QStringLiteral("type"), types[ti - 1]},
                            {QStringLiteral("duration"),
                             qMax(1, int(tdur->value() * fps + 0.5))}};
        }
        emit effectsEdited(m_clipId, fx);
    });
    m_form->addRow(QString(), apply);
}

void InspectorWidget::clearSelection()
{
    m_content->hide();
    m_empty->show();
}

// --- SubtitlesWidget ---------------------------------------------------------

SubtitlesWidget::SubtitlesWidget(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({arenaTr("panel.subtitles.columnTrack"),
                             arenaTr("panel.subtitles.columnLang"),
                             arenaTr("panel.subtitles.columnActions")});
    m_tree->setRootIsDecorated(false);
    lay->addWidget(m_tree, 1);

    auto *row = new QHBoxLayout;
    m_add = new QPushButton(arenaTr("panel.subtitles.add"));
    m_add->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(m_add, &QPushButton::clicked, this, &SubtitlesWidget::addTrackDialog);
    row->addWidget(m_add);
    row->addStretch(1);
    lay->addLayout(row);

    QLabel *t = nullptr;
    QLabel *b = nullptr;
    m_empty = makeEmptyState(QStringLiteral("panel.subtitles.empty.title"),
                             QStringLiteral("panel.subtitles.empty.body"), &t, &b);
    m_emptyTitle = t;
    m_emptyBody = b;
    m_empty->show();
    lay->addWidget(m_empty, 1);
    m_tree->hide();
    m_add->hide();
}

void SubtitlesWidget::retranslateUi()
{
    m_tree->setHeaderLabels({arenaTr("panel.subtitles.columnTrack"),
                             arenaTr("panel.subtitles.columnLang"),
                             arenaTr("panel.subtitles.columnActions")});
    m_add->setText(arenaTr("panel.subtitles.add"));
    if (m_emptyTitle) m_emptyTitle->setText(arenaTr("panel.subtitles.empty.title"));
    if (m_emptyBody) m_emptyBody->setText(arenaTr("panel.subtitles.empty.body"));
}

void SubtitlesWidget::addTrack(const QString &name, const QString &bcp47)
{
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, name);
    item->setText(1, bcp47);
    item->setText(2, arenaTr("panel.subtitles.export"));
    m_tree->show();
    m_add->show();
    emit trackAdded(name, bcp47);
}

void SubtitlesWidget::clearTracks()
{
    m_tree->clear();
    m_tree->hide();
    m_add->hide();
    if (m_empty) m_empty->show();
}

void SubtitlesWidget::addTrackDialog()
{
    const QString f = QFileDialog::getOpenFileName(
        this, arenaTr("panel.subtitles.add"), QString(),
        arenaTr("subtitles.filter"));
    if (f.isEmpty()) return;
    QString lang = QStringLiteral("und");
    if (f.endsWith(QLatin1String("_en"), Qt::CaseInsensitive) || f.endsWith(QLatin1String(".en."), Qt::CaseInsensitive))
        lang = QStringLiteral("en");
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, QFileInfo(f).completeBaseName());
    item->setText(1, lang);
    item->setText(2, arenaTr("panel.subtitles.export"));
    m_tree->show();
    m_add->show();
    emit trackAdded(QFileInfo(f).completeBaseName(), lang);
}

} // namespace arena
