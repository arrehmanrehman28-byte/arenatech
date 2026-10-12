#pragma once
// Workspace panels: Program Monitor (with overlay surface), Media Bin,
// Inspector, Subtitle manager.
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFormLayout>

namespace arena {

// Draws safe-area / grid / thirds overlays on a letterboxed preview surface.
class PreviewSurface : public QWidget {
    Q_OBJECT
public:
    explicit PreviewSurface(QWidget *parent = nullptr);
    void setShowGuides(bool on);
    void setShowGrid(bool on);
    void setShowThirds(bool on);
    void setAspect(double w, double h);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool m_guides = true;
    bool m_grid = false;
    bool m_thirds = false;
    double m_aspect = 16.0 / 9.0;
};

class MediaBinWidget : public QWidget {
    Q_OBJECT
public:
    explicit MediaBinWidget(QWidget *parent = nullptr);
    void retranslateUi();
    void addPaths(const QStringList &paths);
    QStringList paths() const;

signals:
    void itemDoubleClicked(const QString &path);

private:
    void refreshBadges();
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
