#pragma once

#include "core/MorfService.h"

#include <QHash>
#include <QObject>
#include <QVector>

class QUdpSocket;

// Ecoute les annonces morfBeacon. Ne controle rien : observe seulement.
class BeaconListener : public QObject {
    Q_OBJECT
public:
    explicit BeaconListener(QObject* parent = nullptr);

    bool start(quint16 port = 45454);
    QString bindError() const { return bindError_; }
    QVector<MorfService> services() const;

signals:
    void changed();

private slots:
    void onReadyRead();

private:
    QUdpSocket* socket_ = nullptr;
    QHash<QString, MorfService> byKey_;
    QString bindError_;
};
