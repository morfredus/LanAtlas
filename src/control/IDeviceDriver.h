#pragma once

#include "control/DeviceControl.h"
#include <QString>

// Interface generique de pilotage d'un appareil connecte. Volontairement neutre
// vis-a-vis de la marque : une implementation Tuya existe, une Shelly/Hue/autre
// pourra s'ajouter sans toucher a l'UI. Chaque appel est bloquant et autonome
// (il ouvre et referme sa connexion), donc a utiliser hors du thread UI.
class IDeviceDriver {
public:
    virtual ~IDeviceDriver() = default;

    // Lit l'etat courant. valid == false en cas d'echec (voir error()).
    virtual DeviceControlState query() = 0;

    virtual bool setPower(bool on) = 0;
    virtual bool setBrightness(int raw) = 0;   // echelle brute de l'appareil
    virtual bool setColorTemp(int raw) = 0;    // bascule en blanc + regle la temperature
    virtual bool setColorHsv(int hue, int sat, int val) = 0;  // bascule en couleur
    virtual bool setWhiteMode() = 0;

    virtual QString error() const = 0;
};
