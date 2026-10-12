#include "mainwindow.h"
#include "panels.h"
#include "timeline.h"
#include "aipanel.h"
#include "dialogs.h"
#include "themer.h"
#include "settings.h"
#include "i18n.h"
#include "autosave.h"
#include "util.h"
#include "kilogateway.h"
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
#include <QWidget>

namespace arena {

MainWindow::MainWindow(Themer *themer, QWidget *parent)
    : QMainWindow(parent), m_themer(themer), m_settings(&Settings::instance()),
      m_kilo(new KiloClient(*m_settings, this)),
      m_autosave(new Autosave(this))
{
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
    connect(m_undoAct, &QAction::triggered, this, [this] { m_undoStack.undo(); refreshUndoButtons(); });
    connect(m_redoAct, &QAction::triggered, this, [this] { m_undoStack.redo(); refreshUndoButtons(); });

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
    connect(exportAct, &QAction::triggered, this, [this] {
        statusBar()->showMessage(arenaTr("export.phaseNote"), 5000);
    });

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
    auto *guides = new QCheckBox(arenaTr("panel.monitor.guides"));
    guides->setChecked(true);
    auto *grid = new QCheckBox(arenaTr("panel.monitor.grid"));
    auto *thirds = new QCheckBox(arenaTr("panel.monitor.thirds"));
    m_monitor = new PreviewSurface;
    connect(guides, &QCheckBox::toggled, m_monitor, &PreviewSurface::setShowGuides);
    connect(grid, &QCheckBox::toggled, m_monitor, &PreviewSurface::setShowGrid);
    connect(thirds, &QCheckBox::toggled, m_monitor, &PreviewSurface::setShowThirds);

    // Virtual transport (playhead) until real decode lands in M1.
    auto advance = [this](int dir) {
        if (!m_project) return;
        m_playhead += dir;
        m_timecode->setText(framesToTimecode(m_playhead, m_project->fps));
    };
    connect(play, &QToolButton::clicked, this, [this, advance] {
        m_transportSpeed = (m_transportSpeed >= 0) ? 1 : 2;
        m_transportTimer.start(qMax(1, int(1000.0 / (m_project ? m_project->fps : 30.0))));
    });
    connect(back, &QToolButton::clicked, this, [this, advance] { m_transportSpeed = -2; (void)advance; });
    connect(fwd, &QToolButton::clicked, this, [this, advance] { m_transportSpeed = 2; (void)advance; });
    connect(stepBack, &QToolButton::clicked, this, [this, advance] { m_transportTimer.stop(); advance(-1); });
    connect(stepFwd, &QToolButton::clicked, this, [this, advance] { m_transportTimer.stop(); advance(1); });
    m_transportTimer.setInterval(33);
    connect(&m_transportTimer, &QTimer::timeout, this, [this, advance] {
        if (!m_project) { m_transportTimer.stop(); return; }
        if (m_transportSpeed > 0) advance(m_transportSpeed);
        else if (m_transportSpeed < 0) advance(m_transportSpeed);
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
    connect(m_mediaBin, &MediaBinWidget::itemDoubleClicked, this, [this](const QString &path) {
        m_monitorNote = arenaTr("media.previewPhase") + QStringLiteral(" — ") + path;
    });

    m_inspector = new InspectorWidget;
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

    m_subtitles = new SubtitlesWidget;
    auto *subDock = new QDockWidget;
    subDock->setObjectName(QStringLiteral("SubtitlesDock"));
    subDock->setWidget(m_subtitles);
    addDockWidget(Qt::BottomDockWidgetArea, subDock);
    connect(m_subtitles, &SubtitlesWidget::trackAdded, this, [this](const QString &name, const QString &bcp47) {
        if (!m_project) return;
        SubtitleTrack t;
        t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        t.name = name;
        t.language = bcp47;
        m_project->subtitleTracks.append(t);
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
    m_aiPanel->askText(text);
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
    add(QStringLiteral("cmd.undo"), QStringLiteral("Ctrl+Z"), [this] { m_undoStack.undo(); refreshUndoButtons(); });
    add(QStringLiteral("cmd.redo"), QStringLiteral("Ctrl+Y"), [this] { m_undoStack.redo(); refreshUndoButtons(); });
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
    m_subtitles->clearTracks();
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
    m_subtitles->clearTracks();
    for (const auto &st : p.subtitleTracks)
        m_subtitles->addTrack(st.name, st.language);
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
    m_subtitles->clearTracks();
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
        m_monitor->setAspect(m_project->width, m_project->height);
        m_fps->setText(arenaTr("status.fps", {{QStringLiteral("fps"), QString::number(m_project->fps, 'g', 3)}}));
        m_inspector->setProjectName(m_project->name, m_project->fps, m_project->width, m_project->height);
    } else {
        m_timeline->setProject(false, 30);
        m_inspector->clearSelection();
        m_fps->setText(arenaTr("status.fps", {{QStringLiteral("fps"), QStringLiteral("30")}}));
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
    if (m_project) refreshProjectViews();
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

void MainWindow::tickSecond()
{
    m_mem->setText(arenaTr("status.mem", {{QStringLiteral("size"), formatBytes(processResidentBytes())}}));
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

} // namespace arena
