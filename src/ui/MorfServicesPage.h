#pragma once

#include "core/MorfService.h"
#include <QWidget>

class BeaconListener;
class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;

class MorfServicesPage : public QWidget {
    Q_OBJECT
public:
    explicit MorfServicesPage(QWidget* parent = nullptr);
    QVector<MorfService> services() const;

signals:
    void directoryChanged();

private slots:
    void rebuild();
    void onSelectionChanged();
    void tickStale();

private:
    void showService(const MorfService& s);
    void fetchStatus(const MorfService& s);

    BeaconListener* listener_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QPlainTextEdit* detail_ = nullptr;
    QLabel* summary_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QNetworkAccessManager* http_ = nullptr;
    QString selectedKey_;
    QString pendingStatusKey_;
};
