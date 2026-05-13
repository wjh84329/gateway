#ifndef GATEWAYHTTPSERVER_H
#define GATEWAYHTTPSERVER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

/// 网关进程对外 HTTP 监听（健康检查等），绑定配置项 AppSettings/Port。
class GatewayHttpServer final : public QObject
{
public:
    static GatewayHttpServer &instance();

    bool start(quint16 port, QString *errorMessage = nullptr);
    void stop();
    bool isListening() const;
    quint16 serverPort() const;

    /// 在 <c>start</c> 刚返回 <c>false</c> 后调用，判断是否为「地址/端口已被占用」。
    bool listenErrorIsAddressInUse() const;

private:
    explicit GatewayHttpServer(QObject *parent = nullptr);
    GatewayHttpServer(const GatewayHttpServer &) = delete;
    GatewayHttpServer &operator=(const GatewayHttpServer &) = delete;

    void onNewConnection();
    void onSocketReadyRead(QTcpSocket *socket);
    void forgetSocket(QTcpSocket *socket);

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_requestBuffers;
};

#endif // GATEWAYHTTPSERVER_H
