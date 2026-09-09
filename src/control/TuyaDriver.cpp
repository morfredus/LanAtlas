#include "control/TuyaDriver.h"
#include "control/TuyaClient.h"

#include <QJsonObject>

TuyaDriver::TuyaDriver(QString host, QByteArray deviceId, QByteArray localKey)
    : host_(std::move(host)), deviceId_(std::move(deviceId)), localKey_(std::move(localKey))
{
}

QString TuyaDriver::hsvToHex(int hue, int sat, int val)
{
    hue = qBound(0, hue, 360);
    sat = qBound(0, sat, 1000);
    val = qBound(0, val, 1000);
    return QStringLiteral("%1%2%3")
        .arg(hue, 4, 16, QLatin1Char('0'))
        .arg(sat, 4, 16, QLatin1Char('0'))
        .arg(val, 4, 16, QLatin1Char('0'));
}

bool TuyaDriver::hexToHsv(const QString& hex, int& hue, int& sat, int& val)
{
    if (hex.size() < 12)
        return false;
    bool a = false, b = false, c = false;
    hue = hex.mid(0, 4).toInt(&a, 16);
    sat = hex.mid(4, 4).toInt(&b, 16);
    val = hex.mid(8, 4).toInt(&c, 16);
    return a && b && c;
}

// Ouvre une connexion, negocie, ecrit les datapoints, referme.
bool TuyaDriver::applyDps(const QJsonObject& dps)
{
    TuyaClient c(host_, deviceId_, localKey_);
    if (!c.connectAndNegotiate(5000)) {
        error_ = c.error();
        return false;
    }
    if (!c.setDps(dps)) {
        error_ = c.error();
        return false;
    }
    error_.clear();
    return true;
}

DeviceControlState TuyaDriver::query()
{
    DeviceControlState st;
    TuyaClient c(host_, deviceId_, localKey_);
    if (!c.connectAndNegotiate(5000)) {
        error_ = c.error();
        return st;   // valid = false
    }
    bool ok = false;
    const QJsonObject dps = c.queryStatus(&ok);
    if (!ok) {
        error_ = c.error();
        return st;
    }
    // Datapoints de la bande LED (voir mapping cloud).
    st.power = dps.value(QStringLiteral("20")).toBool();
    const QString mode = dps.value(QStringLiteral("21")).toString();
    st.colourMode = (mode == QLatin1String("colour"));
    if (dps.contains(QStringLiteral("22")))
        st.brightness = dps.value(QStringLiteral("22")).toInt();
    if (dps.contains(QStringLiteral("23")))
        st.colorTemp = dps.value(QStringLiteral("23")).toInt();
    const QString colour = dps.value(QStringLiteral("24")).toString();
    if (!colour.isEmpty() && hexToHsv(colour, st.hue, st.sat, st.val))
        st.hasColor = true;
    st.valid = true;
    error_.clear();
    return st;
}

bool TuyaDriver::setPower(bool on)
{
    QJsonObject d;
    d.insert(QStringLiteral("20"), on);
    return applyDps(d);
}

bool TuyaDriver::setBrightness(int raw)
{
    QJsonObject d;
    d.insert(QStringLiteral("22"), qBound(10, raw, 1000));
    return applyDps(d);
}

bool TuyaDriver::setColorTemp(int raw)
{
    QJsonObject d;
    d.insert(QStringLiteral("21"), QStringLiteral("white"));
    d.insert(QStringLiteral("23"), qBound(0, raw, 1000));
    return applyDps(d);
}

bool TuyaDriver::setColorHsv(int hue, int sat, int val)
{
    QJsonObject d;
    d.insert(QStringLiteral("21"), QStringLiteral("colour"));
    d.insert(QStringLiteral("24"), hsvToHex(hue, sat, val));
    return applyDps(d);
}

bool TuyaDriver::setWhiteMode()
{
    QJsonObject d;
    d.insert(QStringLiteral("21"), QStringLiteral("white"));
    return applyDps(d);
}
