#pragma once

#include <QString>
#include <QMetaType>

// Etat de controle d'un luminaire connecte, independant de la marque. Les
// valeurs brutes (luminosite, teinte...) gardent l'echelle de l'appareil ;
// l'UI convertit en pourcentage a l'affichage.
struct DeviceControlState {
    bool valid = false;      // faux si la derniere lecture a echoue
    bool power = false;      // allume / eteint
    int  brightness = 0;
    int  brightnessMin = 10;
    int  brightnessMax = 1000;
    int  colorTemp = 0;      // blanc chaud <-> froid
    int  colorTempMax = 1000;
    bool colourMode = false; // vrai = mode couleur, faux = mode blanc
    int  hue = 0;            // 0-360
    int  sat = 0;            // 0-1000
    int  val = 0;            // 0-1000
    bool hasColor = false;   // l'appareil expose-t-il une couleur RVB
};

Q_DECLARE_METATYPE(DeviceControlState)
