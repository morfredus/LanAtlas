#include "core/OuiLookup.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

static QString prefix24(QString mac)
{
    mac = mac.trimmed().toUpper();
    mac.replace('-', ':');
    const auto parts = mac.split(':');
    if (parts.size() < 3)
        return {};
    return parts[0] + ":" + parts[1] + ":" + parts[2];
}

bool OuiLookup::loadFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray())
        return false;
    map_.clear();
    for (const auto& v : doc.array()) {
        const auto o = v.toObject();
        QString oui = o.value("oui").toString().toUpper();
        oui.replace('-', ':');
        map_.insert(oui, o.value("manufacturer").toString());
    }
    return !map_.isEmpty();
}

QString OuiLookup::vendor(const QString& mac) const
{
    return map_.value(prefix24(mac));
}
