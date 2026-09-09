#pragma once

#include "control/DeviceControl.h"
#include <QObject>
#include <QString>
#include <QByteArray>
#include <memory>

class IDeviceDriver;

// Enveloppe QObject autour d'un IDeviceDriver, destinee a vivre dans un thread de
// travail : les appels reseau sont bloquants et ne doivent jamais geler l'UI. Le
// panneau communique par signaux/slots (connexions en file d'attente entre
// threads). Chaque ecriture est suivie d'une relecture pour refleter l'etat reel.
class TuyaController : public QObject {
    Q_OBJECT
public:
    explicit TuyaController(QObject* parent = nullptr);
    ~TuyaController() override;

public slots:
    void configure(const QString& host, const QByteArray& deviceId, const QByteArray& localKey);
    void refresh();
    void setPower(bool on);
    void setBrightness(int raw);
    void setColorTemp(int raw);
    void setColorHsv(int hue, int sat, int val);
    void setWhiteMode();

signals:
    void stateChanged(const DeviceControlState& state);
    void failed(const QString& message);
    void busyChanged(bool busy);

private:
    void afterWrite(bool ok);

    std::unique_ptr<IDeviceDriver> driver_;
};
