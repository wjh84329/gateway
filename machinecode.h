#ifndef MACHINECODE_H
#define MACHINECODE_H

#include <QString>

class MachineCode
{
public:
    MachineCode();
    QString GetRNum() const;
    QString GetCurrentUserName() const;

private:
    QString SecretKeyPrefix() const;
    QString GetDiskVolumeSerialNumber() const;
    QString GetCpu() const;
    QString GetBiosSerialNumber() const;
    QString GetUuid() const;
    QString GetLocalIp() const;
    QString GetMacAddress() const;
    QString GetCurrentUser() const;
    QString BuildMachineNumber() const;
};

#endif // MACHINECODE_H
