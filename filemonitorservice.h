#ifndef FILEMONITORSERVICE_H
#define FILEMONITORSERVICE_H

#include "appconfig.h"

class FileMonitorService
{
public:
    static FileMonitorService &Instance();

    void Initialize(const AppConfigValues &config);
    void Stop();
    void SuppressNextChange(const QString &path);

private:
    FileMonitorService();
    FileMonitorService(const FileMonitorService &) = delete;
    FileMonitorService &operator=(const FileMonitorService &) = delete;
};

#endif // FILEMONITORSERVICE_H
