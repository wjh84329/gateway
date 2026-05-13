#ifndef PARTITIONPATHCACHE_H
#define PARTITIONPATHCACHE_H

#include <QJsonArray>
#include <QString>
#include <QStringList>

/// 缓存分区 Id → ScriptPath（安装路径），供二维码与文件监控按「分区脚本安装目录」推导卷根。
namespace PartitionPathCache {
void UpdateFromPartitionsArray(const QJsonArray &partitions);
QString ScriptPathForPartition(int partitionId);
/// 安装路径所在卷根，如 D:/；未知返回空。
QString RootPathForPartition(int partitionId);
/// 所有已缓存分区脚本路径对应的卷根（去重），用于注册「平台验证」下各监控路径。
QStringList AllScriptVolumeRoots();
}

#endif
