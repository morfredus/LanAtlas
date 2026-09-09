#pragma once

#include "core/Device.h"
#include "core/Inventory.h"
#include "AppSettings.h"
#include <QString>
#include <QVector>

QVector<Device> queryLivebox(const AppSettings& settings, Inventory& inventory, QString* boxIpOut = nullptr);
