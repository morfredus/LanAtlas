#pragma once
#include "core/Device.h"
#include <QVector>

QVector<Device> netbiosDiscover(const QVector<Device>& seeds, int timeoutMs = 400);
