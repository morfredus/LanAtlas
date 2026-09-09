#pragma once

#include "AppSettings.h"
#include <QDialog>

class QLineEdit;
class QSpinBox;
class QCheckBox;
class QTableWidget;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    AppSettings values() const;
    void setValues(const AppSettings& s);

    // Pre-selectionne (ou ajoute) une ligne pour cet appareil, afin d'aller droit
    // a la saisie de sa cle quand on ouvre les reglages depuis le panneau Piloter.
    void focusTuyaDevice(const QString& deviceId, const QString& label);

private slots:
    void addTuyaRow();
    void removeTuyaRow();
    void persistTuyaKeys();

private:
    void loadTuyaKeys();

    QLineEdit* liveboxHost_ = nullptr;
    QLineEdit* liveboxUser_ = nullptr;
    QLineEdit* liveboxPassword_ = nullptr;
    QLineEdit* decoHost_ = nullptr;
    QLineEdit* decoPassword_ = nullptr;
    QSpinBox* pingTimeout_ = nullptr;
    QSpinBox* tcpTimeout_ = nullptr;
    QSpinBox* parallel_ = nullptr;
    QCheckBox* deep_ = nullptr;
    QTableWidget* tuyaKeys_ = nullptr;   // identifiant appareil -> local key
};
