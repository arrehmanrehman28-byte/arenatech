// Core: project model, commands/undo, settings, autosave, timecode.
#include <gtest/gtest.h>
#include "project.h"
#include "command.h"
#include "settings.h"
#include "autosave.h"
#include "util.h"
#include <QTemporaryDir>
#include <QFile>
#include <QThread>
#include <utime.h>

using namespace arena;

TEST(ProjectModel, JsonRoundTrip)
{
    Project p;
    p.id = "test-id";
    p.name = "Round Trip";
    p.fps = 25.0;
    p.width = 3840;
    p.height = 2160;
    p.aspect = "16:9";
    p.sampleRate = 96000;
    Track t;
    t.id = "V1";
    t.type = TrackType::Video;
    t.name = "V1";
    Clip c;
    c.id = "c1";
    c.sourceRef = "clip.mp4";
    c.in = 100;
    c.out = 250;
    c.offset = 50;
    c.speed = 2.0;
    t.clips.append(c);
    p.tracks.append(t);
    SubtitleTrack s;
    s.id = "s1";
    s.language = "ur";
    s.name = "Urdu subs";
    p.subtitleTracks.append(s);
    Marker m;
    m.time = 10;
    m.label = "Intro";
    p.markers.append(m);

    Project q = Project::fromJson(p.toJson());
    EXPECT_EQ(q.id, p.id);
    EXPECT_EQ(q.name, p.name);
    EXPECT_EQ(q.fps, 25.0);
    EXPECT_EQ(q.width, 3840);
    EXPECT_EQ(q.tracks.size(), 1);
    EXPECT_EQ(q.tracks[0].clips.size(), 1);
    EXPECT_EQ(q.tracks[0].clips[0].in, 100);
    EXPECT_EQ(q.tracks[0].clips[0].out, 250);
    EXPECT_EQ(q.tracks[0].clips[0].speed, 2.0);
    EXPECT_EQ(q.subtitleTracks[0].language, "ur");
    EXPECT_EQ(q.markers[0].label, "Intro");
}

TEST(ProjectModel, FileSaveLoad)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = tmp.filePath("t.aep");
    Project p;
    p.id = "id1";
    p.name = "Saved";
    ASSERT_TRUE(p.saveFile(path, nullptr));
    QString err;
    Project q = Project::loadFile(path, &err);
    EXPECT_EQ(q.id, "id1");
    EXPECT_EQ(q.name, "Saved");
    EXPECT_EQ(err, QString());
    // Corrupt file is reported, not crashed on.
    QFile(path).remove();
    {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("{ not json ");
    }
    Project bad = Project::loadFile(path, &err);
    EXPECT_TRUE(bad.id.isEmpty());
    EXPECT_FALSE(err.isEmpty());
}

TEST(ProjectModel, SchemaVersionGate)
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath("future.aep");
    {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(R"({"schemaVersion": 99, "id": "x", "name": "x"})");
    }
    QString err;
    Project p = Project::loadFile(path, &err);
    EXPECT_TRUE(p.id.isEmpty());
    EXPECT_NE(err.indexOf("newer schema"), -1);
}

TEST(UndoStack, RenameCommandRoundTrip)
{
    auto project = std::make_unique<Project>();
    project->name = "Before";
    UndoStack stack;
    stack.push(makeRenameProjectCommand(project.get(), "After"));
    EXPECT_EQ(project->name, "After");
    EXPECT_EQ(stack.undoActionName(), "Rename project");
    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(project->name, "Before");
    EXPECT_EQ(stack.redoActionName(), "Rename project");
    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(project->name, "After");
    // New push clears the redo branch.
    stack.push(makeRenameProjectCommand(project.get(), "Third"));
    EXPECT_FALSE(stack.canRedo());
    EXPECT_EQ(stack.undoDepth(), 2);
}

TEST(UndoStack, MemoryCapEvictsOldest)
{
    UndoStack stack(/*maxCommands=*/3, /*maxBytes=*/1 << 30);
    for (auto project = std::make_unique<Project>(); ; ) {
        stack.push(makeRenameProjectCommand(project.get(), "x"));
        if (stack.undoDepth() >= 3) break;
    }
    // Push one more; oldest must be evicted to hold maxCommands.
    auto p2 = std::make_unique<Project>();
    stack.push(makeRenameProjectCommand(p2.get(), "y"));
    EXPECT_LE(stack.undoDepth(), 3);
}


// M2 exit criterion: every edit is a command; 100 steps undo/redo round-trip.
TEST(UndoStack, HundredStepRoundTrip)
{
    auto project = std::make_unique<Project>();
    Track track;
    track.id = QStringLiteral("V1");
    track.type = TrackType::Video;
    track.name = QStringLiteral("V1");
    project->tracks.append(track);
    project->name = QStringLiteral("P");

    UndoStack stack;
    stack.onChanged = nullptr;
    // 100 distinct, invertible edits (rename + marker adds/undos interleaved
    // would be app-level; rename is the pure-core invertible op).
    for (int i = 0; i < 100; ++i)
        stack.push(makeRenameProjectCommand(project.get(), QStringLiteral("S%1").arg(i)));
    EXPECT_EQ(project->name, QStringLiteral("S99"));
    EXPECT_EQ(stack.undoDepth(), 100);

    // Undo all the way back.
    for (int i = 99; i >= 0; --i) {
        ASSERT_TRUE(stack.undo()) << "undo failed at step " << i;
        if (i == 0)
            EXPECT_EQ(project->name, QStringLiteral("P"));
        else
            EXPECT_EQ(project->name, QStringLiteral("S%1").arg(i - 1));
    }
    EXPECT_FALSE(stack.canUndo());
    EXPECT_EQ(stack.redoDepth(), 100);

    // Redo all the way forward.
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(stack.redo()) << "redo failed at step " << i;
        EXPECT_EQ(project->name, QStringLiteral("S%1").arg(i));
    }
    EXPECT_FALSE(stack.canRedo());
    EXPECT_EQ(project->name, QStringLiteral("S99"));
}

TEST(UndoStack, ChangedCallbackFires)
{
    auto project = std::make_unique<Project>();
    project->name = QStringLiteral("A");
    UndoStack stack;
    int calls = 0;
    stack.onChanged = [&calls] { ++calls; };
    stack.push(makeRenameProjectCommand(project.get(), QStringLiteral("B")));
    EXPECT_EQ(calls, 1);
    stack.undo();
    EXPECT_EQ(calls, 2);
    stack.redo();
    EXPECT_EQ(calls, 3);
    stack.clear();
    EXPECT_EQ(calls, 4);
    EXPECT_FALSE(stack.canUndo());
}

TEST(Settings, DefaultsAreConservative)
{
    // AI must be OFF by default; core never depends on AI.
    EXPECT_FALSE(Settings::instance().aiEnabled());
    EXPECT_TRUE(Settings::instance().snapEnabled());
    EXPECT_EQ(Settings::instance().autosaveSeconds(), 60);
    EXPECT_EQ(Settings::instance().aiHourlyBudget(), 200);
    EXPECT_EQ(Settings::instance().theme(), "dark");
    EXPECT_EQ(Settings::instance().accentColor().name(QColor::HexRgb), "#4c8dff");
}

TEST(Timecode, FramesToSmpteAndBack)
{
    EXPECT_EQ(framesToTimecode(0, 30), "00:00:00:00");
    EXPECT_EQ(framesToTimecode(30, 30), "00:00:01:00");
    EXPECT_EQ(framesToTimecode(29, 30), "00:00:00:29");
    EXPECT_EQ(framesToTimecode(30 * 3600 + 15 * 30, 30), "01:00:15:00");
    EXPECT_EQ(timecodeToFrames("01:00:15:00", 30), 30 * 3600 + 15 * 30);
    EXPECT_EQ(timecodeToFrames("00:00:00:05", 25), 5);
    // 25 fps: frame 25 = 1 second.
    EXPECT_EQ(framesToTimecode(25, 25), "00:00:01:00");
}

TEST(Autosave, RecoveryFlow)
{
    QTemporaryDir tmp;
    const QString project = tmp.filePath("p.aep");
    const QString autoFile = project + ".autosave";
    {
        QFile f(project);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("{}");
    }
    // Clean exit: no recovery.
    Autosave::markCleanExit(project);
    EXPECT_EQ(Autosave::pendingRecovery(project), QString());
    QFile::remove(project + ".clean");
    // Autosave strictly newer than project -> recovery pending.
    {
        QFile f(autoFile);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("{}");
    }
    // POSIX utime (tests run on the Linux CI harness; Windows uses the same
    // recovery logic): project clearly in the past, autosave "now".
    {
        struct utimbuf ub;
        ub.modtime = ub.actime = time(nullptr) - 3600;
        ASSERT_EQ(utime(project.toUtf8().constData(), &ub), 0);
    }
    {
        struct utimbuf ub;
        ub.modtime = ub.actime = time(nullptr);
        ASSERT_EQ(utime(autoFile.toUtf8().constData(), &ub), 0);
    }
    EXPECT_EQ(Autosave::pendingRecovery(project), autoFile);
    Autosave::clearRecovery(project);
    EXPECT_EQ(Autosave::pendingRecovery(project), QString());
    EXPECT_TRUE(QFile::exists(project + ".clean"));
}
