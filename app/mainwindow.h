#pragma once
// Main workspace: menu bar, command bar (Ctrl+K), dockable panels,
// status bar (timecode, fps, render cache, RAM, AI budget), autosave
// indicator in the title bar.
#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include <QToolButton>

#include "command.h"
#include "commandpalette.h"
#include "media.h"
#include "project.h"

class QAction;
class QActionGroup;
class QComboBox;
class QMenu;
class QDockWidget;

namespace arena {

struct NewProjectResult;

class Themer;
class Settings;
class KiloClient;
class AIBudget;
class Autosave;
class MediaBinWidget;
class InspectorWidget;
class SubtitlesWidget;
class AiPanel;
class TimelineWidget;
class PreviewSurface;
class WaveformStrip;
class ThumbnailStrip;
class PlaybackEngine;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Themer *themer, QWidget *parent = nullptr);
    ~MainWindow() override;

    void retranslateUi();
    void applyLanguage();          // layout direction + full retranslate
    void applyThemeAndFont();      // live theme/accent/font-scale
    bool testWidgetReady() const { return m_ready; }

    QString projectPath() const { return m_projectPath; }
    Project *project() const { return m_project ? m_project.get() : nullptr; }

    // Project file operations (used by UI + tests).
    bool newProject(const NewProjectResult &r);
    bool openProjectFile(const QString &path);
    bool saveProjectFile(const QString &path = QString());
    void closeProject();
    QString currentTitle() const;

    // Programmatic media pipeline (used by the UI, the command palette, and
    // headless selftests): import, probe, and place clips.
    void importMedia(const QStringList &paths);
    void addClipToTimeline(const QString &path);
    void togglePlayback();
    void seekToFrame(qint64 frame);
    QImage previewImage() const;
    // Probe result for an imported file (default-constructed until probed).
    MediaInfo mediaInfo(const QString &path) const;

protected:
    void keyPressEvent(QKeyEvent *) override;

private:
    void buildActions();
    void buildCentral();
    void buildDocks();
    void buildStatusBar();
    void buildCommandBar();
    void openPalette();
    void askAiFromPalette(const QString &text);
    void enableAiWithNotice();
    void refreshUndoButtons();
    // Execute a command and push it, then mark the project dirty (autosave)
    // and refresh the undo/redo UI. Every timeline/project edit goes through
    // this so autosave + crash-recovery cover every change (spec M2).
    void commitCommand(std::unique_ptr<Command> cmd);
    void noteHistoryChanged(); // after undo/redo
    void refreshProjectViews();
    void checkRecoveryFor(const QString &path);
    void tickSecond();
    void showWelcomeIfNeeded();
    void registerCommands();

    // --- M1 media pipeline ---------------------------------------------------
    // Probes each imported file on a background thread.
    void probeMedia(const QString &path);
    void onMediaProbed(const QString &path, const MediaInfo &info);
    void maybeStartProxy(const QString &path, const MediaInfo &info);
    void onProxyReady(const QString &source, const QString &proxy, const QString &err);
    // The path the engine should use for `path` (proxy when available).
    QString playablePathFor(const QString &path) const;
    void selectClipAt(const QString &source, qint64 timelineFrame);
    void refreshTimeline();
    void generateWaveformThumbs(const QString &path);
    void onEngineFrame(const QImage &img, int frame);
    void onEnginePosition(double sec);
    void onEngineEnded();
    void onPlayStateChanged(bool playing);
    void relinkMedia(const QString &oldPath);
    void updateTransportButtons();

    Themer *m_themer;
    Settings *m_settings;
    KiloClient *m_kilo;
    Autosave *m_autosave;
    PlaybackEngine *m_engine = nullptr;

    std::unique_ptr<Project> m_project;
    QString m_projectPath;
    UndoStack m_undoStack;
    bool m_ready = false;

    qint64 m_playhead = 0;
    int m_transportSpeed = 0;
    QTimer m_transportTimer;
    QString m_monitorNote;

    // M1 state.
    QHash<QString, MediaInfo> m_mediaInfo;  // per imported media
    QHash<QString, QString> m_proxies;      // source -> proxy path
    QHash<QString, QVector<Peak>> m_wavePeaks; // source -> waveform (for clips)
    QString m_currentSource;                // clip shown in the monitor
    QString m_engineSource;                 // path the engine is open on
    QToolButton *m_playBtn = nullptr;

    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_aiMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    QList<QDockWidget *> m_docks;
    QList<QPair<QDockWidget *, QAction *>> m_dockToggles;

    // command bar
    QToolButton *m_paletteButton = nullptr;
    // status bar
    QLabel *m_timecode = nullptr;
    QLabel *m_fps = nullptr;
    QLabel *m_cache = nullptr;
    QLabel *m_mem = nullptr;
    QLabel *m_aiBudget = nullptr;
    QTimer m_secondTimer;

    // panels
    MediaBinWidget *m_mediaBin = nullptr;
    InspectorWidget *m_inspector = nullptr;
    SubtitlesWidget *m_subtitles = nullptr;
    AiPanel *m_aiPanel = nullptr;
    TimelineWidget *m_timeline = nullptr;
    PreviewSurface *m_monitor = nullptr;
    WaveformStrip *m_waveform = nullptr;
    ThumbnailStrip *m_thumbs = nullptr;
    QComboBox *m_playbackRes = nullptr;

    // actions
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;
    QAction *m_aiEnableAct = nullptr;
    QActionGroup *m_themeGroup = nullptr;
    QActionGroup *m_languageGroup = nullptr;
    CommandPalette *m_palette = nullptr;
};

} // namespace arena
