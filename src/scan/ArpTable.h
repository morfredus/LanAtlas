#pragma once

#include "core/Device.h"
#include "scan/LanContext.h"
#include <QVector>

QVector<Device> readArpTable(const LanContext& ctx);
void primeArpCache(const LanContext& ctx, int timeoutMs, int maxParallel);
