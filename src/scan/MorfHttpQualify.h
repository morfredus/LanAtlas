#pragma once

#include "core/Device.h"
#include <QVector>
#include <functional>

// Port ouvert = candidat. GET /status (et /healthz) = preuve seulement si le
// JSON suit le contrat morfSystem. Un 200 HTML ou MQTT/TLS ne qualifie pas.
void qualifyMorfHttp(QVector<Device>& devices, int timeoutMs,
                     const std::function<void(int done, int total)>& onProgress = {});

// Empreinte HTTP generique : pour un hote qui sert du web mais n'a pas de modele,
// on lit le <title> de "/" et l'en-tete Server: (revele souvent l'appareil :
// admin routeur, imprimante, NAS, camera...). N'ecrase jamais un modele connu.
void fingerprintHttp(QVector<Device>& devices, int timeoutMs);
