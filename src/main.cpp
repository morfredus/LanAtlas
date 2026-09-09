#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "core/Inventory.h"

#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleHints>
#include <morfbeacon/PresenceService.h>
#include <morfbeacon/IMetricsProvider.h>
#include <morfupdate/UpdateDialog.h>
#include <QJsonObject>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LanAtlas"));
    app.setOrganizationName(QStringLiteral("morfredus"));
    app.setApplicationVersion(QStringLiteral(LA_APP_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/lanatlas.png")));

    // Detection du theme sombre. colorScheme() n'existe qu'a partir de Qt 6.5 ;
    // le Qt systeme de Debian/WSL (6.4) ne l'a pas. On garde donc l'API recente
    // derriere un test de version, avec un repli portable base sur la luminosite
    // du fond de la palette.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const bool dark = app.styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    const bool dark = app.palette().color(QPalette::Window).lightness() < 128;
#endif
    app.setStyleSheet(Theme::stylesheet(dark));

    MainWindow window;
    window.show();

    morfbeacon::PresenceConfig beaconCfg;
    beaconCfg.appName = QStringLiteral("LanAtlas");
    beaconCfg.version = QStringLiteral(LA_APP_VERSION);
    beaconCfg.statusPort = 8883;
    beaconCfg.capabilities = QStringList{QStringLiteral("network_map")};
    beaconCfg.api = {
        {QStringLiteral("GET"), QStringLiteral("/status"), QStringLiteral("etat et resume d'inventaire")},
        {QStringLiteral("GET"), QStringLiteral("/healthz"), QStringLiteral("sonde de vie")},
    };

    morfbeacon::FunctionMetricsProvider beaconMetrics([&window]() {
        QJsonObject m;
        const auto devices = window.inventory()->devices();
        int online = 0;
        int infra = 0;
        for (const auto& d : devices) {
            if (d.online)
                ++online;
            if (d.infrastructure)
                ++infra;
        }
        m["device_count"] = devices.size();
        m["online_count"] = online;
        m["infrastructure_count"] = infra;
        m["livebox"] = window.inventory()->liveboxSummary;
        m["deco"] = window.inventory()->decoSummary;
        m["wan_ipv4"] = window.inventory()->wanIpv4;
        return m;
    });

    morfbeacon::PresenceService presence(beaconCfg, &beaconMetrics);
    presence.start();

    morfupdate::morfUpdateConfig upd;
    upd.owner = QStringLiteral("morfredus");
    upd.repo = QStringLiteral("LanAtlas");
    upd.currentVersion = QStringLiteral(LA_APP_VERSION);
    morfupdate::checkAndNotify(&window, QStringLiteral("LanAtlas"), upd, true);

    return app.exec();
}
