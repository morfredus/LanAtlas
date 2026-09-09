#pragma once
#include "core/Device.h"
#include <QVector>
#include <QStringList>

QVector<Device> pingSweep(const QStringList& ips, int timeoutMs, int maxParallel);
