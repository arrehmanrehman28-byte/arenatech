#pragma once
// Logging: category-based, file sink with daily rotation under
// %LOCALAPPDATA%/ArenaEdit/logs on Windows (AppLocalDataLocation elsewhere).
#include <QObject>
#include <QString>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(arenaLog)

namespace arena {

class Logger : public QObject {
    Q_OBJECT
public:
    static void init();              // creates directory + file handler (idempotent)
    static QString logDirectory();   // for the "open log folder" action
    static void crashContext(const QString &message, const QString &details);
};

// Info/warn/error helpers routed to the arena category (and console).
void logInfo(const QString &msg);
void logWarn(const QString &msg);
void logError(const QString &msg, const QString &details = QString());

} // namespace arena
