#pragma once

#include "core/Device.h"
#include <QMutex>
#include <QVector>
#include <QJsonObject>

class Inventory {
public:
    void clear();
    // Debut de scan : purge les resumes/journal. En mode complet, vide aussi les
    // appareils ; en incremental, garde l'identite connue mais remet la preuve de
    // presence a zero (les non-revus repassent en historique).
    void beginScan(bool incremental);
    void upsert(Device device);
    void replaceAll(QVector<Device> devices);
    QVector<Device> devices() const;
    QJsonObject toJson() const;
    void loadJson(const QJsonObject& root);   // restaure une session precedente

    QString liveboxSummary;
    QString decoSummary;
    QString wanIpv4;
    QString lanCidr;
    QString lastError;
    QStringList logLines;

    void log(const QString& line);

private:
    mutable QMutex mutex_;
    QVector<Device> devices_;
};
