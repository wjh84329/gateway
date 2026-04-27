#include "machinecode.h"

#include "appconfig.h"

#include <QHostInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QStringList>
#include <QSysInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <WtsApi32.h>
#include <Wbemidl.h>
#endif

namespace {
QString QueryWmiValue(const QString &wql, const QString &propertyName)
{
#ifdef Q_OS_WIN
    HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitialize = SUCCEEDED(initializeResult);
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE) {
        return {};
    }

    HRESULT securityResult = CoInitializeSecurity(nullptr,
                                                  -1,
                                                  nullptr,
                                                  nullptr,
                                                  RPC_C_AUTHN_LEVEL_DEFAULT,
                                                  RPC_C_IMP_LEVEL_IMPERSONATE,
                                                  nullptr,
                                                  EOAC_NONE,
                                                  nullptr);
    Q_UNUSED(securityResult);

    IWbemLocator *locator = nullptr;
    HRESULT locatorResult = CoCreateInstance(CLSID_WbemLocator,
                                             nullptr,
                                             CLSCTX_INPROC_SERVER,
                                             IID_IWbemLocator,
                                             reinterpret_cast<void **>(&locator));
    if (FAILED(locatorResult) || locator == nullptr) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return {};
    }

    IWbemServices *services = nullptr;
    HRESULT connectResult = locator->ConnectServer(BSTR(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    if (FAILED(connectResult) || services == nullptr) {
        locator->Release();
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return {};
    }

    CoSetProxyBlanket(services,
                      RPC_C_AUTHN_WINNT,
                      RPC_C_AUTHZ_NONE,
                      nullptr,
                      RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr,
                      EOAC_NONE);

    IEnumWbemClassObject *enumerator = nullptr;
    HRESULT queryResult = services->ExecQuery(BSTR(L"WQL"),
                                              BSTR(reinterpret_cast<const wchar_t *>(wql.utf16())),
                                              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                              nullptr,
                                              &enumerator);
    if (FAILED(queryResult) || enumerator == nullptr) {
        services->Release();
        locator->Release();
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return {};
    }

    QString value;
    IWbemClassObject *object = nullptr;
    ULONG returnedCount = 0;
    if (SUCCEEDED(enumerator->Next(WBEM_INFINITE, 1, &object, &returnedCount)) && returnedCount == 1 && object != nullptr) {
        VARIANT variantValue;
        VariantInit(&variantValue);
        if (SUCCEEDED(object->Get(reinterpret_cast<LPCWSTR>(propertyName.utf16()), 0, &variantValue, nullptr, nullptr))) {
            if (variantValue.vt == VT_BSTR && variantValue.bstrVal != nullptr) {
                value = QString::fromWCharArray(variantValue.bstrVal).trimmed();
            }
        }
        VariantClear(&variantValue);
        object->Release();
    }

    enumerator->Release();
    services->Release();
    locator->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return value;
#else
    Q_UNUSED(wql);
    Q_UNUSED(propertyName);
    return {};
#endif
}

QString CleanHardwareValue(QString value)
{
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char(' '));
    return value.trimmed();
}

bool IsObviouslyInvalid(const QString &value, int minLength)
{
    if (value.isEmpty() || value.length() < minLength) {
        return true;
    }

    return value.contains(QStringLiteral("ffffff"), Qt::CaseInsensitive)
           || value.contains(QStringLiteral("000000"), Qt::CaseInsensitive)
           || value.contains(QStringLiteral("333333"), Qt::CaseInsensitive);
}
}

MachineCode::MachineCode() = default;

QString MachineCode::GetRNum() const
{
    const QString machineNumber = BuildMachineNumber();
    if (machineNumber.isEmpty()) {
        return SecretKeyPrefix();
    }

    QVector<int> intCode(127);
    for (int index = 1; index < intCode.size(); ++index) {
        intCode[index] = index % 9;
    }

    QString asciiName;
    asciiName.reserve(machineNumber.size());
    for (const QChar &character : machineNumber) {
        const int unicode = character.unicode();
        int encodedValue = unicode;
        if (unicode >= 0 && unicode < intCode.size()) {
            encodedValue += intCode.at(unicode);
        }

        if ((encodedValue >= '0' && encodedValue <= '9')
            || (encodedValue >= 'A' && encodedValue <= 'Z')
            || (encodedValue >= 'a' && encodedValue <= 'z')) {
            asciiName.append(QChar(encodedValue));
        } else if (encodedValue > 'z') {
            asciiName.append(QChar(encodedValue - 10));
        } else {
            asciiName.append(QChar(encodedValue - 9));
        }
    }

    return asciiName + SecretKeyPrefix();
}

QString MachineCode::GetCurrentUserName() const
{
    return GetCurrentUser();
}

QString MachineCode::SecretKeyPrefix() const
{
    const QString secretKey = AppConfig::Load().secretKey;
    return secretKey.left(qMin(8, secretKey.size()));
}

QString MachineCode::GetDiskVolumeSerialNumber() const
{
    QString value = CleanHardwareValue(QueryWmiValue(QStringLiteral("SELECT VolumeSerialNumber FROM Win32_LogicalDisk WHERE DeviceID='C:'"),
                                                     QStringLiteral("VolumeSerialNumber")));
    if (!value.isEmpty()) {
        return value;
    }

#ifdef Q_OS_WIN
    DWORD serialNumber = 0;
    if (GetVolumeInformationW(L"C:\\", nullptr, 0, &serialNumber, nullptr, nullptr, nullptr, 0)) {
        return QString::number(static_cast<qulonglong>(serialNumber), 16).toUpper();
    }
#endif

    return QStringLiteral("DISK0000");
}

QString MachineCode::GetCpu() const
{
    QString value = CleanHardwareValue(QueryWmiValue(QStringLiteral("SELECT ProcessorId FROM Win32_Processor"),
                                                     QStringLiteral("ProcessorId")));
    if (!value.isEmpty()) {
        return value;
    }

    value = GetBiosSerialNumber();
    if (!value.isEmpty()) {
        return value;
    }

    value = GetUuid();
    if (!value.isEmpty()) {
        return value;
    }

    value = GetLocalIp();
    if (!value.isEmpty()) {
        return value;
    }

    return QSysInfo::machineUniqueId().isEmpty() ? QStringLiteral("CPU00000") : QString::fromLatin1(QSysInfo::machineUniqueId());
}

QString MachineCode::GetBiosSerialNumber() const
{
    return CleanHardwareValue(QueryWmiValue(QStringLiteral("SELECT SerialNumber FROM Win32_BIOS"),
                                            QStringLiteral("SerialNumber")));
}

QString MachineCode::GetUuid() const
{
    QString value = CleanHardwareValue(QueryWmiValue(QStringLiteral("SELECT UUID FROM Win32_ComputerSystemProduct"),
                                                     QStringLiteral("UUID")));
    if (value.compare(QStringLiteral("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"), Qt::CaseInsensitive) == 0) {
        value.clear();
    }

    if (!value.isEmpty()) {
        return value;
    }

    return CleanHardwareValue(GetLocalIp());
}

QString MachineCode::GetLocalIp() const
{
    const auto addresses = QNetworkInterface::allAddresses();
    for (const auto &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            QString value = address.toString();
            value.remove(QLatin1Char('.'));
            return value;
        }
    }

    return QStringLiteral("127001");
}

QString MachineCode::GetMacAddress() const
{
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &networkInterface : interfaces) {
        if (!(networkInterface.flags() & QNetworkInterface::IsUp)) {
            continue;
        }
        const QString hardwareAddress = CleanHardwareValue(networkInterface.hardwareAddress());
        if (!hardwareAddress.isEmpty()) {
            return hardwareAddress;
        }
    }

    return QStringLiteral("MAC00000");
}

QString MachineCode::GetCurrentUser() const
{
#ifdef Q_OS_WIN
    LPSTR buffer = nullptr;
    DWORD bytesReturned = 0;
    if (WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE,
                                    WTS_CURRENT_SESSION,
                                    WTSUserName,
                                    &buffer,
                                    &bytesReturned)
        && buffer != nullptr
        && bytesReturned > 1) {
        const QString userName = QString::fromLocal8Bit(buffer).trimmed();
        WTSFreeMemory(buffer);
        if (!userName.isEmpty()) {
            return userName;
        }
    }
#endif

    const QString userName = qEnvironmentVariable("USERNAME").trimmed();
    return userName.isEmpty() ? QStringLiteral("SYSTEM") : userName;
}

QString MachineCode::BuildMachineNumber() const
{
    const QString cpu = GetCpu();
    const QString disk = GetDiskVolumeSerialNumber();
    const QString uuid = GetUuid();

    const bool isInvalid = IsObviouslyInvalid(cpu, 4)
                           || IsObviouslyInvalid(disk, 4)
                           || IsObviouslyInvalid(uuid, 8);

    if (isInvalid) {
        const QString ip = GetLocalIp();
        const QString host = QHostInfo::localHostName();
        const QString mac = GetMacAddress();
        const QString user = GetCurrentUser();
        return cpu.left(8) + disk.left(8) + uuid.left(8) + ip.left(8) + host.left(8) + user + mac.left(8);
    }

    return cpu.left(16) + disk.left(8) + uuid.left(8);
}
