#include "mainwindow.h"

#include "filemonitorservice.h"
#include "gatewayapiclient.h"
#include "gatewayhttpserver.h"
#include "gatewaysingleinstance.h"
#include "rabbitmqservice.h"
#include "startupservice.h"
#include "startupsplash.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setQuitOnLastWindowClosed(false);
    a.setWindowIcon(MainWindow::ApplicationIcon());

    QObject::connect(&a, &QCoreApplication::aboutToQuit, [] {
        QString offlineErr;
        GatewayApiClient().SetGatewayEndpointOffline(&offlineErr);
        Q_UNUSED(offlineErr);
        GatewayHttpServer::instance().stop();
        FileMonitorService::Instance().Stop();
        RabbitMqService::StopListening();
        GatewaySingleInstance::instance().shutdownForAppExit();
    });

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "cs_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    if (!GatewaySingleInstance::instance().tryAcquirePrimaryOrNotifyExisting()) {
        return 0;
    }

    StartupSplash splash;
    splash.show();
    splash.pumpEvents();

    if (!StartupService::RunStartupSequence(&splash)) {
        return 0;
    }

    MainWindow w;
    GatewaySingleInstance::instance().setMainWindow(&w);
    splash.fadeOutAndThen([&w]() {
        w.show();
        w.raise();
        w.activateWindow();
    });
    return QCoreApplication::exec();
}
