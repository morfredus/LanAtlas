#include "core/IspDetect.h"

static bool containsCi(const QString& hay, const char* needle)
{
    return hay.contains(QLatin1String(needle), Qt::CaseInsensitive);
}

void IspDetect::enrich(Device& device)
{
    const QString h = device.hostname;
    const QString v = device.vendor;

    if (containsCi(h, "livebox") || containsCi(v, "orange") || containsCi(v, "sagem")) {
        device.vendor = "Orange";
        device.category = "box";
        device.infrastructure = true;
        if (containsCi(h, "livebox6") || containsCi(h, "livebox-6"))
            device.model = "Livebox 6";
        else if (containsCi(h, "livebox5") || containsCi(h, "livebox-5"))
            device.model = "Livebox 5";
        else if (containsCi(h, "livebox4") || containsCi(h, "livebox-4"))
            device.model = "Livebox 4";
        else if (device.model.isEmpty())
            device.model = "Livebox";
        return;
    }

    if (containsCi(h, "deco") || (containsCi(v, "tp-link") && containsCi(h, "x50"))) {
        device.vendor = "TP-Link";
        device.category = "mesh_node";
        device.infrastructure = true;
        if (device.model.isEmpty())
            device.model = "Deco X50";
        return;
    }

    if (containsCi(h, "freebox") || containsCi(v, "freebox")) {
        device.vendor = "Free";
        device.category = "box";
        device.infrastructure = true;
    }
}
