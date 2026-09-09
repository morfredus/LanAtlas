#pragma once

#include "core/Inventory.h"
#include "core/OuiLookup.h"
#include "scan/ScanOrchestrator.h"

#include <QMainWindow>
#include <QHash>
#include <QSet>
#include <QThread>

class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QComboBox;
class QProgressBar;
class QCheckBox;
class QTimer;
class TopologyView;
class MorfServicesPage;
class WebEnricher;
class TuyaListener;
class PilotPanel;
struct Device;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    Inventory* inventory() { return &inventory_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void startScan();     // scan complet (repart de zero)
    void startRescan();   // rescan incremental (complete / met a jour)
    void onProgress(int percent, const QString& phase);
    void onFinished();
    void openSettings();
    void exportJson();
    void exportCsv();
    void refreshViews();
    void fillTable();
    void fillHistoryTable();
    void paintMap();
    void showDeviceRow(int row);
    void showHistRow(int row);
    void onHeaderClicked(int column);
    void onMorfDirectoryChanged();
    void onWebDeviceUpdated(const QString& deviceId);
    void enrichCurrent();
    void copyIp();
    void copyMac();
    void renameCurrent();
    void pingCurrent();
    void webSearchCurrent();
    void macLookupCurrent();
    void openHttpCurrent();
    void tableContextMenu(const QPoint& pos);

private:
    const Device* currentDevice() const;
    QString statsText() const;
    void rebuildShown();
    void launchScan(bool incremental);
    QString statePath() const;
    void saveState() const;
    void loadState();
    QString labelsPath() const;
    void loadLabels();
    void saveLabels() const;
    void showDeviceFromTable(QTableWidget* table, int row, QPlainTextEdit* dest, QString* selId);
    void updatePilot(const Device* d);   // affiche le panneau Piloter si l'appareil est controlable
    void openSettingsFor(const QString& tuyaDeviceId, const QString& label);  // reglages, cale sur un appareil

    Inventory inventory_;
    OuiLookup oui_;
    QThread worker_;
    ScanOrchestrator* orchestrator_ = nullptr;
    TopologyView* topology_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPlainTextEdit* detail_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    MorfServicesPage* morfPage_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QComboBox* presenceFilter_ = nullptr;
    QComboBox* parentFilter_ = nullptr;
    QCheckBox* mapHistory_ = nullptr;
    QComboBox* mapConnFilter_ = nullptr;
    QTableWidget* histTable_ = nullptr;
    QPlainTextEdit* histDetail_ = nullptr;
    PilotPanel* pilot_ = nullptr;      // controle des appareils connectes (Tuya)
    QLabel* statsLabel_ = nullptr;
    QVector<Device> shown_;
    QString selectedId_;       // ligne selectionnee dans l'onglet Inventaire
    QString histSelectedId_;   // ligne selectionnee dans l'onglet Historique
    QHash<QString, QString> labels_;   // id -> nom donne par l'utilisateur
    QSet<QString> prevConnected_;      // ids connectes au dernier scan (badges)
    int sortColumn_ = -1;
    bool sortAsc_ = true;
    WebEnricher* enricher_ = nullptr;
    TuyaListener* tuya_ = nullptr;     // ecoute permanente des broadcasts Tuya
    QTimer* tuyaTimer_ = nullptr;      // coalesce les rafraichissements Tuya
    QLabel* statusLabel_ = nullptr;
    QLabel* progressPct_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QAction* scanAction_ = nullptr;
    QAction* rescanAction_ = nullptr;
    QTimer* autoTimer_ = nullptr;
};
