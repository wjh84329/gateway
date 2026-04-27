#include "applogger.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
QString g_sessionStartTime;

QString CurrentTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}
}

QString AppLogger::LogDirectoryPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/log");
}

QString AppLogger::TodayLogFilePath()
{
    return LogDirectoryPath() + QStringLiteral("/") + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".log");
}

void AppLogger::EnsureLogDirectoryExists()
{
    QDir().mkpath(LogDirectoryPath());
}

void AppLogger::MarkSessionStart()
{
    g_sessionStartTime = CurrentTimestamp();
}

void AppLogger::WriteLog(const QString &message)
{
    EnsureLogDirectoryExists();

    QFile file(TodayLogFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    const QString line = CurrentTimestamp() + QStringLiteral("\t") + message + QStringLiteral("\n");
    file.write(line.toUtf8());
    file.close();
}

QList<LogEntry> AppLogger::LoadTodayLogs()
{
    QList<LogEntry> logs;

    QFile file(TodayLogFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return logs;
    }

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const int separatorIndex = line.indexOf(QLatin1Char('\t'));
        if (separatorIndex < 0) {
            logs.append({QString(), line});
            continue;
        }

        logs.append({line.left(separatorIndex), line.mid(separatorIndex + 1)});
    }

    return logs;
}

QList<LogEntry> AppLogger::LoadCurrentSessionLogs()
{
    const auto logs = LoadTodayLogs();
    if (g_sessionStartTime.isEmpty()) {
        return logs;
    }

    QList<LogEntry> sessionLogs;
    for (const auto &log : logs) {
        if (log.time >= g_sessionStartTime) {
            sessionLogs.append(log);
        }
    }

    return sessionLogs;
}

void AppLogger::ClearTodayLogs()
{
    EnsureLogDirectoryExists();
}
