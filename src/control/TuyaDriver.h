#pragma once

#include "control/IDeviceDriver.h"
#include <QString>
#include <QByteArray>

// Pilotage d'un appareil Tuya en protocole LAN 3.5. Traduit l'etat generique
// (DeviceControlState) vers les datapoints Tuya de la bande LED (LSC 10M) :
//   20 marche/arret, 21 mode (white/colour), 22 luminosite, 23 temperature,
//   24 couleur (chaine hex teinte/saturation/valeur).
// Chaque operation ouvre une connexion courte : l'appareil pousse des mises a
// jour incrementales sur une connexion persistante, ce qui brouillerait la
// lecture. Une connexion par action est simple et fiable.
class TuyaDriver : public IDeviceDriver {
public:
    TuyaDriver(QString host, QByteArray deviceId, QByteArray localKey);

    DeviceControlState query() override;
    bool setPower(bool on) override;
    bool setBrightness(int raw) override;
    bool setColorTemp(int raw) override;
    bool setColorHsv(int hue, int sat, int val) override;
    bool setWhiteMode() override;
    QString error() const override { return error_; }

    // Encodage Tuya de la couleur : teinte(0-360) sat(0-1000) val(0-1000) en
    // trois groupes de 4 chiffres hexadecimaux (HHHHSSSSVVVV).
    static QString hsvToHex(int hue, int sat, int val);
    static bool hexToHsv(const QString& hex, int& hue, int& sat, int& val);

private:
    bool applyDps(const class QJsonObject& dps);

    QString    host_;
    QByteArray deviceId_;
    QByteArray localKey_;
    QString    error_;
};
