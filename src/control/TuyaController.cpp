#include "control/TuyaController.h"
#include "control/TuyaDriver.h"

TuyaController::TuyaController(QObject* parent)
    : QObject(parent)
{
}

TuyaController::~TuyaController() = default;

void TuyaController::configure(const QString& host, const QByteArray& deviceId,
                               const QByteArray& localKey)
{
    driver_ = std::make_unique<TuyaDriver>(host, deviceId, localKey);
}

void TuyaController::refresh()
{
    if (!driver_) {
        emit failed(QStringLiteral("aucun appareil configure"));
        return;
    }
    emit busyChanged(true);
    const DeviceControlState st = driver_->query();
    emit busyChanged(false);
    if (st.valid)
        emit stateChanged(st);
    else
        emit failed(driver_->error());
}

void TuyaController::afterWrite(bool ok)
{
    if (!ok) {
        emit busyChanged(false);
        emit failed(driver_->error());
        return;
    }
    // Relire l'etat reel apres l'ecriture (l'appareil applique parfois des
    // valeurs bornees differentes de la consigne).
    const DeviceControlState st = driver_->query();
    emit busyChanged(false);
    if (st.valid)
        emit stateChanged(st);
    // Un echec de relecture seul n'est pas remonte comme une erreur d'ecriture.
}

void TuyaController::setPower(bool on)
{
    if (!driver_) { emit failed(QStringLiteral("aucun appareil configure")); return; }
    emit busyChanged(true);
    afterWrite(driver_->setPower(on));
}

void TuyaController::setBrightness(int raw)
{
    if (!driver_) { emit failed(QStringLiteral("aucun appareil configure")); return; }
    emit busyChanged(true);
    afterWrite(driver_->setBrightness(raw));
}

void TuyaController::setColorTemp(int raw)
{
    if (!driver_) { emit failed(QStringLiteral("aucun appareil configure")); return; }
    emit busyChanged(true);
    afterWrite(driver_->setColorTemp(raw));
}

void TuyaController::setColorHsv(int hue, int sat, int val)
{
    if (!driver_) { emit failed(QStringLiteral("aucun appareil configure")); return; }
    emit busyChanged(true);
    afterWrite(driver_->setColorHsv(hue, sat, val));
}

void TuyaController::setWhiteMode()
{
    if (!driver_) { emit failed(QStringLiteral("aucun appareil configure")); return; }
    emit busyChanged(true);
    afterWrite(driver_->setWhiteMode());
}
