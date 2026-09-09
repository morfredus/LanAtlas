#pragma once

#include "core/Device.h"

namespace WebLookup {
void searchDevice(const Device& d);
void lookupMac(const QString& mac);
void lookupOui(const QString& mac);
void searchPorts(const Device& d);
void openHttp(const Device& d);
void openStatus(const QString& url);
}
