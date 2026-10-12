#include "logger.h"
#include <QDate>
#include <qlogging.h>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>

Q_LOGGING_CATEGORY(arenaLog, "arena")

namespace arena {

static QMutex s_fileMutex;

QString Logger::logDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    // Windows: %LOCALAPPDATA%/ArenaEdit/ArenaEdit ; keep the spec path shape.
    return QDir(base).filePath(QStringLiteral("logs"));
}

static QFile *openLogFile()
{
    const QString dir = Logger::logDirectory();
    QDir().mkpath(dir);
    const QString name = QStringLiteral("arena-%1.log").arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
    QFile *f = new QFile(QDir(dir).filePath(name));
    if (!f->open(QIODevice::Append | QIODevice::Text)) {
        delete f;
        return nullptr;
    }
    return f;
}

static void fileSink(const QString &line)
{
    QMutexLocker lock(&s_fileMutex);
    QFile *f = openLogFile();
    if (!f) return;
    QTextStream ts(f);
    ts << line << '\n';
    ts.flush();
    f->close();
    delete f;
}

void Logger::init()
{
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        const QString line = QStringLiteral("[%1] [%2] %3")
                                 .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                                 .arg(ctx.category ? QString::fromUtf8(ctx.category) : QStringLiteral("default"))
                                 .arg(msg);
        fileSink(line);
        // Also keep console output for debugging.
        const QByteArray bytes = (line + '\n').toUtf8();
        if (type == QtCriticalMsg || type == QtFatalMsg)
            fwrite(bytes.constData(), 1, bytes.size(), stderr);
        else
            fwrite(bytes.constData(), 1, bytes.size(), stdout);
    });
    qCDebug(arenaLog) << "Logger initialized, log dir:" << Logger::logDirectory();
}

void Logger::crashContext(const QString &message, const QString &details)
{
    const QString line = QStringLiteral("=== CRASH CONTEXT %1 ===\n%2\n%s\n===================\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), message, details.toUtf8());
    fileSink(line);
    qCWarning(arenaLog) << message << details;
}

void logInfo(const QString &msg) { qCInfo(arenaLog).noquote() << msg; }
void logWarn(const QString &msg) { qCWarning(arenaLog).noquote() << msg; }
void logError(const QString &msg, const QString &details)
{
    qCWarning(arenaLog).noquote() << msg;
    if (!details.isEmpty()) qCWarning(arenaLog).noquote() << details;
}

} // namespace arena
