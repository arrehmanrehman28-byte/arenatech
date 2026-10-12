#include "autosave.h"
#include "logger.h"
#include <QFile>
#include <QFileInfo>

namespace arena {

Autosave::Autosave(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &Autosave::tick);
    m_timer.start(60 * 1000);
}

void Autosave::setProjectPath(const QString &projectPath)
{
    m_path = projectPath;
    m_dirty = false;
    m_lastSaved = QDateTime();
    if (projectPath.isEmpty())
        m_timer.stop();
    else
        m_timer.start(60 * 1000);
}

void Autosave::setProjectChanged()
{
    m_dirty = true;
}

void Autosave::setSnapshotFn(SnapshotFn fn) { m_snapshot = std::move(fn); }
void Autosave::setInterval(int seconds)
{
    m_timer.setInterval(qMax(5, seconds) * 1000);
}

void Autosave::tick()
{
    if (!m_dirty || m_path.isEmpty() || !m_snapshot) return;
    if (m_snapshot(m_autosavePath())) {
        m_dirty = false;
        m_lastSaved = QDateTime::currentDateTime();
        logInfo(QStringLiteral("Autosaved %1").arg(m_autosavePath()));
        emit projectSaved();
    } else {
        logError(QStringLiteral("Autosave failed"), m_autosavePath());
    }
}

QString Autosave::lastSavedString() const
{
    if (!m_lastSaved.isValid()) return QString();
    const qint64 secs = m_lastSaved.secsTo(QDateTime::currentDateTime());
    if (secs < 5) return QStringLiteral("just now");
    if (secs < 60) return QStringLiteral("%1 s ago").arg(secs);
    return QStringLiteral("%1 min ago").arg(secs / 60);
}

QString Autosave::pendingRecovery(const QString &projectPath)
{
    if (projectPath.isEmpty()) return QString();
    const QString marker = projectPath + QStringLiteral(".clean");
    if (QFile::exists(marker)) return QString(); // clean exit: nothing to do
    const QString autoPath = projectPath + QStringLiteral(".autosave");
    const QFileInfo a(autoPath), p(projectPath);
    if (!a.exists() || !a.lastModified().isValid()) return QString();
    if (p.exists() && p.lastModified() >= a.lastModified()) return QString();
    return autoPath;
}

void Autosave::clearRecovery(const QString &projectPath)
{
    QFile::remove(projectPath + QStringLiteral(".autosave"));
    markCleanExit(projectPath);
}

void Autosave::markCleanExit(const QString &projectPath)
{
    if (projectPath.isEmpty()) return;
    QFile f(projectPath + QStringLiteral(".clean"));
    if (f.open(QIODevice::WriteOnly)) f.write("1");
}

} // namespace arena
