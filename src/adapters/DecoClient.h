#pragma once

#include "core/Device.h"
#include "core/Inventory.h"
#include "AppSettings.h"
#include <QString>
#include <QVector>

QVector<Device> queryDeco(const AppSettings& settings, Inventory& inventory,
                          const QStringList& candidateHosts);
