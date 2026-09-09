#pragma once
#include "core/Device.h"
#include <QVector>
#include <functional>

// Sonde TCP. `onProgress(done, total)` est optionnel.
void probePorts(QVector<Device>& devices, int timeoutMs, bool deep,
                const std::function<void(int done, int total)>& onProgress = {});
