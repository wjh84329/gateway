#ifndef APPLOGGER_H
#define APPLOGGER_H

#include <QList>
#include <QString>

struct LogEntry
{
    QString time;
    QString message;
};

namespace AppLogger {
QString LogDirectoryPath();
QString TodayLogFilePath();
void EnsureLogDirectoryExists();
void MarkSessionStart();
void WriteLog(const QString &message);
QList<LogEntry> LoadTodayLogs();
QList<LogEntry> LoadCurrentSessionLogs();
void ClearTodayLogs();
}

#endif // APPLOGGER_H
