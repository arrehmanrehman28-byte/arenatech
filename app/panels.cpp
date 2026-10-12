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
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({arenaTr("common.name"), QString()});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 90);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) {
        emit itemDoubleClicked(it->data(0, Qt::UserRole).toString());
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_tree->itemAt(pos);
        if (!item) return;
        QMenu menu;
        auto *relink = menu.addAction(arenaTr("panel.mediaBin.relink"));
        auto *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (chosen == relink) {
            const QString cur = item->data(0, Qt::UserRole).toString();
            const QString f = QFileDialog::getOpenFileName(this, arenaTr("dlg.open.title"), QFileInfo(cur).absolutePath());
            if (!f.isEmpty()) {
                const int row = m_tree->indexOfTopLevelItem(item);
                m_paths[row] = f;
                item->setText(0, QFileInfo(f).fileName());
                refreshBadges();
            }
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
    m_tree->setHeaderLabels({arenaTr("common.name"), arenaTr("media.type")});
    m_import->setText(arenaTr("panel.mediaBin.import"));
    if (m_emptyTitle) m_emptyTitle->setText(arenaTr("panel.mediaBin.empty.title"));
    if (m_emptyBody) m_emptyBody->setText(arenaTr("panel.mediaBin.empty.body"));
}

void MediaBinWidget::addPaths(const QStringList &paths)
{
    m_paths += paths;
    for (const QString &p : paths) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, QFileInfo(p).fileName());
        item->setData(0, Qt::UserRole, p);
    }
    m_empty->hide();
    m_tree->show();
    m_import->show();
    refreshBadges();
}

QStringList MediaBinWidget::paths() const { return m_paths; }

void MediaBinWidget::refreshBadges()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        const QString path = item->data(0, Qt::UserRole).toString();
        const bool missing = !QFileInfo::exists(path);
        item->setText(1, missing ? arenaTr("media.missing") : arenaTr("media.ok"));
        item->setForeground(1, missing ? QBrush(QColor(0xE5, 0x53, 0x4B)) : QBrush(QColor(0x3D, 0xBE, 0x7B)));
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
