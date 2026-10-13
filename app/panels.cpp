#include "panels.h"
#include "dialogs.h"
#include "i18n.h"
#include "subtitles.h"
#include "util.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QUuid>
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

// --- SubtitlesWidget (M5.3: full CRUD + import/export + burn-in) ----------
namespace {
bool validTimecode(const QString &tc)
{
    return tc.split(QLatin1Char(':'), Qt::SkipEmptyParts).size() == 4;
}
} // namespace

SubtitlesWidget::SubtitlesWidget(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(8);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({arenaTr("panel.subtitles.columnTrack"),
                             arenaTr("panel.subtitles.columnLang"),
                             arenaTr("panel.subtitles.burnIn")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int col) {
        if (col != 2) return;
        const int i = it->data(0, Qt::UserRole).toInt();
        if (m_project && i >= 0 && i < m_project->subtitleTracks.size()) {
            m_project->subtitleTracks[i].burnIn =
                (it->checkState(2) == Qt::Checked);
            emit subtitlesChanged();
        }
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { rebuildCues(); });
    lay->addWidget(m_tree, 1);

    m_cues = new QTableWidget(0, 3);
    m_cues->verticalHeader()->setVisible(false);
    m_cues->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_cues->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_cues->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_cues->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_cues->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_cues->setMaximumHeight(180);
    connect(m_cues, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem *it) { editCue(m_cues->row(it)); });
    lay->addWidget(m_cues, 1);

    m_hint = new QLabel;
    lay->addWidget(m_hint);

    auto *row = new QHBoxLayout;
    m_addTrack = new QPushButton(arenaTr("panel.subtitles.add"));
    m_import = new QPushButton(arenaTr("panel.subtitles.import"));
    m_deleteTrack = new QPushButton(arenaTr("panel.subtitles.deleteTrack"));
    m_addCue = new QPushButton(arenaTr("panel.subtitles.addCue"));
    m_deleteCue = new QPushButton(arenaTr("panel.subtitles.deleteCue"));
    m_export = new QPushButton(arenaTr("panel.subtitles.export"));
    for (QPushButton *b : {m_addTrack, m_import, m_deleteTrack, m_addCue, m_deleteCue, m_export})
        b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(m_addTrack, &QPushButton::clicked, this, &SubtitlesWidget::addTrackDialog);
    connect(m_import, &QPushButton::clicked, this, &SubtitlesWidget::importTrack);
    connect(m_deleteTrack, &QPushButton::clicked, this, &SubtitlesWidget::deleteCurrentTrack);
    connect(m_addCue, &QPushButton::clicked, this, &SubtitlesWidget::addCue);
    connect(m_deleteCue, &QPushButton::clicked, this, &SubtitlesWidget::deleteCue);
    connect(m_export, &QPushButton::clicked, this, &SubtitlesWidget::exportCurrentTrack);
    row->addWidget(m_addTrack);
    row->addWidget(m_import);
    row->addWidget(m_deleteTrack);
    row->addStretch(1);
    row->addWidget(m_addCue);
    row->addWidget(m_deleteCue);
    row->addWidget(m_export);
    lay->addLayout(row);

    QLabel *t = nullptr;
    QLabel *b = nullptr;
    m_empty = makeEmptyState(QStringLiteral("panel.subtitles.empty.title"),
                             QStringLiteral("panel.subtitles.empty.body"), &t, &b);
    m_emptyTitle = t;
    m_emptyBody = b;
    lay->addWidget(m_empty, 1);

    setProject(nullptr);
}

void SubtitlesWidget::retranslateUi()
{
    m_tree->setHeaderLabels({arenaTr("panel.subtitles.columnTrack"),
                             arenaTr("panel.subtitles.columnLang"),
                             arenaTr("panel.subtitles.burnIn")});
    m_cues->setHorizontalHeaderLabels({arenaTr("panel.subtitles.columnStart"),
                                       arenaTr("panel.subtitles.columnEnd"),
                                       arenaTr("panel.subtitles.columnText")});
    m_addTrack->setText(arenaTr("panel.subtitles.add"));
    m_import->setText(arenaTr("panel.subtitles.import"));
    m_deleteTrack->setText(arenaTr("panel.subtitles.deleteTrack"));
    m_addCue->setText(arenaTr("panel.subtitles.addCue"));
    m_deleteCue->setText(arenaTr("panel.subtitles.deleteCue"));
    m_export->setText(arenaTr("panel.subtitles.export"));
    if (m_emptyTitle) m_emptyTitle->setText(arenaTr("panel.subtitles.empty.title"));
    if (m_emptyBody) m_emptyBody->setText(arenaTr("panel.subtitles.empty.body"));
    rebuildCues(); // refresh hint / warning tooltips
}

void SubtitlesWidget::setProject(Project *project)
{
    m_project = project;
    m_fps = project ? project->fps : 30.0;
    rebuildTracks();
    rebuildCues();
}

int SubtitlesWidget::currentTrackIndex() const
{
    QTreeWidgetItem *it = m_tree->currentItem();
    return it ? it->data(0, Qt::UserRole).toInt() : -1;
}

SubtitleTrack *SubtitlesWidget::currentTrack()
{
    const int i = currentTrackIndex();
    if (!m_project || i < 0 || i >= m_project->subtitleTracks.size()) return nullptr;
    return &m_project->subtitleTracks[i];
}

void SubtitlesWidget::rebuildTracks()
{
    m_tree->blockSignals(true);
    m_tree->clear();
    if (m_project) {
        for (int i = 0; i < m_project->subtitleTracks.size(); ++i) {
            const SubtitleTrack &st = m_project->subtitleTracks.at(i);
            auto *item = new QTreeWidgetItem(m_tree);
            item->setText(0, st.name);
            item->setText(1, st.language);
            item->setCheckState(2, st.burnIn ? Qt::Checked : Qt::Unchecked);
            item->setData(0, Qt::UserRole, i);
        }
    }
    m_tree->blockSignals(false);
    const bool has = m_project != nullptr && !m_project->subtitleTracks.isEmpty();
    m_tree->setVisible(has);
    m_cues->setVisible(has);
    if (m_empty) m_empty->setVisible(!has);
    const bool sel = m_addTrack->isEnabled();
    m_addTrack->setEnabled(m_project != nullptr);
    m_import->setEnabled(m_project != nullptr);
    m_deleteTrack->setEnabled(has);
    m_export->setEnabled(has);
    m_addCue->setEnabled(has);
    m_deleteCue->setEnabled(has);
    (void)sel;
    if (has) m_tree->setCurrentItem(m_tree->topLevelItem(0));
}

void SubtitlesWidget::rebuildCues()
{
    m_cues->blockSignals(true);
    m_cues->setRowCount(0);
    SubtitleTrack *st = currentTrack();
    if (st) {
        m_cues->setRowCount(st->cues.size());
        for (int i = 0; i < st->cues.size(); ++i) {
            const TimelineCue &c = st->cues.at(i);
            const double s = Subtitles::frameToSec(c.startFrame, m_fps);
            const double e = Subtitles::frameToSec(c.endFrame, m_fps);
            const Subtitles::CueWarning w = Subtitles::checkCue(c.text, s, e);
            auto *start = new QTableWidgetItem(framesToTimecode(c.startFrame, m_fps));
            auto *end = new QTableWidgetItem(framesToTimecode(c.endFrame, m_fps));
            auto *text = new QTableWidgetItem(c.text);
            start->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            end->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            text->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (w.any()) {
                // Flagged cues: underlined + tinted, with the reason in the
                // tooltip (spec M5.3: reading-speed warnings).
                QFont f = text->font();
                f.setUnderline(true);
                text->setFont(f);
                text->setForeground(QColor(0xC0, 0x39, 0x2B));
                QStringList msgs;
                if (w.tooLong)
                    msgs << arenaTr("panel.subtitles.warnLong",
                                     {{QStringLiteral("n"), w.visibleChars}});
                if (w.tooFast)
                    msgs << arenaTr("panel.subtitles.warnFast",
                                     {{QStringLiteral("cps"),
                                       QString::number(w.cps, 'f', 1)}});
                const QString tip = msgs.join(QStringLiteral(" · "));
                start->setToolTip(tip);
                end->setToolTip(tip);
                text->setToolTip(tip);
            }
            m_cues->setItem(i, 0, start);
            m_cues->setItem(i, 1, end);
            m_cues->setItem(i, 2, text);
        }
    }
    m_cues->blockSignals(false);
    m_hint->setText(
        st ? arenaTr("panel.subtitles.trackCueCount",
                     {{QStringLiteral("name"), st->name},
                      {QStringLiteral("n"), st->cues.size()}})
           : arenaTr("panel.subtitles.noTrackSelected"));
}

void SubtitlesWidget::addTrackDialog()
{
    if (!m_project) return;
    QDialog dlg(this);
    dlg.setWindowTitle(arenaTr("dialog.subtitleTrack.title"));
    auto *lay = new QFormLayout(&dlg);
    auto *name = new QLineEdit;
    auto *lang = new QComboBox;
    lang->setEditable(true);
    for (const QString &l : subtitleLanguages()) lang->addItem(l);
    lang->setCurrentText(QStringLiteral("en"));
    auto *burn = new QCheckBox(arenaTr("dialog.subtitleTrack.burnIn"));
    lay->addRow(arenaTr("dialog.subtitleTrack.name"), name);
    lay->addRow(arenaTr("dialog.subtitleTrack.language"), lang);
    lay->addRow(QString(), burn);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addRow(bb);
    if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) return;
    SubtitleTrack st;
    st.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    st.name = name->text().trimmed();
    st.language = lang->currentText().trimmed();
    st.burnIn = burn->isChecked();
    m_project->subtitleTracks.append(st);
    rebuildTracks();
    emit subtitlesChanged();
}

void SubtitlesWidget::importTrack()
{
    if (!m_project) return;
    const QString f = QFileDialog::getOpenFileName(
        this, arenaTr("panel.subtitles.import"), QString(),
        arenaTr("subtitles.filter"));
    if (f.isEmpty()) return;
    QFile file(f);
    if (!file.open(QIODevice::ReadOnly)) {
        ErrorDialog(arenaTr("error.cannotOpenFile"), f, this).exec();
        return;
    }
    QString err;
    QVector<SubtitleCue> cues;
    if (!Subtitles::parse(QString::fromUtf8(file.readAll()),
                          QFileInfo(f).fileName(), &cues, &err)) {
        ErrorDialog(err, f, this).exec();
        return;
    }
    bool ok = false;
    const QString lang = QInputDialog::getItem(
        this, arenaTr("dialog.subtitleTrack.language"),
        arenaTr("dialog.subtitleTrack.language"), subtitleLanguages(), 0, true, &ok);
    if (!ok) return;
    SubtitleTrack st;
    st.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    st.name = QFileInfo(f).completeBaseName();
    st.language = lang;
    for (const SubtitleCue &c : cues) {
        TimelineCue tc;
        tc.startFrame = Subtitles::secToFrame(c.startSec, m_fps);
        tc.endFrame = Subtitles::secToFrame(c.endSec, m_fps);
        tc.text = c.text;
        st.cues.append(tc);
    }
    m_project->subtitleTracks.append(st);
    rebuildTracks();
    emit subtitlesChanged();
}

void SubtitlesWidget::exportCurrentTrack()
{
    SubtitleTrack *st = currentTrack();
    if (!st) return;
    const QString f = QFileDialog::getSaveFileName(
        this, arenaTr("panel.subtitles.export"),
        st->name + QStringLiteral(".srt"), arenaTr("subtitles.filter"));
    if (f.isEmpty()) return;
    QVector<SubtitleCue> cues;
    for (const TimelineCue &c : st->cues) {
        SubtitleCue sc;
        sc.startSec = Subtitles::frameToSec(c.startFrame, m_fps);
        sc.endSec = Subtitles::frameToSec(c.endFrame, m_fps);
        sc.text = c.text;
        cues.append(sc);
    }
    const QString encoded = Subtitles::encode(cues, f);
    QFile out(f);
    if (!out.open(QIODevice::WriteOnly)) {
        ErrorDialog(arenaTr("error.cannotOpenFile"), f, this).exec();
        return;
    }
    out.write(encoded.toUtf8());
}

void SubtitlesWidget::deleteCurrentTrack()
{
    SubtitleTrack *st = currentTrack();
    if (!st) return;
    const int i = currentTrackIndex();
    const auto ans = QMessageBox::question(
        this, arenaTr("panel.subtitles.deleteTrack"),
        arenaTr("panel.subtitles.deleteTrackConfirm",
                {{QStringLiteral("name"), st->name}}),
        QMessageBox::Yes | QMessageBox::No);
    if (ans != QMessageBox::Yes) return;
    m_project->subtitleTracks.removeAt(i);
    rebuildTracks();
    emit subtitlesChanged();
}

void SubtitlesWidget::addCue()
{
    SubtitleTrack *st = currentTrack();
    if (!st) return;
    TimelineCue tc;
    tc.startFrame = st->cues.isEmpty()
                        ? 0
                        : st->cues.last().endFrame;
    tc.endFrame = tc.startFrame + qMax(1, int(m_fps));
    tc.text = arenaTr("panel.subtitles.defaultCueText");
    st->cues.append(tc);
    rebuildCues();
    if (m_cues->rowCount() > 0)
        m_cues->selectRow(m_cues->rowCount() - 1);
    emit subtitlesChanged();
}

void SubtitlesWidget::deleteCue()
{
    SubtitleTrack *st = currentTrack();
    if (!st) return;
    const int row = m_cues->currentRow();
    if (row < 0 || row >= st->cues.size()) return;
    st->cues.removeAt(row);
    rebuildCues();
    emit subtitlesChanged();
}

void SubtitlesWidget::editCue(int row)
{
    SubtitleTrack *st = currentTrack();
    if (!st || row < 0 || row >= st->cues.size()) return;
    const TimelineCue c = st->cues.at(row);
    QDialog dlg(this);
    dlg.setWindowTitle(arenaTr("dialog.subtitleCue.title"));
    auto *lay = new QFormLayout(&dlg);
    auto *startEdit = new QLineEdit(framesToTimecode(c.startFrame, m_fps));
    auto *endEdit = new QLineEdit(framesToTimecode(c.endFrame, m_fps));
    auto *textEdit = new QPlainTextEdit(c.text);
    textEdit->setFixedHeight(72);
    auto *warn = new QLabel;
    warn->setStyleSheet(QStringLiteral("color: #C0392B;"));
    lay->addRow(arenaTr("dialog.subtitleCue.start"), startEdit);
    lay->addRow(arenaTr("dialog.subtitleCue.end"), endEdit);
    lay->addRow(arenaTr("dialog.subtitleCue.text"), textEdit);
    lay->addRow(warn);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addRow(bb);

    auto updateWarn = [this, warn, startEdit, endEdit, textEdit]() {
        const double s =
            Subtitles::frameToSec(timecodeToFrames(startEdit->text(), m_fps), m_fps);
        double e = Subtitles::frameToSec(timecodeToFrames(endEdit->text(), m_fps), m_fps);
        if (e <= s) e = s + 1.0 / m_fps;
        const Subtitles::CueWarning w =
            Subtitles::checkCue(textEdit->toPlainText(), s, e);
        QStringList msgs;
        if (w.tooLong)
            msgs << arenaTr("panel.subtitles.warnLong", {{QStringLiteral("n"), w.visibleChars}});
        if (w.tooFast)
            msgs << arenaTr("panel.subtitles.warnFast",
                            {{QStringLiteral("cps"), QString::number(w.cps, 'f', 1)}});
        warn->setText(msgs.join(QStringLiteral(" · ")));
        warn->setVisible(!msgs.isEmpty());
    };
    QObject::connect(startEdit, &QLineEdit::editingFinished, this, updateWarn);
    QObject::connect(endEdit, &QLineEdit::editingFinished, this, updateWarn);
    QObject::connect(textEdit, &QPlainTextEdit::textChanged, this, updateWarn);
    updateWarn();

    if (dlg.exec() != QDialog::Accepted) return;
    if (!validTimecode(startEdit->text()) || !validTimecode(endEdit->text())) {
        ErrorDialog(arenaTr("dialog.subtitleCue.invalidTimecode"), QString(), this).exec();
        return;
    }
    TimelineCue &tc = st->cues[row];
    tc.startFrame = timecodeToFrames(startEdit->text(), m_fps);
    tc.endFrame = timecodeToFrames(endEdit->text(), m_fps);
    tc.text = textEdit->toPlainText();
    if (tc.endFrame <= tc.startFrame) tc.endFrame = tc.startFrame + 1;
    rebuildCues();
    emit subtitlesChanged();
}

} // namespace arena
