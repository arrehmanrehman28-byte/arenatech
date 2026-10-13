#pragma once
// Main workspace: menu bar, command bar (Ctrl+K), dockable panels,
// status bar (timecode, fps, render cache, RAM, AI budget), autosave
// indicator in the title bar.
#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include <QToolButton>

#include "aifeatures.h"
#include "command.h"
#include "commandpalette.h"
#include "compositor.h"
#include "media.h"
#include "project.h"
#include "timeline.h"

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
class ExportDialog;
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
    ExportDialog *exportDialog() const { return m_exportDialog; }

    QString projectPath() const { return m_projectPath; }
    Project *project() const { return m_project ? m_project.get() : nullptr; }
    TimelineWidget *timeline() const { return m_timeline; }
    SubtitlesWidget *subtitles() const { return m_subtitles; }
    QString selectedClipId() const { return m_selectedClip; }
    void undo() { m_undoStack.undo(); }
    void redo() { m_undoStack.redo(); }

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
    // M4: opens (or raises) the export dialog for the current project.
    void openExportDialog();

    // M6.2: AI feature application. Every method is an explicit user
    // confirmation from the AI panel (accept button); each produces one or
    // more undoable commands. Text-only inputs; media never leaves.
    void applyAiTranslation(const QString &sourceTrackId, const QString &targetLang,
                            const QStringList &translatedTexts);
    void applyAiChapters(const QVector<ChapterSuggestion> &chapters);
    void applyAiTitle(const TitleResult &t);
    void applyAiCuts(const QVector<CutSuggestion> &cuts);
    // Ripple-remove [startFrame, endFrame) on the primary video track
    // (one undoable command). Public so the AI accept path and the headless
    // smoke test share the exact same code path.
    void applyCutRange(qint64 startFrame, qint64 endFrame);
    void togglePlayback();
    void seekToFrame(qint64 frame);
    // M3: re-render the monitor frame at the current playhead (synchronous;
    // also used to re-apply the playback resolution after a seek).
    void applyPlaybackResolution();
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
    // Index of the first video track (-1 when none); used by the M6.2
    // cut-apply path.
    int primaryVideoTrackIndex() const;

    // --- M1 media pipeline ---------------------------------------------------
    // Probes each imported file on a background thread.
    void probeMedia(const QString &path);
    void onMediaProbed(const QString &path, const MediaInfo &info);
    void maybeStartProxy(const QString &path, const MediaInfo &info);
    void onProxyReady(const QString &source, const QString &proxy, const QString &err);
    // The path the engine should use for `path` (proxy when available).
    QString playablePathFor(const QString &path) const;
    void selectClipAt(const QString &source, qint64 timelineFrame);
    void deleteSelectedClip();
    void onTrimRequested(const QList<ClipPatch> &patches);
    void onEffectsEdited(const QString &clipId, const QJsonObject &fx);
    void rippleTrim(bool rightSide); // Q / W at the playhead
    void addMarkerAt(qint64 frame);
    void removeMarkerAt(qint64 frame);
    void refreshTimeline();
    void generateWaveformThumbs(const QString &path);
    void onEngineFrame(const QImage &img, int frame);
    // M3: render the project frame at `frame`. `srcFrame` is the frame the
    // engine just decoded for the current source (reused by the fetcher);
    // falls back to the plain source frame when no clip has effects.
    QImage renderProjectFrame(qint64 frame, const QImage &srcFrame) const;
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
    QString m_selectedClip;                 // selected clip id (timeline)
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
    QLabel *m_dropped = nullptr;
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
    int m_playbackScale = 1; // 1 (Full), 2 (Half), 4 (Quarter)
    mutable Compositor::FrameCache m_frameCache;

    // M4: single open export dialog (nullptr when closed).
    ExportDialog *m_exportDialog = nullptr;

    // actions
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;
    QAction *m_aiEnableAct = nullptr;
    QActionGroup *m_themeGroup = nullptr;
    QActionGroup *m_languageGroup = nullptr;
    CommandPalette *m_palette = nullptr;
};

} // namespace arena
