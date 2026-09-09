#pragma once
#include "core/Device.h"
#include <QVector>

QVector<Device> mdnsDiscover(int timeoutMs = 2000);
