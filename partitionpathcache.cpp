#include "partitionpathcache.h"

#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

namespace {
QMutex g_mutex;
QHash<int, QString> g_idToScriptPath;

QString ReadStringField(const QJsonObject &object, const QStringList &names)
{
    for (const auto &name : names) {
        const QString value = object.value(name).toVariant().toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}
}

void PartitionPathCache::UpdateFromPartitionsArray(const QJsonArray &partitions)
{
    QMutexLocker lock(&g_mutex);
    g_idToScriptPath.clear();
    for (const QJsonValue &v : partitions) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject p = v.toObject();
        bool ok = false;
        const int id = ReadStringField(p, {QStringLiteral("Id"), QStringLiteral("id")}).toInt(&ok);
        if (!ok || id <= 0) {
            continue;
        }
        const QString path
            = ReadStringField(p,
                              {QStringLiteral("ScriptPath"),
                               QStringLiteral("scriptPath"),
                               QStringLiteral("PartitionPath"),
                               QStringLiteral("partitionPath")});
        if (!path.isEmpty()) {
            g_idToScriptPath.insert(id, QDir::fromNativeSeparators(path));
        }
    }
}

QString PartitionPathCache::ScriptPathForPartition(int partitionId)
{
    if (partitionId <= 0) {
        return {};
    }
    QMutexLocker lock(&g_mutex);
    return g_idToScriptPath.value(partitionId);
}

QString PartitionPathCache::RootPathForPartition(int partitionId)
{
    if (partitionId <= 0) {
        return {};
    }
    QString script;
    {
        QMutexLocker lock(&g_mutex);
        script = g_idToScriptPath.value(partitionId);
    }
    if (script.isEmpty()) {
        return {};
    }
    return QDir::fromNativeSeparators(QDir(script).rootPath());
}

QStringList PartitionPathCache::AllScriptVolumeRoots()
{
    QMutexLocker lock(&g_mutex);
    QStringList roots;
    for (auto it = g_idToScriptPath.constBegin(); it != g_idToScriptPath.constEnd(); ++it) {
        const QString r = QDir::fromNativeSeparators(QDir(it.value()).rootPath());
        if (!r.isEmpty() && !roots.contains(r, Qt::CaseInsensitive)) {
            roots.append(r);
        }
    }
    return roots;
}
