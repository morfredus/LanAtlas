#pragma once

#include "control/DeviceControl.h"
#include <QWidget>
#include <QThread>
#include <QString>

class QLabel;
class QPushButton;
class QSlider;
class QLineEdit;
class QWidget;
class TuyaController;

// Panneau « Piloter » : marche/arret, luminosite, mode blanc (temperature) ou
// couleur (teinte/saturation) d'un luminaire connecte. S'affiche pour un appareil
// dont on sait parler (aujourd'hui Tuya). Embarque son propre thread de travail :
// les appels reseau sont bloquants et ne doivent pas figer l'interface.
class PilotPanel : public QWidget {
    Q_OBJECT
public:
    explicit PilotPanel(QWidget* parent = nullptr);
    ~PilotPanel() override;

    // Cible un appareil ; charge sa local key depuis les reglages. displayName
    // sert d'intitule. Si la cle manque, le panneau propose de la saisir.
    void setDevice(const QString& host, const QString& deviceId, const QString& displayName);
    // Recharge la cle depuis les reglages (apres edition dans les Parametres) et
    // relance une lecture d'etat.
    void reloadDeviceKey();

signals:
    // Demande l'ouverture des Parametres, pre-cales sur cet appareil, pour saisir
    // ou corriger sa local key.
    void openSettingsRequested(const QString& deviceId, const QString& label);

private slots:
    void onStateChanged(const DeviceControlState& st);
    void onFailed(const QString& message);
    void onBusy(bool busy);
    void saveKey();              // enregistre la cle saisie en ligne

private:
    void applyStateToUi(const DeviceControlState& st);
    void refreshKeyState();      // affiche le renvoi Parametres ou les controles
    void sendColour();           // pousse la couleur teinte/saturation courante

    QThread thread_;
    TuyaController* controller_ = nullptr;

    QString host_;
    QString deviceId_;
    QString displayName_;
    bool    loaded_ = false;     // un appareil est deja charge (evite les relectures en boucle)
    bool    updating_ = false;   // bloque la reaction aux signaux pendant l'application d'un etat

    QLabel*      title_ = nullptr;
    QWidget*     keyRow_ = nullptr;        // saisie inline de la local key
    QLineEdit*   keyEdit_ = nullptr;
    QPushButton* keySave_ = nullptr;
    QPushButton* settingsBtn_ = nullptr;   // renvoi vers Parametres (modification)
    QWidget*     controls_ = nullptr;
    QPushButton* power_ = nullptr;
    QSlider*     bright_ = nullptr;
    QPushButton* modeWhite_ = nullptr;
    QPushButton* modeColour_ = nullptr;
    QSlider*     temp_ = nullptr;
    QSlider*     hue_ = nullptr;
    QSlider*     sat_ = nullptr;
    QLabel*      swatch_ = nullptr;
    QLabel*      status_ = nullptr;
};
