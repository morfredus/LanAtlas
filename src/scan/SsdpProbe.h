#pragma once
#include "core/Device.h"
#include <QVector>

QVector<Device> ssdpDiscover(int timeoutMs = 2500);
