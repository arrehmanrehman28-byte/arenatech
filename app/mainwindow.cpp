#include "mainwindow.h"
#include "panels.h"
#include "playback.h"
#include "timeline.h"
#include "aipanel.h"
#include "dialogs.h"
#include "themer.h"
#include "settings.h"
#include "i18n.h"
#include "autosave.h"
#include "util.h"
#include "kilogateway.h"
#include "exportdialog.h"
#include "exportqueue.h"
#include "exporter.h"
#include "media.h"
#include "subtitles.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QCryptographicHash>
#include <QKeyEvent>
#include <QUuid>
#include <QWidget>

#include <algorithm>
#include <thread>

namespace arena {

MainWindow::MainWindow(Themer *themer, QWidget *parent)
    : QMainWindow(parent), m_themer(themer), m_settings(&Settings::instance()),
      m_kilo(new KiloClient(*m_settings, this)),
      m_autosave(new Autosave(this)),
      m_engine(new PlaybackEngine(this))
{
    connect(m_engine, &PlaybackEngine::frameReady, this, &MainWindow::onEngineFrame);
    connect(m_engine, &PlaybackEngine::positionChanged, this, &MainWindow::onEnginePosition);
    connect(m_engine, &PlaybackEngine::ended, this, &MainWindow::onEngineEnded);
    connect(m_engine, &PlaybackEngine::playStateChanged, this, &MainWindow::onPlayStateChanged);
    m_undoStack.onChanged = [this] { noteHistoryChanged(); };
    // M3: render cache size (MB), default 512.
    const QVariant cacheMb = m_settings->raw(QStringLiteral("render/cacheMB"));
    m_frameCache.setMaxBytes(
        qint64(cacheMb.isValid() ? cacheMb.toInt() : 512) * 1024 * 1024);
    setWindowTitle(QStringLiteral("Arena Edit"));
    setMinimumSize(1100, 680);
    resize(1440, 860);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks
                   | QMainWindow::AllowNestedDocks);
    setTabShape(QTabWidget::Rounded);

    m_autosave->setInterval(m_settings->autosaveSeconds());
    m_autosave->setSnapshotFn([this](const QString &path) {
        if (!m_project) return false;
        return m_project->saveFile(path, nullptr);
    });
    connect(m_autosave, &Autosave::projectSaved, this, [this] {
        refreshProjectViews();
        m_secondTimer.start(1000);
    });

    buildActions();
    buildCentral();
    buildDocks();
    buildStatusBar();
    buildCommandBar();
    registerCommands();

    m_secondTimer.setInterval(1000);
    connect(&m_secondTimer, &QTimer::timeout, this, &MainWindow::tickSecond);
    m_secondTimer.start(1000);

    connect(&I18n::instance(), &I18n::localeChanged, this, &MainWindow::applyLanguage);
    connect(m_kilo, &KiloClient::modelsReady, this, [this](const QList<KiloModel> &) {
        m_settings->setRaw(QStringLiteral("ai/modelsCached"), m_kilo->models().size());
    });

    retranslateUi();
    applyLanguage();
    refreshUndoButtons();
    m_ready = true;
    showWelcomeIfNeeded();
}

MainWindow::~MainWindow() = default;

// --- construction helpers ----------------------------------------------------

void MainWindow::buildActions()
{
    m_undoAct = new QAction(this);
    m_redoAct = new QAction(this);
    m_undoAct->setShortcut(QKeySequence::Undo);
    m_redoAct->setShortcut(QKeySequence::Redo);
    connect(m_undoAct, &QAction::triggered, this, [this] { m_undoStack.undo(); });
    connect(m_redoAct, &QAction::triggered, this, [this] { m_undoStack.redo(); });

    auto *newAct = new QAction(this);
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, [this] {
        NewProjectDialog d(this);
        if (d.exec() == QDialog::Accepted) newProject(d.result());
    });
    auto *openAct = new QAction(this);
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, arenaTr("dlg.open.title"),
                                                       QString(), arenaTr("dlg.projectFilter"));
        if (!f.isEmpty()) openProjectFile(f);
    });
    auto *saveAct = new QAction(this);
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, [this] { saveProjectFile(); });
    auto *saveAsAct = new QAction(this);
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, arenaTr("dlg.save.title"),
                                                       m_projectPath, arenaTr("dlg.projectFilter"));
        if (!f.isEmpty()) saveProjectFile(f);
    });
    auto *closeAct = new QAction(this);
    closeAct->setShortcut(QKeySequence::Close);
    connect(closeAct, &QAction::triggered, this, &MainWindow::closeProject);
    auto *exitAct = new QAction(this);
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);
    auto *exportAct = new QAction(this);
    exportAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    connect(exportAct, &QAction::triggered, this, &MainWindow::openExportDialog);

    m_fileMenu = menuBar()->addMenu(arenaTr("menu.file"));
    m_fileMenu->addAction(newAct);
    m_fileMenu->addAction(openAct);
    m_fileMenu->addAction(saveAct);
    m_fileMenu->addAction(saveAsAct);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(closeAct);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(exportAct);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(exitAct);

    m_editMenu = menuBar()->addMenu(arenaTr("menu.edit"));
    m_editMenu->addAction(m_undoAct);
    m_editMenu->addAction(m_redoAct);
    m_editMenu->addSeparator();
    auto *prefsAct = new QAction(this);
    connect(prefsAct, &QAction::triggered, this, [this] {
        SettingsDialog dlg(m_kilo->models(), this);
        connect(&dlg, &SettingsDialog::themeChanged, this, [this] { applyThemeAndFont(); });
        connect(&dlg, &SettingsDialog::fontScaleChanged, this, [this] { applyThemeAndFont(); });
        connect(&dlg, &SettingsDialog::languageChanged, this, [this](const QString &bcp47) {
            I18n::instance().setLocale(bcp47.isEmpty() ? QStringLiteral("en") : bcp47);
        });
        dlg.exec();
    });
    m_editMenu->addAction(prefsAct);

    m_viewMenu = menuBar()->addMenu(arenaTr("menu.view"));
    // Dock toggles are appended in buildDocks (docks must exist first).

    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    QMenu *themeMenu = m_viewMenu->addMenu(arenaTr("view.theme"));
    for (const QString &t : {QStringLiteral("dark"), QStringLiteral("light"),
                             QStringLiteral("high-contrast")}) {
        auto *act = new QAction(this);
        act->setCheckable(true);
        act->setChecked(t == m_settings->theme());
        m_themeGroup->addAction(act);
        themeMenu->addAction(act);
        connect(act, &QAction::triggered, this, [this, t] {
            m_settings->setTheme(t);
            applyThemeAndFont();
        });
    }
    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);
    QMenu *langMenu = m_viewMenu->addMenu(arenaTr("view.language"));
    for (const auto &loc : I18n::instance().locales()) {
        auto *act = new QAction(this);
        act->setCheckable(true);
        act->setChecked(loc.bcp47 == I18n::instance().currentLocale());
        m_languageGroup->addAction(act);
        langMenu->addAction(act);
        connect(act, &QAction::triggered, this, [this, loc] {
            I18n::instance().setLocale(loc.bcp47);
        });
    }

    m_aiMenu = menuBar()->addMenu(arenaTr("menu.ai"));
    m_aiEnableAct = new QAction(this);
    m_aiEnableAct->setCheckable(true);
    m_aiEnableAct->setChecked(m_settings->aiEnabled());
    connect(m_aiEnableAct, &QAction::toggled, this, [this](bool on) {
        if (on) enableAiWithNotice();
        else {
            m_settings->setAiEnabled(false);
            m_aiPanel->setEnabledUi(false);
        }
    });
    m_aiMenu->addAction(m_aiEnableAct);
    m_aiMenu->addAction(arenaTr("ai.settings"), this, [this] {
        SettingsDialog dlg(m_kilo->models(), this);
        dlg.exec();
    });

    m_helpMenu = menuBar()->addMenu(arenaTr("menu.help"));
    m_helpMenu->addAction(arenaTr("help.whatsNew"), this, [this] {
        statusBar()->showMessage(
            arenaTr("welcome.whatsNew.body", {{QStringLiteral("version"), qApp->applicationVersion()}}), 6000);
    });
    m_helpMenu->addAction(arenaTr("help.about"), this, [this] {
        AboutDialog dlg(this);
        dlg.exec();
    });
}

void MainWindow::buildCentral()
{
    auto *split = new QSplitter(Qt::Vertical, this);

    // Program monitor: transport + preview surface.
    auto *monitorBox = new QWidget;
    auto *monLay = new QVBoxLayout(monitorBox);
    monLay->setContentsMargins(8, 8, 8, 4);
    monLay->setSpacing(6);
    auto *transport = new QHBoxLayout;
    auto btn = [&](const QString &key, const QString &glyph) {
        auto *b = new QToolButton;
        b->setText(glyph);
        b->setToolTip(arenaTr(key));
        return b;
    };
    auto *back = btn(QStringLiteral("transport.back"), QStringLiteral("◀◀"));
    auto *stepBack = btn(QStringLiteral("transport.stepBack"), QStringLiteral("◀"));
    auto *play = btn(QStringLiteral("transport.play"), QStringLiteral("▶"));
    auto *stepFwd = btn(QStringLiteral("transport.stepFwd"), QStringLiteral("▶"));
    auto *fwd = btn(QStringLiteral("transport.fwd"), QStringLiteral("▶▶"));
    m_playbackRes = new QComboBox;
    m_playbackRes->addItems({QStringLiteral("Full"), QStringLiteral("1/2"), QStringLiteral("1/4")});
    m_playbackRes->setToolTip(arenaTr("panel.monitor.playback"));
    m_playbackRes->setCurrentIndex(0);
    connect(m_playbackRes, &QComboBox::currentIndexChanged, this,
            [this](int) { applyPlaybackResolution(); });
    auto *guides = new QCheckBox(arenaTr("panel.monitor.guides"));
    guides->setChecked(true);
    auto *grid = new QCheckBox(arenaTr("panel.monitor.grid"));
    auto *thirds = new QCheckBox(arenaTr("panel.monitor.thirds"));
    m_monitor = new PreviewSurface;
    connect(guides, &QCheckBox::toggled, m_monitor, &PreviewSurface::setShowGuides);
    connect(grid, &QCheckBox::toggled, m_monitor, &PreviewSurface::setShowGrid);
    connect(thirds, &QCheckBox::toggled, m_monitor, &PreviewSurface::setShowThirds);

    // M1 transport: real decode + A/V-synced playback (see PlaybackEngine).
    m_playBtn = play;
    connect(play, &QToolButton::clicked, this, &MainWindow::togglePlayback);
    connect(back, &QToolButton::clicked, this, [this] {
        if (m_project) seekToFrame(0);
    });
    connect(fwd, &QToolButton::clicked, this, [this] {
        if (!m_project) return;
        qint64 total = 0;
        if (!m_project->tracks.isEmpty())
            for (const Clip &c : m_project->tracks.first().clips) total = c.offset + c.duration();
        seekToFrame(qMax<qint64>(0, total - 1));
    });
    connect(stepBack, &QToolButton::clicked, this, [this] {
        if (m_engine && m_engine->isOpen())
            m_engine->stepFrame(-1);
        else
            seekToFrame(m_playhead - 1);
    });
    connect(stepFwd, &QToolButton::clicked, this, [this] {
        if (m_engine && m_engine->isOpen())
            m_engine->stepFrame(1);
        else
            seekToFrame(m_playhead + 1);
    });

    transport->addWidget(back);
    transport->addWidget(stepBack);
    transport->addWidget(play);
    transport->addWidget(stepFwd);
    transport->addWidget(fwd);
    transport->addSpacing(12);
    transport->addWidget(m_playbackRes);
    transport->addStretch(1);
    transport->addWidget(guides);
    transport->addWidget(grid);
    transport->addWidget(thirds);
    monLay->addLayout(transport);
    monLay->addWidget(m_monitor, 1);
    m_waveform = new WaveformStrip;
    m_thumbs = new ThumbnailStrip;
    monLay->addWidget(m_waveform);
    monLay->addWidget(m_thumbs);

    m_timeline = new TimelineWidget;
    connect(m_timeline, &TimelineWidget::zoomChanged, this, [](double) {});

    split->addWidget(monitorBox);
    split->addWidget(m_timeline);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    setCentralWidget(split);
}

void MainWindow::buildDocks()
{
    m_mediaBin = new MediaBinWidget;
    auto *mediaDock = new QDockWidget;
    mediaDock->setObjectName(QStringLiteral("MediaBinDock"));
    mediaDock->setWidget(m_mediaBin);
    mediaDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, mediaDock);
    connect(m_mediaBin, &MediaBinWidget::addToTimelineRequested, this,
            &MainWindow::addClipToTimeline);
    connect(m_mediaBin, &MediaBinWidget::relinkRequested, this, &MainWindow::relinkMedia);
    connect(m_mediaBin, &MediaBinWidget::mediaImported, this, &MainWindow::importMedia);
    connect(m_timeline, &TimelineWidget::seekRequested, this, &MainWindow::seekToFrame);
    connect(m_timeline, &TimelineWidget::clipClicked, this, [this](qint64 frame) {
        if (!m_project) return;
        for (const Track &t : m_project->tracks) {
            if (t.type != TrackType::Video) continue;
            for (const Clip &c : t.clips) {
                if (frame >= c.offset && frame < c.offset + c.duration()) {
                    m_selectedClip = c.id;
                    selectClipAt(c.sourceRef, frame);
                    return;
                }
            }
        }
    });
    connect(m_timeline, &TimelineWidget::clipDeselected, this, [this] {
        m_selectedClip.clear();
        refreshTimeline();
    });
    connect(m_timeline, &TimelineWidget::trimRequested, this, &MainWindow::onTrimRequested);
    connect(m_timeline, &TimelineWidget::markerAddRequested, this, &MainWindow::addMarkerAt);
    connect(m_timeline, &TimelineWidget::markerRemoveRequested, this,
            &MainWindow::removeMarkerAt);

    m_inspector = new InspectorWidget;
    connect(m_inspector, &InspectorWidget::effectsEdited, this,
            &MainWindow::onEffectsEdited);
    auto *inspectorDock = new QDockWidget;
    inspectorDock->setObjectName(QStringLiteral("InspectorDock"));
    inspectorDock->setWidget(m_inspector);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    m_aiPanel = new AiPanel(m_kilo, *m_settings);
    auto *aiDock = new QDockWidget;
    aiDock->setObjectName(QStringLiteral("AiDock"));
    aiDock->setWidget(m_aiPanel);
    addDockWidget(Qt::RightDockWidgetArea, aiDock);
    tabifyDockWidget(inspectorDock, aiDock);
    connect(m_aiPanel, &AiPanel::enableRequested, this, [this] { enableAiWithNotice(); });
    // M6.2: accept signals carry validated payloads; the panel never mutates
    // the project itself.
    connect(m_aiPanel, &AiPanel::translationAccepted, this,
            &MainWindow::applyAiTranslation);
    connect(m_aiPanel, &AiPanel::chaptersAccepted, this, &MainWindow::applyAiChapters);
    connect(m_aiPanel, &AiPanel::titleAccepted, this, &MainWindow::applyAiTitle);
    connect(m_aiPanel, &AiPanel::cutsAccepted, this, &MainWindow::applyAiCuts);
    connect(m_aiPanel, &AiPanel::actionsAccepted, this, &MainWindow::applyAiActions);

    m_subtitles = new SubtitlesWidget;
    auto *subDock = new QDockWidget;
    subDock->setObjectName(QStringLiteral("SubtitlesDock"));
    subDock->setWidget(m_subtitles);
    addDockWidget(Qt::BottomDockWidgetArea, subDock);
    // M5.3: the panel mutates the project directly; just mark it dirty.
    connect(m_subtitles, &SubtitlesWidget::subtitlesChanged, this, [this] {
        m_autosave->setProjectChanged();
    });

    m_docks = {mediaDock, inspectorDock, aiDock, subDock};
    // Fill the View menu toggles created in buildActions.
    for (QDockWidget *d : m_docks) {
        auto *act = new QAction(this);
        act->setCheckable(true);
        act->setChecked(d->isVisible());
        m_viewMenu->insertAction(m_themeGroup->actions().first(), act);
        connect(act, &QAction::toggled, d, &QDockWidget::setVisible);
        connect(d, &QDockWidget::visibilityChanged, act, &QAction::setChecked);
        m_dockToggles.append({d, act});
    }
}

void MainWindow::buildStatusBar()
{
    auto make = [this](const QString &init) {
        auto *l = new QLabel(init);
        l->setProperty("role", QStringLiteral("muted"));
        statusBar()->addPermanentWidget(l);
        return l;
    };
    m_timecode = new QLabel(framesToTimecode(0, 30));
    QFont mono;
    mono.setFamilies(m_themer ? m_themer->monoFamilies() : QStringList{QStringLiteral("monospace")});
    mono.setStyleHint(QFont::Monospace);
    m_timecode->setFont(mono);
    statusBar()->addPermanentWidget(m_timecode);
    m_fps = make(QStringLiteral("30 fps"));
    m_cache = make(arenaTr("status.cache", {{QStringLiteral("pct"), 0}}));
    m_mem = make(QString());
    m_dropped = make(arenaTr("status.dropped", {{QStringLiteral("n"), 0}}));
    m_aiBudget = make(arenaTr("panel.ai.quota", {{QStringLiteral("used"), 0}, {QStringLiteral("limit"), 200}}));
    connect(&m_kilo->budget(), &AIBudget::changed, this, [this](int used, int limit) {
        m_aiBudget->setText(arenaTr("panel.ai.quota", {{QStringLiteral("used"), used}, {QStringLiteral("limit"), limit}}));
    });
    tickSecond();
}

void MainWindow::buildCommandBar()
{
    auto *bar = new QToolBar;
    bar->setObjectName(QStringLiteral("CommandBar"));
    bar->setMovable(false);
    m_paletteButton = new QToolButton;
    m_paletteButton->setText(arenaTr("cmd.search"));
    m_paletteButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_paletteButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bar->addWidget(m_paletteButton);
    auto *user = new QToolButton;
    user->setText(QStringLiteral("User"));
    user->setEnabled(false);
    bar->addWidget(user);
    addToolBar(Qt::TopToolBarArea, bar);
    connect(m_paletteButton, &QToolButton::clicked, this, &MainWindow::openPalette);
    auto *paletteAct = new QAction(this);
    paletteAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    connect(paletteAct, &QAction::triggered, this, &MainWindow::openPalette);
    addAction(paletteAct);
}

void MainWindow::openPalette()
{
    if (!m_palette) {
        m_palette = new CommandPalette(this);
        m_palette->setWindowModality(Qt::NonModal);
        registerCommands();
        connect(m_palette, &CommandPalette::aiRequested, this, &MainWindow::askAiFromPalette);
    }
    registerCommands();
    m_palette->show();
    m_palette->raise();
    m_palette->activateWindow();
}

void MainWindow::askAiFromPalette(const QString &text)
{
    if (!m_settings->aiEnabled() || text.isEmpty()) {
        enableAiWithNotice();
        return;
    }
    // M7.3: the palette's /ai mode is the NL command bar — the text is a
    // request interpreted into validated, previewable, undoable actions.
    if (m_project) {
        m_aiPanel->runCommand(text);
    } else {
        statusBar()->showMessage(arenaTr("panel.ai.cmd.noProject"), 4000);
        return;
    }
    if (auto *dock = findChild<QDockWidget *>(QStringLiteral("AiDock")))
        dock->show();
}

void MainWindow::enableAiWithNotice()
{
    if (!m_settings->aiPrivacyAccepted()) {
        QMessageBox box(this);
        box.setWindowTitle(arenaTr("dlg.privacy.title"));
        box.setText(arenaTr("dlg.privacy.body"));
        auto *yesBtn = box.addButton(arenaTr("dlg.privacy.accept"), QMessageBox::AcceptRole);
        box.addButton(arenaTr("dlg.cancel"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != yesBtn) {
            m_aiEnableAct->setChecked(false);
            return;
        }
        m_settings->setAiPrivacyAccepted();
    }
    m_settings->setAiEnabled(true);
    m_kilo->refreshModels();
    m_aiPanel->setEnabledUi(true);
    m_aiEnableAct->setChecked(true);
}

void MainWindow::registerCommands()
{
    if (!m_palette) return;
    QList<PaletteCommand> cmds;
    auto add = [&](const QString &key, const QString &shortcut, std::function<void()> fn) {
        cmds.append({key, shortcut, std::move(fn)});
    };
    add(QStringLiteral("cmd.newProject"), QStringLiteral("Ctrl+N"), [this] {
        NewProjectDialog d(this);
        if (d.exec() == QDialog::Accepted) newProject(d.result());
    });
    add(QStringLiteral("cmd.openProject"), QStringLiteral("Ctrl+O"), [this] {
        const QString f = QFileDialog::getOpenFileName(this, arenaTr("dlg.open.title"),
                                                       QString(), arenaTr("dlg.projectFilter"));
        if (!f.isEmpty()) openProjectFile(f);
    });
    add(QStringLiteral("cmd.save"), QStringLiteral("Ctrl+S"), [this] { saveProjectFile(); });
    add(QStringLiteral("cmd.undo"), QStringLiteral("Ctrl+Z"), [this] { m_undoStack.undo(); });
    add(QStringLiteral("cmd.redo"), QStringLiteral("Ctrl+Y"), [this] { m_undoStack.redo(); });
    add(QStringLiteral("cmd.snap"), QString(), [this] {
        m_settings->setSnapEnabled(!m_settings->snapEnabled());
    });
    for (const QString &t : {QStringLiteral("dark"), QStringLiteral("light"),
                             QStringLiteral("high-contrast")}) {
        add(QStringLiteral("cmd.theme.") + t, QString(), [this, t] {
            m_settings->setTheme(t);
            applyThemeAndFont();
        });
    }
    add(QStringLiteral("cmd.aiPanel"), QString(), [this] {
        if (auto *dock = findChild<QDockWidget *>(QStringLiteral("AiDock"))) dock->show();
    });
    add(QStringLiteral("cmd.settings"), QStringLiteral("Ctrl+,"), [this] {
        SettingsDialog dlg(m_kilo->models(), this);
        dlg.exec();
    });
    add(QStringLiteral("cmd.about"), QString(), [this] {
        AboutDialog dlg(this);
        dlg.exec();
    });
    m_palette->setCommands(cmds);
}

// --- project operations -------------------------------------------------------

bool MainWindow::newProject(const NewProjectResult &r)
{
    m_project = std::make_unique<Project>();
    m_project->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_project->name = r.name;
    m_project->width = r.width;
    m_project->height = r.height;
    m_project->fps = r.fps;
    m_project->aspect = r.aspect;
    m_project->sampleRate = r.sampleRate;
    m_project->proxy.enabled = r.proxy;
    for (const QString &tid : {QStringLiteral("V1"), QStringLiteral("A1")}) {
        Track t;
        t.id = tid;
        t.type = tid.startsWith(QLatin1Char('V')) ? TrackType::Video : TrackType::Audio;
        t.name = tid;
        m_project->tracks.append(t);
    }
    m_projectPath.clear();
    m_undoStack.clear();
    m_playhead = 0;
    m_transportTimer.stop();
    m_engine->close();
    m_currentSource.clear();
    m_monitor->setFrame(QImage());
    m_waveform->setPeaks(QVector<Peak>());
    m_thumbs->setThumbs(QList<QImage>());
    m_subtitles->setProject(m_project.get());
    m_autosave->setProjectPath(QString());
    refreshProjectViews();
    return true;
}

bool MainWindow::openProjectFile(const QString &path)
{
    QString err;
    Project p = Project::loadFile(path, &err);
    if (p.id.isEmpty()) {
        ErrorDialog(err, path, this).exec();
        return false;
    }
    m_project = std::make_unique<Project>(p);
    m_projectPath = path;
    m_undoStack.clear();
    m_playhead = 0;
    m_transportTimer.stop();
    m_engine->close();
    m_currentSource.clear();
    m_monitor->setFrame(QImage());
    m_waveform->setPeaks(QVector<Peak>());
    m_thumbs->setThumbs(QList<QImage>());
    m_subtitles->setProject(m_project.get());
    m_autosave->setProjectPath(path);
    checkRecoveryFor(path);
    refreshProjectViews();
    return true;
}

void MainWindow::checkRecoveryFor(const QString &path)
{
    const QString autoPath = Autosave::pendingRecovery(path);
    if (autoPath.isEmpty()) return;
    QMessageBox box(this);
    box.setWindowTitle(arenaTr("dlg.recovery.title"));
    box.setText(arenaTr("dlg.recovery.body", {{QStringLiteral("project"), m_project ? m_project->name : path}}));
    auto *recoverBtn = box.addButton(arenaTr("dlg.recovery.recover"), QMessageBox::AcceptRole);
    box.addButton(arenaTr("dlg.recovery.discard"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == recoverBtn) {
        QString err;
        Project p = Project::loadFile(autoPath, &err);
        if (!p.id.isEmpty()) m_project = std::make_unique<Project>(p);
    }
    m_subtitles->setProject(m_project.get());
    Autosave::clearRecovery(path);
    refreshProjectViews();
}

bool MainWindow::saveProjectFile(const QString &path)
{
    if (!m_project) return false;
    QString target = path;
    if (target.isEmpty()) {
        if (m_projectPath.isEmpty()) {
            target = QFileDialog::getSaveFileName(this, arenaTr("dlg.save.title"),
                                                  m_project->name, arenaTr("dlg.projectFilter"));
            if (target.isEmpty()) return false;
        } else {
            target = m_projectPath;
        }
    }
    QString err;
    if (!m_project->saveFile(target, &err)) {
        ErrorDialog(err, target, this).exec();
        return false;
    }
    m_projectPath = target;
    Autosave::clearRecovery(target);
    Autosave::markCleanExit(target);
    m_autosave->setProjectPath(target);
    refreshProjectViews();
    return true;
}

void MainWindow::closeProject()
{
    if (!m_projectPath.isEmpty()) Autosave::markCleanExit(m_projectPath);
    m_project.reset();
    m_projectPath.clear();
    m_undoStack.clear();
    m_transportTimer.stop();
    m_engine->close();
    m_currentSource.clear();
    m_monitor->setFrame(QImage());
    m_waveform->setPeaks(QVector<Peak>());
    m_thumbs->setThumbs(QList<QImage>());
    m_subtitles->setProject(nullptr);
    m_autosave->setProjectPath(QString());
    refreshProjectViews();
}

QString MainWindow::currentTitle() const
{
    const QString base = m_project
                             ? QStringLiteral("Arena Edit — %1").arg(m_project->name)
                             : QStringLiteral("Arena Edit");
    const QString saved = m_autosave->lastSavedString();
    return saved.isEmpty() ? base
                           : base + QStringLiteral(" · ")
                                  + arenaTr("status.saved", {{QStringLiteral("time"), saved}});
}

void MainWindow::refreshProjectViews()
{
    setWindowTitle(currentTitle());
    if (m_project) {
        m_timeline->setProject(true, m_project->fps);
        QList<qint64> markerFrames;
        for (const Marker &m : m_project->markers) markerFrames.append(m.time);
        m_timeline->canvas()->setMarkers(markerFrames);
        m_monitor->setAspect(m_project->width, m_project->height);
        m_fps->setText(arenaTr("status.fps", {{QStringLiteral("fps"), QString::number(m_project->fps, 'g', 3)}}));
        m_inspector->setProjectName(m_project->name, m_project->fps, m_project->width, m_project->height);
        m_aiPanel->setProject(m_project.get()); // M6.2: task availability
    } else {
        m_timeline->setProject(false, 30);
        m_inspector->clearSelection();
        m_fps->setText(arenaTr("status.fps", {{QStringLiteral("fps"), QStringLiteral("30")}}));
        m_aiPanel->setProject(nullptr);
    }
    m_timecode->setText(framesToTimecode(m_playhead, m_project ? m_project->fps : 30));
    retranslateUi();
}

// --- i18n / theming -----------------------------------------------------------

void MainWindow::retranslateUi()
{
    m_fileMenu->setTitle(arenaTr("menu.file"));
    m_editMenu->setTitle(arenaTr("menu.edit"));
    m_viewMenu->setTitle(arenaTr("view.viewMenuTitle"));
    m_aiMenu->setTitle(arenaTr("menu.ai"));
    m_helpMenu->setTitle(arenaTr("menu.help"));
    // Menu actions (fixed known order from buildActions).
    const QStringList fileKeys = {
        QStringLiteral("file.newProject"), QStringLiteral("file.open"),
        QStringLiteral("file.save"), QStringLiteral("file.saveAs"),
        QStringLiteral("file.closeProject"), QStringLiteral("file.export"),
        QStringLiteral("file.exit")};
    int i = 0;
    for (QAction *a : m_fileMenu->actions())
        if (!a->isSeparator()) a->setText(arenaTr(fileKeys.value(++i - 1, a->text())));
    const QStringList editKeys = {QStringLiteral("edit.undo"), QStringLiteral("edit.redo"),
                                  QStringLiteral("edit.preferences")};
    i = 0;
    for (QAction *a : m_editMenu->actions())
        if (!a->isSeparator()) a->setText(arenaTr(editKeys.value(++i - 1, a->text())));
    // undo/redo use named variants; refresh now.
    refreshUndoButtons();
    i = 0;
    for (QAction *a : m_aiMenu->actions()) {
        a->setText(i == 0 ? arenaTr(m_settings->aiEnabled() ? "ai.disable" : "ai.enable")
                          : arenaTr("ai.settings"));
        ++i;
    }
    const QStringList helpKeys = {QStringLiteral("help.whatsNew"), QStringLiteral("help.about")};
    i = 0;
    for (QAction *a : m_helpMenu->actions())
        if (!a->isSeparator()) a->setText(arenaTr(helpKeys.value(++i - 1, a->text())));
    // Theme/language submenus.
    const QStringList themeKeys = {
        QStringLiteral("view.theme.dark"), QStringLiteral("view.theme.light"),
        QStringLiteral("view.theme.highContrast")};
    i = 0;
    for (QAction *a : m_themeGroup->actions()) a->setText(arenaTr(themeKeys.value(i++)));
    i = 0;
    for (QAction *a : m_languageGroup->actions()) {
        const auto loc = I18n::instance().locales().value(i);
        a->setText(loc.nativeName);
        ++i;
    }
    // Dock titles.
    const QStringList dockKeys = {
        QStringLiteral("view.mediaBin"), QStringLiteral("view.inspector"),
        QStringLiteral("view.aiPanel"), QStringLiteral("view.subtitles")};
    i = 0;
    for (QDockWidget *d : m_docks) d->setWindowTitle(arenaTr(dockKeys.value(i++)));
    for (auto &t : m_dockToggles)
        t.second->setText(arenaTr(dockKeys.value(m_docks.indexOf(t.first), t.second->text())));

    m_paletteButton->setText(arenaTr("cmd.search"));
    m_mediaBin->retranslateUi();
    m_inspector->retranslateUi();
    m_subtitles->retranslateUi();
    m_aiPanel->retranslateUi();
    m_timeline->retranslateUi();
    m_playbackRes->setToolTip(arenaTr("panel.monitor.playback"));
    m_cache->setText(arenaTr("status.cache", {{QStringLiteral("pct"), 0}}));
    m_aiBudget->setText(arenaTr("panel.ai.quota",
                                {{QStringLiteral("used"), m_kilo->budget().used()},
                                 {QStringLiteral("limit"), m_kilo->budget().limit()}}));
    tickSecond();
    // Re-apply project-specific labels (without calling refreshProjectViews,
    // which ends with retranslateUi and would recurse).
    if (m_project) {
        m_fps->setText(arenaTr("status.fps", {{QStringLiteral("fps"), QString::number(m_project->fps, 'g', 3)}}));
        m_timecode->setText(framesToTimecode(m_playhead, m_project->fps));
    }
}

void MainWindow::applyLanguage()
{
    QApplication::setLayoutDirection(I18n::instance().isRtl() ? Qt::RightToLeft : Qt::LeftToRight);
    retranslateUi();
}

void MainWindow::applyThemeAndFont()
{
    m_themer->apply(qApp, m_settings->theme(), m_settings->accentColor(), m_settings->fontScale());
    // Re-check theme action state (accent changes don't touch it).
    for (QAction *a : m_themeGroup->actions())
        a->setChecked(a->text() == arenaTr(QStringLiteral("view.theme.%1").arg(m_settings->theme())));
}

void MainWindow::refreshUndoButtons()
{
    m_undoAct->setEnabled(m_undoStack.canUndo());
    m_redoAct->setEnabled(m_undoStack.canRedo());
    m_undoAct->setText(m_undoStack.canUndo()
                           ? arenaTr("edit.undoNamed", {{QStringLiteral("action"), m_undoStack.undoActionName()}})
                           : arenaTr("edit.undo"));
    m_redoAct->setText(m_undoStack.canRedo()
                           ? arenaTr("edit.redoNamed", {{QStringLiteral("action"), m_undoStack.redoActionName()}})
                           : arenaTr("edit.redo"));
}

void MainWindow::commitCommand(std::unique_ptr<Command> cmd)
{
    if (!cmd) return;
    // The stack's onChanged callback marks the project dirty (autosave) and
    // refreshes the undo/redo UI.
    m_undoStack.push(std::move(cmd));
}

void MainWindow::noteHistoryChanged()
{
    m_autosave->setProjectChanged();
    refreshUndoButtons();
    if (m_project && !m_selectedClip.isEmpty()) {
        bool found = false;
        for (const Track &t : m_project->tracks)
            for (const Clip &c : t.clips)
                if (c.id == m_selectedClip) {
                    found = true;
                    break;
                }
        if (!found) m_selectedClip.clear();
    }
    refreshTimeline();
}

void MainWindow::tickSecond()
{
    m_mem->setText(arenaTr("status.mem", {{QStringLiteral("size"), formatBytes(processResidentBytes())}}));
    // M3: live render-cache occupancy.
    if (m_cache) {
        const qint64 cap = qMax<qint64>(1, m_frameCache.maxBytes());
        const int pct = int(m_frameCache.usedBytes() * 100 / cap);
        m_cache->setText(arenaTr("status.cache", {{QStringLiteral("pct"), pct}}));
    }
    if (m_autosave->lastSaved().isValid())
        setWindowTitle(currentTitle());
}

void MainWindow::showWelcomeIfNeeded()
{
    if (qEnvironmentVariableIsSet("ARENA_SELFTEST")) return;
    if (m_settings->firstRunDone()) return;
    WelcomeDialog dlg(this);
    dlg.exec();
    const QString lang = dlg.chosenLanguage();
    if (!lang.isEmpty()) I18n::instance().setLocale(lang);
    m_settings->setFirstRunDone();
}

// --- M1 media pipeline ---------------------------------------------------------

namespace {

// Append a clip to a video track (undoable).
struct AddClipCommand : public Command {
    Track *track = nullptr;
    Clip clip;
    AddClipCommand(Track *t, Clip c) : track(t), clip(std::move(c)) {}
    void execute() override { track->clips.append(clip); }
    void undo() override
    {
        for (int i = track->clips.size() - 1; i >= 0; --i)
            if (track->clips[i].id == clip.id) {
                track->clips.removeAt(i);
                break;
            }
    }
    QString actionName() const override
    {
        return arenaTr("media.clipAdded", {{QStringLiteral("track"), track->name}});
    }
    int retainedBytes() const override { return 384; }
};

// Add / remove a timeline marker (undoable).
struct AddMarkerCommand : public Command {
    Project *project = nullptr;
    qint64 time = 0;
    AddMarkerCommand(Project *p, qint64 t) : project(p), time(t) {}
    void execute() override
    {
        Marker m;
        m.time = time;
        project->markers.append(m);
    }
    void undo() override
    {
        for (int i = project->markers.size() - 1; i >= 0; --i)
            if (project->markers[i].time == time) {
                project->markers.removeAt(i);
                return;
            }
    }
    QString actionName() const override { return arenaTr("timeline.markerAdded"); }
    int retainedBytes() const override { return 64; }
};

struct RemoveMarkerCommand : public Command {
    Project *project = nullptr;
    qint64 time = 0;
    RemoveMarkerCommand(Project *p, qint64 t) : project(p), time(t) {}
    void execute() override
    {
        for (int i = project->markers.size() - 1; i >= 0; --i)
            if (project->markers[i].time == time) {
                project->markers.removeAt(i);
                return;
            }
    }
    void undo() override
    {
        Marker m;
        m.time = time;
        project->markers.append(m);
    }
    QString actionName() const override { return arenaTr("timeline.markerRemoved"); }
    int retainedBytes() const override { return 64; }
};

// Apply one or two clip geometry patches (trim / roll; undoable).
struct TrimClipsCommand : public Command {
    Project *project = nullptr;
    QList<ClipPatch> to;
    QList<ClipPatch> from;
    bool roll = false;
    TrimClipsCommand(Project *p, const QList<ClipPatch> &patches, bool isRoll)
        : project(p), to(patches), roll(isRoll)
    {
    }
    void applyList(const QList<ClipPatch> &list)
    {
        for (const ClipPatch &t : list) {
            for (Track &tr : project->tracks)
                for (Clip &c : tr.clips)
                    if (c.id == t.clipId) {
                        c.offset = t.offset;
                        c.in = t.in;
                        c.out = t.out;
                    }
        }
    }
    void execute() override
    {
        from.clear();
        for (const ClipPatch &t : to) {
            ClipPatch f;
            f.clipId = t.clipId;
            for (Track &tr : project->tracks)
                for (const Clip &c : tr.clips)
                    if (c.id == t.clipId) {
                        f.offset = c.offset;
                        f.in = c.in;
                        f.out = c.out;
                    }
            from.append(f);
        }
        applyList(to);
    }
    void undo() override { applyList(from); }
    QString actionName() const override
    {
        return arenaTr(roll ? "timeline.rollEdit" : "timeline.trimEdge");
    }
    int retainedBytes() const override { return 256 * (1 + to.size()); }
};

// Replace the effect JSON on a single clip (undoable).
struct SetEffectsCommand : public Command {
    Project *project = nullptr;
    QString clipId;
    QJsonObject to;
    QJsonObject from;
    SetEffectsCommand(Project *p, QString id, QJsonObject fx)
        : project(p), clipId(std::move(id)), to(std::move(fx))
    {
    }
    void execute() override
    {
        from = QJsonObject();
        for (Track &tr : project->tracks)
            for (const Clip &c : tr.clips)
                if (c.id == clipId) from = c.effects;
        for (Track &tr : project->tracks)
            for (Clip &c : tr.clips)
                if (c.id == clipId) c.effects = to;
    }
    void undo() override
    {
        for (Track &tr : project->tracks)
            for (Clip &c : tr.clips)
                if (c.id == clipId) c.effects = from;
    }
    QString actionName() const override { return arenaTr("cmd.setEffects"); }
    int retainedBytes() const override
    {
        return 128 + int(to.size()) + int(from.size());
    }
};

// Add a whole subtitle track (AI translation accept; undoable).
struct AddSubtitleTrackCommand : public Command {
    Project *project = nullptr;
    SubtitleTrack track;
    AddSubtitleTrackCommand(Project *p, SubtitleTrack t)
        : project(p), track(std::move(t)) {}
    void execute() override { project->subtitleTracks.append(track); }
    void undo() override
    {
        for (int i = project->subtitleTracks.size() - 1; i >= 0; --i)
            if (project->subtitleTracks[i].id == track.id) {
                project->subtitleTracks.removeAt(i);
                return;
            }
    }
    QString actionName() const override { return arenaTr("subtitles.trackAdded"); }
    int retainedBytes() const override { return 512 + track.cues.size() * 96; }
};

// Add several labeled markers at once (AI chapters accept; undoable).
struct AddMarkersCommand : public Command {
    Project *project = nullptr;
    QList<Marker> markers;
    AddMarkersCommand(Project *p, QList<Marker> m)
        : project(p), markers(std::move(m)) {}
    void execute() override
    {
        for (const Marker &m : markers) project->markers.append(m);
    }
    void undo() override
    {
        // Remove newest first, matching (time, label) from the back, so a
        // user marker added later at the same time is never touched.
        for (int i = markers.size() - 1; i >= 0; --i) {
            const Marker m = markers[i];
            for (int j = project->markers.size() - 1; j >= 0; --j) {
                if (project->markers[j].time == m.time &&
                    project->markers[j].label == m.label) {
                    project->markers.removeAt(j);
                    break;
                }
            }
        }
    }
    QString actionName() const override { return arenaTr("timeline.chaptersAdded"); }
    int retainedBytes() const override { return 64 * (1 + markers.size()); }
};

// Ripple-remove [s, e) from the primary video track (AI cuts accept; one
// undoable command). Clips fully inside the range are deleted; clips
// overlapping an edge are trimmed in place; a clip strictly containing the
// range is split (the tail becomes a new clip starting at s); every clip
// after the range shifts left by the removed length. Audio/other tracks are
// untouched (documented in DECISIONS.md).
struct RippleRemoveRangeCommand : public Command {
    Project *project = nullptr;
    int trackIndex = -1;
    qint64 s = 0;
    qint64 e = 0;
    QList<Clip> saved; // full snapshot of the track before the cut
    bool hasSaved = false;

    RippleRemoveRangeCommand(Project *p, int ti, qint64 start, qint64 end)
        : project(p), trackIndex(ti), s(start), e(end) {}

    void execute() override
    {
        if (!project || trackIndex < 0 || trackIndex >= project->tracks.size())
            return;
        Track &tr = project->tracks[trackIndex];
        if (!hasSaved) {
            saved = tr.clips;
            hasSaved = true;
        }
        tr.clips = cutRange(saved, s, e);
    }

    void undo() override
    {
        if (!project || !hasSaved || trackIndex < 0 ||
            trackIndex >= project->tracks.size())
            return;
        project->tracks[trackIndex].clips = saved;
    }

    static QList<Clip> cutRange(const QList<Clip> &in, qint64 s, qint64 e)
    {
        QList<Clip> out;
        const qint64 len = e - s;
        if (len <= 0) return in;
        for (const Clip &c : in) {
            const qint64 a = c.offset;
            const qint64 b = a + c.duration();
            const qint64 removedBefore = qBound<qint64>(0, a - s, len);
            if (b <= s || a >= e) {
                Clip n = c;
                n.offset = a - removedBefore;
                out.append(n);
                continue;
            }
            // Overlap with [s, e): keep the pre-cut part [a, s) in place and
            // the post-cut part [e, b) adjacent at s. A clip fully inside
            // the range yields no parts and is deleted.
            if (a < s) {
                Clip n = c;
                n.out = c.in + (s - a);
                out.append(n);
            }
            if (b > e) {
                Clip n = c;
                if (a >= s) n.id = c.id; // no head part: keep identity
                else n.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                n.in = c.in + (e - a);
                n.offset = s;
                out.append(n);
            }
        }
        return out;
    }

    QString actionName() const override { return arenaTr("timeline.rangeCut"); }
    int retainedBytes() const override { return 256 * (1 + saved.size()); }
};

// Remove one clip from a track (undoable; re-inserts at the same position).
struct DeleteClipCommand : public Command {
    Track *track = nullptr;
    QString clipId;
    Clip removed;
    int index = -1;
    DeleteClipCommand(Track *t, QString id) : track(t), clipId(std::move(id)) {}
    void execute() override
    {
        index = -1;
        for (int i = 0; i < track->clips.size(); ++i)
            if (track->clips[i].id == clipId) {
                removed = track->clips[i];
                index = i;
                track->clips.removeAt(i);
                break;
            }
    }
    void undo() override
    {
        if (removed.id.isEmpty()) return;
        track->clips.insert(qBound(0, index, track->clips.size()), removed);
    }
    QString actionName() const override { return arenaTr("media.clipDeleted"); }
    int retainedBytes() const override { return 512; }
};

// Set (or clear) a clip's tail transition — the `effects.transition`
// object the compositor renders (M3). Only that sub-object is touched, so
// other effects survive; undo restores the previous one.
struct SetTransitionCommand : public Command {
    Project *project = nullptr;
    QString clipId;
    QJsonObject to; // empty object = remove the transition
    QJsonObject from;
    bool hasFrom = false;
    SetTransitionCommand(Project *p, QString id, QJsonObject tr)
        : project(p), clipId(std::move(id)), to(std::move(tr))
    {
    }
    void applySide(const QJsonObject &tr, bool present)
    {
        for (Track &t : project->tracks)
            for (Clip &c : t.clips)
                if (c.id == clipId) {
                    QJsonObject fx = c.effects;
                    if (present) fx.insert(QStringLiteral("transition"), tr);
                    else fx.remove(QStringLiteral("transition"));
                    c.effects = fx;
                }
    }
    void execute() override
    {
        hasFrom = false;
        for (Track &t : project->tracks)
            for (const Clip &c : t.clips)
                if (c.id == clipId) {
                    from = c.effects.value(QStringLiteral("transition")).toObject();
                    hasFrom = true;
                }
        applySide(to, !to.isEmpty());
    }
    void undo() override
    {
        if (!hasFrom) return;
        applySide(from, !from.isEmpty());
    }
    QString actionName() const override { return arenaTr("cmd.setTransition"); }
    int retainedBytes() const override { return 128 + int(to.size()) + int(from.size()); }
};

// Set one clip's playback speed (undoable).
struct SetClipSpeedCommand : public Command {
    Project *project = nullptr;
    QString clipId;
    double to = 1.0;
    double from = 1.0;
    bool hasFrom = false;
    SetClipSpeedCommand(Project *p, QString id, double speed)
        : project(p), clipId(std::move(id)), to(speed)
    {
    }
    void execute() override
    {
        hasFrom = false;
        for (Track &t : project->tracks)
            for (const Clip &c : t.clips)
                if (c.id == clipId) {
                    from = c.speed;
                    hasFrom = true;
                }
        for (Track &t : project->tracks)
            for (Clip &c : t.clips)
                if (c.id == clipId) c.speed = to;
    }
    void undo() override
    {
        if (!hasFrom) return;
        for (Track &t : project->tracks)
            for (Clip &c : t.clips)
                if (c.id == clipId) c.speed = from;
    }
    QString actionName() const override { return arenaTr("cmd.setClipSpeed"); }
    int retainedBytes() const override { return 96; }
};

// Add a labeled timeline marker (undoable; remove the newest match).
struct AddLabeledMarkerCommand : public Command {
    Project *project = nullptr;
    qint64 time = 0;
    QString label;
    AddLabeledMarkerCommand(Project *p, qint64 t, QString lbl)
        : project(p), time(t), label(std::move(lbl))
    {
    }
    void execute() override
    {
        Marker m;
        m.time = time;
        m.label = label;
        project->markers.append(m);
    }
    void undo() override
    {
        for (int i = project->markers.size() - 1; i >= 0; --i)
            if (project->markers[i].time == time && project->markers[i].label == label) {
                project->markers.removeAt(i);
                return;
            }
    }
    QString actionName() const override { return arenaTr("timeline.markerAdded"); }
    int retainedBytes() const override { return 64 + int(label.size()); }
};

// Re-point all clips from one source to another (undoable).
struct RelinkCommand : public Command {
    Project *project = nullptr;
    QString from;
    QString to;
    RelinkCommand(Project *p, const QString &f, const QString &t)
        : project(p), from(f), to(t)
    {
    }
    void execute() override
    {
        for (Track &t : project->tracks)
            for (Clip &c : t.clips)
                if (c.sourceRef == from) c.sourceRef = to;
    }
    void undo() override
    {
        for (Track &t : project->tracks)
            for (Clip &c : t.clips)
                if (c.sourceRef == to) c.sourceRef = from;
    }
    QString actionName() const override { return arenaTr("media.relinked"); }
    int retainedBytes() const override { return 512; }
};

} // namespace

QImage MainWindow::previewImage() const
{
    return m_monitor ? m_monitor->frame() : QImage();
}

MediaInfo MainWindow::mediaInfo(const QString &path) const
{
    return m_mediaInfo.value(path);
}

void MainWindow::importMedia(const QStringList &paths)
{
    for (const QString &p : paths) {
        if (m_mediaInfo.contains(p)) continue;
        const MediaInfo stub = MediaProbe::probe(p);
        if (!stub.exists) {
            m_mediaInfo.insert(p, stub);
            m_mediaBin->setItemInfo(p, stub);
            continue;
        }
        m_mediaBin->setItemProbing(p);
        probeMedia(p);
    }
}

void MainWindow::probeMedia(const QString &path)
{
    std::thread([path, this] {
        MediaInfo info;
        QString err;
        if (!MediaProbe::probeFull(path, &info, &err)) {
            info = MediaProbe::probe(path);
            info.error = err;
        }
        QMetaObject::invokeMethod(this, [this, path, info] { onMediaProbed(path, info); },
                                  Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onMediaProbed(const QString &path, const MediaInfo &info)
{
    m_mediaInfo.insert(path, info);
    m_mediaBin->setItemInfo(path, info);
    maybeStartProxy(path, info);
    // If a monitor is already aimed at this source, (re)open the engine.
    if (m_currentSource == path && m_engine->isOpen() && m_engineSource != playablePathFor(path)) {
        selectClipAt(path, m_playhead);
    }
}

void MainWindow::maybeStartProxy(const QString &path, const MediaInfo &info)
{
    if (!m_project || !m_project->proxy.enabled) return;
    if (!info.hasVideo || info.height <= 0) return;
    if (info.height <= m_project->proxy.height) return; // already small enough
    if (m_proxies.contains(path) && QFileInfo::exists(m_proxies.value(path))) return;
    const QString dir = ProxyGenerator::defaultProxyDir();
    const QFileInfo fi(path);
    const QByteArray digest = QCryptographicHash::hash(
        (fi.absoluteFilePath() + QStringLiteral("|") + QString::number(fi.size())).toUtf8(),
        QCryptographicHash::Sha1);
    const QString proxyPath = dir + QStringLiteral("/proxy_") +
                              QString::fromLatin1(digest.toHex().left(16)) +
                              QStringLiteral(".mp4");
    if (QFileInfo::exists(proxyPath) &&
        QFileInfo(proxyPath).lastModified() >= fi.lastModified()) {
        m_proxies.insert(path, proxyPath);
        return;
    }
    const int targetHeight = m_project->proxy.height;
    std::thread([this, path, proxyPath, targetHeight] {
        QString err;
        std::function<void(double)> prog = [this](double d) {
            const int pct = static_cast<int>(d * 100.0);
            QMetaObject::invokeMethod(
                this, [this, pct] {
                    statusBar()->showMessage(
                        arenaTr("media.proxying", {{QStringLiteral("pct"), pct}}), 800);
                },
                Qt::QueuedConnection);
        };
        const bool ok = ProxyGenerator::generate(path, proxyPath, targetHeight, &err, &prog);
        const QString finalErr = ok ? QString() : err;
        QMetaObject::invokeMethod(
            this, [this, path, proxyPath, finalErr] {
                onProxyReady(path, proxyPath, finalErr);
            },
            Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onProxyReady(const QString &source, const QString &proxy, const QString &err)
{
    if (err.isEmpty()) {
        m_proxies.insert(source, proxy);
        statusBar()->showMessage(arenaTr("media.proxyReady"), 3000);
    } else {
        // Fall back to full resolution (works, just slower scrubbing).
        statusBar()->showMessage(
            arenaTr("media.proxyFailed", {{QStringLiteral("err"), err}}), 5000);
    }
}

QString MainWindow::playablePathFor(const QString &path) const
{
    if (m_proxies.contains(path) && QFileInfo::exists(m_proxies.value(path)))
        return m_proxies.value(path);
    return path;
}

void MainWindow::addClipToTimeline(const QString &path)
{
    if (!m_project) {
        NewProjectResult r; // Defaults: 1920x1080 @ 30 fps, proxies on.
        newProject(r);
    }
    const MediaInfo info = m_mediaInfo.value(path);
    if (!info.exists) {
        statusBar()->showMessage(arenaTr("media.missing"), 3000);
        return;
    }
    if (info.exists && info.durationMs <= 0 && !info.hasVideo && !info.hasAudio) {
        statusBar()->showMessage(arenaTr("media.probing"), 2000); // still probing
        return;
    }
    if (!info.error.isEmpty()) {
        statusBar()->showMessage(
            arenaTr("media.loadError", {{QStringLiteral("file"), QFileInfo(path).fileName()}}),
            4000);
        return;
    }
    Track *vtrack = nullptr;
    for (Track &t : m_project->tracks)
        if (t.type == TrackType::Video) {
            vtrack = &t;
            break;
        }
    if (!vtrack) return;

    const double fps = m_project->fps;
    const qint64 durFrames =
        info.durationMs > 0 ? qMax<qint64>(1, qRound(info.durationMs / 1000.0 * fps))
                            : qMax<qint64>(1, qRound(fps)); // unknown: 1 s
    qint64 offset = 0;
    for (const Clip &c : vtrack->clips) offset = qMax(offset, c.offset + c.duration());

    Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.sourceRef = path;
    clip.in = 0;
    clip.out = durFrames;
    clip.offset = offset;

    commitCommand(std::make_unique<AddClipCommand>(vtrack, clip));
    m_currentSource = path;
    selectClipAt(path, offset);
    generateWaveformThumbs(path);
    statusBar()->showMessage(
        arenaTr("media.clipAdded", {{QStringLiteral("track"), vtrack->name}}), 2500);
}

void MainWindow::selectClipAt(const QString &source, qint64 timelineFrame)
{
    if (!m_project) return;
    double relSec = 0.0;
    for (const Track &t : m_project->tracks) {
        if (t.type != TrackType::Video) continue;
        for (const Clip &c : t.clips) {
            if (c.sourceRef != source) continue;
            relSec = double(c.in + (timelineFrame - c.offset)) / m_project->fps;
            break;
        }
    }
    m_currentSource = source;
    const QString playable = playablePathFor(source);
    QString err;
    if (!m_engine->isOpen() || m_engineSource != playable) {
        m_engine->close();
        if (!m_engine->open(playable, &err)) {
            statusBar()->showMessage(
                arenaTr("media.loadError", {{QStringLiteral("file"), QFileInfo(source).fileName()}}),
                4000);
            m_currentSource.clear();
            refreshTimeline();
            updateTransportButtons();
            return;
        }
        m_engineSource = playable;
    }
    m_engine->seekSec(std::clamp(relSec, 0.0, m_engine->durationSec()));
    if (m_waveform->peaks().isEmpty() && m_wavePeaks.contains(source))
        m_waveform->setPeaks(m_wavePeaks.value(source));
    refreshTimeline();
    updateTransportButtons();
}

void MainWindow::generateWaveformThumbs(const QString &path)
{
    const QString playable = playablePathFor(path);
    std::thread([this, path, playable] {
        QVector<Peak> peaks;
        QString err;
        WaveformGenerator::generate(playable, 120, &peaks, &err);
        QList<QImage> thumbs;
        Thumbnailer::generate(playable, 8, 44, &thumbs, nullptr);
        QMetaObject::invokeMethod(
            this, [this, path, peaks, thumbs] {
                m_wavePeaks.insert(path, peaks);
                if (m_currentSource == path) {
                    m_waveform->setPeaks(peaks);
                    m_thumbs->setThumbs(thumbs);
                }
                refreshTimeline();
            },
            Qt::QueuedConnection);
    }).detach();
}

void MainWindow::refreshTimeline()
{
    if (!m_timeline) return;
    if (!m_project) {
        m_timeline->canvas()->setClips({});
        return;
    }
    QList<ClipView> views;
    bool done = false;
    for (const Track &t : m_project->tracks) {
        if (t.type != TrackType::Video || done) continue;
        done = true; // canvas draws clips on the first video track
        for (const Clip &c : t.clips) {
            ClipView v;
            v.clipId = c.id;
            v.offsetFrames = c.offset;
            v.durationFrames = c.duration();
            v.inFrames = c.in;
            const MediaInfo mi = m_mediaInfo.value(c.sourceRef);
            if (mi.durationMs > 0)
                v.sourceDurationFrames =
                    qMax<qint64>(1, qRound(mi.durationMs / 1000.0 * m_project->fps));
            v.selected = (c.id == m_selectedClip);
            v.missing = !QFileInfo::exists(c.sourceRef);
            if (m_wavePeaks.contains(c.sourceRef)) v.waveform = m_wavePeaks.value(c.sourceRef);
            views.append(v);
        }
    }
    m_timeline->canvas()->setClips(views);
    m_timeline->canvas()->setPlayhead(m_playhead);
    QList<qint64> markerFrames;
    for (const Marker &m : m_project->markers) markerFrames.append(m.time);
    m_timeline->canvas()->setMarkers(markerFrames);

    // Inspector: the selected clip, or the project summary.
    for (const Track &t : m_project->tracks) {
        for (const Clip &c : t.clips) {
            if (c.id != m_selectedClip) continue;
            ClipSelection sel;
            sel.clipId = c.id;
            sel.source = QFileInfo(c.sourceRef).fileName();
            sel.in = c.in;
            sel.out = c.out;
            sel.offset = c.offset;
            sel.fps = m_project->fps;
            sel.hasVideo = true;
            sel.missing = !QFileInfo::exists(c.sourceRef);
            sel.effects = c.effects;
            m_inspector->setClip(sel);
            return;
        }
    }
    m_inspector->setProjectName(m_project->name, m_project->fps, m_project->width,
                                m_project->height);
}

void MainWindow::seekToFrame(qint64 frame)
{
    if (!m_project) {
        m_playhead = qMax<qint64>(0, frame);
        return;
    }
    qint64 total = 0;
    for (const Track &t : m_project->tracks)
        if (t.type == TrackType::Video)
            for (const Clip &c : t.clips) total = qMax(total, c.offset + c.duration());
    const qint64 clamped = qBound<qint64>(0, frame, qMax<qint64>(0, total - 1));
    m_playhead = clamped;
    m_timecode->setText(framesToTimecode(clamped, m_project->fps));
    m_timeline->canvas()->setPlayhead(clamped);
    for (const Track &t : m_project->tracks) {
        if (t.type != TrackType::Video) continue;
        for (const Clip &c : t.clips) {
            if (clamped < c.offset || clamped >= c.offset + c.duration()) continue;
            if (c.sourceRef != m_currentSource) {
                selectClipAt(c.sourceRef, clamped);
            } else if (m_engine->isOpen()) {
                // Source frame at the playhead includes the in-point.
                m_engine->seekSec(double(c.in + (clamped - c.offset)) / m_project->fps);
            }
            return;
        }
    }
}

// M3: playback resolution scaling (monitor display only; the compositor
// always works at project resolution).
static QImage scaleForPlayback(const QImage &img, int scale)
{
    if (img.isNull() || scale <= 1) return img;
    return img.scaled(img.width() / scale, img.height() / scale,
                      Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void MainWindow::applyPlaybackResolution()
{
    m_playbackScale = qMax(1, 1 << m_playbackRes->currentIndex()); // Full, Half, Quarter
    if (!m_project || !m_engine || !m_engine->isOpen()) return;
    const QImage img = m_engine->frameAtSec(m_engine->positionSec());
    if (!img.isNull()) m_monitor->setFrame(renderProjectFrame(m_playhead, img));
}

QImage MainWindow::renderProjectFrame(qint64 frame, const QImage &srcFrame) const
{
    if (!m_project || srcFrame.isNull()) return srcFrame;
    // Fast path: no clip carries effects AND no subtitle track is burned in
    // (burn-in happens inside the compositor, so it forces the slow path).
    bool anyFx = false;
    for (const Track &t : m_project->tracks) {
        if (t.type != TrackType::Video) continue;
        for (const Clip &c : t.clips) {
            if (!c.effects.isEmpty()) {
                anyFx = true;
                break;
            }
        }
        if (anyFx) break;
    }
    bool anyBurnIn = false;
    for (const SubtitleTrack &st : m_project->subtitleTracks)
        if (st.burnIn) { anyBurnIn = true; break; }
    if (!anyFx && !anyBurnIn) return scaleForPlayback(srcFrame, m_playbackScale);

    // Fetcher: decoded source frames, LRU-cached. The engine's current
    // source is decoded through the open engine (frame-accurate seek); any
    // other clip's source (transitions between files) decodes on demand.
    auto fetch = [this](const QString &source, qint64 f) -> QImage {
        const QString key = QStringLiteral("src|%1|%2").arg(source).arg(f);
        QImage cached;
        if (m_frameCache.get(key, &cached)) return cached;
        QImage img;
        const QString playable = playablePathFor(source);
        if (source == m_currentSource && m_engineSource == playable && m_engine->isOpen()) {
            img = m_engine->frameAtSec(double(f) / qMax(0.1, m_engine->fps()));
        } else {
            VideoDecoder dec;
            if (dec.open(playable)) {
                QImage tmp;
                if (dec.frameAtSec(double(f) / qMax(0.1, dec.fps()), &tmp)) img = tmp;
            }
        }
        if (!img.isNull()) m_frameCache.put(key, img);
        return img;
    };
    return scaleForPlayback(Compositor::renderFrame(*m_project, frame, fetch),
                            m_playbackScale);
}

void MainWindow::onEngineFrame(const QImage &img, int)
{
    m_monitor->setFrame(m_project ? renderProjectFrame(m_playhead, img) : img);
}

void MainWindow::onEnginePosition(double sec)
{
    m_dropped->setText(arenaTr("status.dropped", {{QStringLiteral("n"),
                                                   m_engine->droppedFrames()}}));
    if (!m_project || !m_engine->isPlaying()) return;
    for (const Track &t : m_project->tracks) {
        if (t.type != TrackType::Video) continue;
        for (const Clip &c : t.clips) {
            if (c.sourceRef != m_currentSource) continue;
            m_playhead = c.offset + qRound(sec * m_project->fps);
            if (m_playhead >= c.offset + c.duration()) m_playhead = c.offset + c.duration() - 1;
            m_timecode->setText(framesToTimecode(m_playhead, m_project->fps));
            m_timeline->canvas()->setPlayhead(m_playhead);
            return;
        }
    }
}

void MainWindow::onEngineEnded()
{
    updateTransportButtons();
}

void MainWindow::onPlayStateChanged(bool playing)
{
    updateTransportButtons();
    if (!playing)
        m_dropped->setText(arenaTr("status.dropped", {{QStringLiteral("n"), 0}}));
}

void MainWindow::togglePlayback()
{
    if (!m_engine->isOpen()) {
        if (m_currentSource.isEmpty()) {
            statusBar()->showMessage(arenaTr("media.noClips"), 2500);
            return;
        }
        selectClipAt(m_currentSource, m_playhead);
    }
    if (m_engine->isPlaying())
        m_engine->pause();
    else
        m_engine->play();
    updateTransportButtons();
}

void MainWindow::updateTransportButtons()
{
    if (!m_playBtn) return;
    const bool playing = m_engine && m_engine->isPlaying();
    m_playBtn->setText(playing ? QStringLiteral("\u2759\u2759") : QStringLiteral("\u25B6"));
    m_playBtn->setToolTip(arenaTr(playing ? "transport.pause" : "transport.play"));
}

void MainWindow::relinkMedia(const QString &oldPath)
{
    const QString f = QFileDialog::getOpenFileName(
        this, arenaTr("panel.mediaBin.relink"), QFileInfo(oldPath).absolutePath(),
        arenaTr("media.filter"));
    if (f.isEmpty() || f == oldPath) return;
    m_mediaBin->replacePath(oldPath, f);
    if (m_project) {
        commitCommand(std::make_unique<RelinkCommand>(m_project.get(), oldPath, f));
        m_autosave->setProjectChanged();
    }
    if (m_currentSource == oldPath) m_currentSource = f;
    m_mediaInfo.remove(oldPath);
    m_wavePeaks.remove(oldPath);
    m_proxies.remove(oldPath);
    m_engine->close();
    m_engineSource.clear();
    probeMedia(f);
    refreshTimeline();
    statusBar()->showMessage(arenaTr("media.relinked"), 2500);
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    if (auto *fw = qApp->focusWidget()) {
        if (fw->inherits("QLineEdit") || fw->inherits("QPlainTextEdit") ||
            fw->inherits("QTextEdit") || fw->inherits("QAbstractSpinBox")) {
            QWidget::keyPressEvent(e);
            return;
        }
    }
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier) {
        togglePlayback();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Delete) {
        deleteSelectedClip();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_W || e->key() == Qt::Key_Q) {
        rippleTrim(e->key() == Qt::Key_W);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_M) {
        addMarkerAt(m_playhead);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

void MainWindow::addMarkerAt(qint64 frame)
{
    if (!m_project || frame < 0) return;
    commitCommand(std::make_unique<AddMarkerCommand>(m_project.get(), frame));
}

void MainWindow::removeMarkerAt(qint64 frame)
{
    if (!m_project) return;
    for (int i = m_project->markers.size() - 1; i >= 0; --i)
        if (m_project->markers[i].time == frame) {
            commitCommand(std::make_unique<RemoveMarkerCommand>(m_project.get(), frame));
            return;
        }
}

void MainWindow::onTrimRequested(const QList<ClipPatch> &patches)
{
    if (!m_project || patches.isEmpty()) return;
    const bool roll = patches.size() > 1;
    commitCommand(std::make_unique<TrimClipsCommand>(m_project.get(), patches, roll));
}

// M3: effects editor (Inspector) -> undoable command.
void MainWindow::onEffectsEdited(const QString &clipId, const QJsonObject &fx)
{
    if (!m_project || clipId.isEmpty()) return;
    commitCommand(std::make_unique<SetEffectsCommand>(m_project.get(), clipId, fx));
}

void MainWindow::rippleTrim(bool rightSide)
{
    if (!m_project) return;
    Track *track = nullptr;
    for (Track &t : m_project->tracks)
        if (t.type == TrackType::Video) {
            track = &t;
            break;
        }
    if (!track || track->clips.isEmpty()) return;
    const qint64 f = m_playhead;
    for (int i = 0; i < track->clips.size(); ++i) {
        Clip &c = track->clips[i];
        const qint64 end = c.offset + c.duration();
        if (f < c.offset || f >= end) continue; // playhead must be inside
        QList<ClipPatch> patches;
        ClipPatch p;
        p.clipId = c.id;
        p.offset = c.offset;
        if (rightSide) {
            // W: trim the start to the playhead; later clips shift right.
            if (f <= c.offset) return;
            const qint64 grow = f - c.offset;
            p.in = c.in + grow;
            p.out = c.out;
            for (int j = i + 1; j < track->clips.size(); ++j) {
                Clip &n = track->clips[j];
                ClipPatch np;
                np.clipId = n.id;
                np.offset = n.offset + grow;
                np.in = n.in;
                np.out = n.out;
                patches.append(np);
            }
        } else {
            // Q: trim the end to the playhead; later clips shift left.
            if (f >= end) return;
            const qint64 shrink = end - f;
            p.in = c.in;
            p.out = c.in + (f - c.offset); // source frame at the playhead
            for (int j = i + 1; j < track->clips.size(); ++j) {
                Clip &n = track->clips[j];
                ClipPatch np;
                np.clipId = n.id;
                np.offset = n.offset - shrink;
                np.in = n.in;
                np.out = n.out;
                patches.append(np);
            }
        }
        patches.prepend(p);
        m_selectedClip = c.id;
        commitCommand(std::make_unique<TrimClipsCommand>(m_project.get(), patches, false));
        return;
    }
}

void MainWindow::deleteSelectedClip()
{
    if (!m_project || m_selectedClip.isEmpty()) {
        statusBar()->showMessage(arenaTr("timeline.noSelection"), 2500);
        return;
    }
    Track *track = nullptr;
    int index = -1;
    for (Track &t : m_project->tracks)
        for (int i = 0; i < t.clips.size(); ++i)
            if (t.clips[i].id == m_selectedClip) {
                track = &t;
                index = i;
                break;
            }
    if (!track || index < 0) {
        m_selectedClip.clear();
        refreshTimeline();
        return;
    }
    const QString source = track->clips[index].sourceRef;
    commitCommand(std::make_unique<DeleteClipCommand>(track, m_selectedClip));
    m_selectedClip.clear();
    if (m_currentSource == source) {
        m_currentSource.clear();
        m_engine->close();
        m_engineSource.clear();
        m_monitor->setFrame(QImage());
        updateTransportButtons();
    }
    refreshTimeline();
}

// --- M6.2 AI feature application (accept paths from the AI panel) -----------

int MainWindow::primaryVideoTrackIndex() const
{
    if (!m_project) return -1;
    for (int i = 0; i < m_project->tracks.size(); ++i)
        if (m_project->tracks[i].type == TrackType::Video) return i;
    return -1;
}

void MainWindow::applyCutRange(qint64 startFrame, qint64 endFrame)
{
    if (!m_project || endFrame <= startFrame) return;
    const int ti = primaryVideoTrackIndex();
    if (ti < 0) {
        statusBar()->showMessage(arenaTr("panel.ai.noVideoTrack"), 4000);
        return;
    }
    commitCommand(std::make_unique<RippleRemoveRangeCommand>(
        m_project.get(), ti, startFrame, endFrame));
}

void MainWindow::applyAiTranslation(const QString &sourceTrackId,
                                    const QString &targetLang,
                                    const QStringList &translatedTexts)
{
    if (!m_project) return;
    const SubtitleTrack *src = nullptr;
    for (const SubtitleTrack &t : m_project->subtitleTracks)
        if (t.id == sourceTrackId) { src = &t; break; }
    if (!src || translatedTexts.size() != src->cues.size()) {
        statusBar()->showMessage(arenaTr("panel.ai.translationStale"), 4000);
        return;
    }
    SubtitleTrack st;
    st.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    st.language = targetLang;
    st.name = targetLang;
    st.burnIn = false;
    for (int i = 0; i < src->cues.size(); ++i) {
        TimelineCue c = src->cues[i]; // identical frame timings
        c.text = translatedTexts.at(i);
        st.cues.append(c);
    }
    commitCommand(std::make_unique<AddSubtitleTrackCommand>(m_project.get(), st));
    m_subtitles->setProject(m_project.get());
    statusBar()->showMessage(
        arenaTr("panel.ai.apply.translation",
                {{QStringLiteral("count"), QString::number(st.cues.size())},
                 {QStringLiteral("lang"), targetLang}}),
        5000);
}

void MainWindow::applyAiChapters(const QVector<ChapterSuggestion> &chapters)
{
    if (!m_project || chapters.isEmpty()) return;
    QList<Marker> ms;
    for (const ChapterSuggestion &ch : chapters) {
        Marker m;
        m.time = Subtitles::secToFrame(ch.startSec, m_project->fps);
        m.label = ch.title;
        ms.append(m);
    }
    commitCommand(std::make_unique<AddMarkersCommand>(m_project.get(), ms));
    statusBar()->showMessage(
        arenaTr("panel.ai.apply.chapters",
                {{QStringLiteral("count"), QString::number(ms.size())}}),
        5000);
}

void MainWindow::applyAiTitle(const TitleResult &t)
{
    if (!m_project || t.title.trimmed().isEmpty()) return;
    commitCommand(makeRenameProjectCommand(m_project.get(), t.title.trimmed()));
    statusBar()->showMessage(arenaTr("panel.ai.apply.title"), 5000);
}

void MainWindow::applyAiCuts(const QVector<CutSuggestion> &cuts)
{
    if (!m_project || cuts.isEmpty()) return;
    const double fps = m_project->fps;
    // Apply in reverse so the earlier timecodes stay valid after the
    // timeline shortens; each cut is its own undoable command.
    int applied = 0;
    for (int i = cuts.size() - 1; i >= 0; --i) {
        const CutSuggestion c = cuts[i];
        const qint64 s = static_cast<qint64>(qRound(c.startSec * fps));
        const qint64 e = static_cast<qint64>(qRound(c.endSec * fps));
        if (e - s < 1) continue; // sub-frame suggestion
        applyCutRange(s, e);
        ++applied;
    }
    statusBar()->showMessage(
        arenaTr("panel.ai.apply.cuts", {{QStringLiteral("count"), QString::number(applied)}}),
        5000);
}

// M7.3: apply a *validated* NL action list (one undoable command per
// applicable action). `export` is not an edit — it opens the export dialog
// with the preset preselected; the user still confirms the export.
int MainWindow::applyAiActions(const QJsonArray &actions)
{
    if (!m_project || actions.isEmpty()) return 0;
    const double fps = m_project->fps;
    const int ti = primaryVideoTrackIndex();
    const int clipCount = ti >= 0 ? m_project->tracks[ti].clips.size() : 0;
    int applied = 0;
    QString exportPreset;
    for (const QJsonValue &v : actions) {
        const QJsonObject a = v.toObject();
        const QString op = a.value(QStringLiteral("op")).toString();
        if (op == QLatin1String("add_transition") || op == QLatin1String("set_clip_speed")) {
            const QJsonObject target = a.value(QStringLiteral("target")).toObject();
            int idx = static_cast<int>(target.value(QStringLiteral("index")).toDouble(0));
            if (idx == -1) idx = clipCount - 1; // -1 = last clip (prompt contract)
            if (ti < 0 || idx < 0 || idx >= clipCount) continue; // ungroundable target
            const Clip &clip = m_project->tracks[ti].clips[idx];
            if (op == QLatin1String("add_transition")) {
                QJsonObject tr;
                tr.insert(QStringLiteral("type"), a.value(QStringLiteral("transition")).toString());
                // The compositor takes the duration in *frames* (M3 contract).
                const double ms = a.value(QStringLiteral("duration_ms")).toDouble(500);
                tr.insert(QStringLiteral("duration"),
                          qMax(1, static_cast<int>(qRound(ms / 1000.0 * fps))));
                commitCommand(std::make_unique<SetTransitionCommand>(m_project.get(), clip.id, tr));
            } else {
                commitCommand(std::make_unique<SetClipSpeedCommand>(
                    m_project.get(), clip.id, a.value(QStringLiteral("speed")).toDouble(1.0)));
            }
            ++applied;
        } else if (op == QLatin1String("add_marker")) {
            const qint64 frames = static_cast<qint64>(a.value(QStringLiteral("time")).toDouble(0));
            commitCommand(std::make_unique<AddLabeledMarkerCommand>(
                m_project.get(), frames, a.value(QStringLiteral("label")).toString()));
            ++applied;
        } else if (op == QLatin1String("export")) {
            exportPreset = a.value(QStringLiteral("preset")).toString();
        }
    }
    if (!exportPreset.isEmpty()) {
        openExportDialog();
        if (m_exportDialog)
            m_exportDialog->preselectPreset(exportPreset);
    }
    statusBar()->showMessage(
        arenaTr("panel.ai.apply.actions", {{QStringLiteral("count"), QString::number(applied)}}),
        5000);
    return applied;
}

QString MainWindow::describeAiAction(const QJsonObject &action) const
{
    const int ti = primaryVideoTrackIndex();
    const int n = m_project && ti >= 0 ? m_project->tracks[ti].clips.size() : 0;
    const double fps = m_project ? m_project->fps : 30.0;
    return aifeatures::actionDescription(action, fps, n);
}

// --- M4 export ---------------------------------------------------------------

void MainWindow::openExportDialog()
{
    if (!m_project) {
        statusBar()->showMessage(arenaTr("export.noProject"), 3000);
        return;
    }
    if (!Exporter::isAvailable()) {
        statusBar()->showMessage(arenaTr("export.unavailable"), 5000);
        return;
    }
    if (m_exportDialog) {
        m_exportDialog->raise();
        m_exportDialog->activateWindow();
        return;
    }
    // Snapshot everything the worker will read: project data and the
    // per-source fps table. Decoders are stateless per call, so the lambdas
    // below are safe on the export worker thread (never the playback engine).
    const Project snapshot = *m_project;
    const QHash<QString, double> fpsByPath = [this] {
        QHash<QString, double> out;
        for (auto it = m_mediaInfo.cbegin(); it != m_mediaInfo.cend(); ++it)
            if (it->fps > 0.0)
                out.insert(it.key(), it->fps);
        return out;
    }();
    const double projectFps = snapshot.fps;

    ExportJobSource src;
    src.project = snapshot;
    // Stateless: opens its own decoder per call (export must never touch the
    // playback engine or live window state).
    src.fetch = [](const QString &source, qint64 frame) -> QImage {
        QImage img;
        VideoDecoder dec;
        if (dec.open(source)) {
            QImage tmp;
            if (dec.frameAtSec(double(frame) / qMax(0.1, dec.fps()), &tmp))
                img = tmp;
        }
        return img;
    };
    src.audio = [fpsByPath, projectFps](const QString &source, qint64 inFrame, qint64 outFrame,
                                         int rate, int channels, QVector<float> *out) {
        const double fps = fpsByPath.value(source, 0.0);
        const double srcFps = fps > 0.0 ? fps : projectFps;
        const double secIn = double(inFrame) / srcFps;
        const double secOut = double(outFrame) / srcFps;
        AudioReader reader;
        if (!reader.open(source))
            return false;
        reader.setOutput(rate, channels);
        if (!reader.seekSec(secIn))
            return false;
        const int total = int((secOut - secIn) * rate + 0.5);
        out->resize(total * channels);
        int got = 0;
        while (got < total * channels) {
            const int n = reader.readInterleaved(out->data() + got, total * channels - got);
            if (n <= 0)
                break;
            got += n;
        }
        if (got < total * channels)
            out->resize(got); // the export timeline silence-pads the rest
        return got > 0;
    };

    m_exportDialog = new ExportDialog(src, this);
    connect(m_exportDialog, &QObject::destroyed, this, [this] { m_exportDialog = nullptr; });
    m_exportDialog->show();
}

} // namespace arena
