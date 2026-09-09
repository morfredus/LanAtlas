#pragma once

#include "core/Inventory.h"
#include "core/OuiLookup.h"
#include "AppSettings.h"
#include "scan/LanContext.h"

#include <QObject>

class ScanOrchestrator : public QObject {
    Q_OBJECT
public:
    explicit ScanOrchestrator(Inventory* inventory, OuiLookup* oui, QObject* parent = nullptr);

public slots:
    // incremental=false : scan complet (repart de zero).
    // incremental=true  : rescan qui complete/met a jour l'existant.
    void run(AppSettings settings, bool incremental);

signals:
    void progress(int percent, const QString& phase);
    void finished();

private:
    Inventory* inventory_;
    OuiLookup* oui_;
};
