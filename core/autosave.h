#pragma once
// Autosave + crash recovery.
// - Autosaves the project every N seconds (default 60) to <project>.autosave
// - Writes a clean-exit marker on shutdown
// - On launch, restoreProject() reports whether a recoverable autosave exists
#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <functional>

namespace arena {

struct Project;

class Autosave : public QObject {
    Q_OBJECT
public:
    using SnapshotFn = std::function<bool(const QString &path)>; // save project to path

    explicit Autosave(QObject *parent = nullptr);

    void setProjectPath(const QString &projectPath); // "" disables
    void setProjectChanged();                        // called after any edit
    void setSnapshotFn(SnapshotFn fn);
    void setInterval(int seconds);
    QString projectPath() const { return m_path; }
    QDateTime lastSaved() const { return m_lastSaved; }
    QString lastSavedString() const; // "12 s ago" (caller translates via I18n)

    // Launch-time recovery: returns autosave path if it is newer than the
    // project file and no clean exit marker is present.
    static QString pendingRecovery(const QString &projectPath);
    // Called after a successful manual restore or explicit "discard".
    static void clearRecovery(const QString &projectPath);
    static void markCleanExit(const QString &projectPath);

signals:
    void projectSaved();

private:
    void tick();
    QTimer m_timer;
    QString m_path;
    QString m_autosavePath() const { return m_path + QStringLiteral(".autosave"); }
    SnapshotFn m_snapshot;
    bool m_dirty = false;
    QDateTime m_lastSaved;
};

} // namespace arena
