#include "ui/SettingsDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Parametres - LanAtlas"));
    auto* form = new QFormLayout();
    liveboxHost_ = new QLineEdit(this);
    liveboxUser_ = new QLineEdit(this);
    liveboxPassword_ = new QLineEdit(this);
    liveboxPassword_->setEchoMode(QLineEdit::Password);
    decoHost_ = new QLineEdit(this);
    decoHost_->setPlaceholderText(QStringLiteral("vide = auto-decouverte"));
    decoPassword_ = new QLineEdit(this);
    decoPassword_->setEchoMode(QLineEdit::Password);
    pingTimeout_ = new QSpinBox(this);
    pingTimeout_->setRange(50, 3000);
    tcpTimeout_ = new QSpinBox(this);
    tcpTimeout_->setRange(50, 3000);
    parallel_ = new QSpinBox(this);
    parallel_->setRange(4, 256);
    deep_ = new QCheckBox(QStringLiteral("Scan de ports etendu"), this);

    form->addRow(QStringLiteral("Livebox (IP)"), liveboxHost_);
    form->addRow(QStringLiteral("Livebox utilisateur"), liveboxUser_);
    form->addRow(QStringLiteral("Livebox mot de passe"), liveboxPassword_);
    form->addRow(QStringLiteral("Deco (IP du principal, optionnel)"), decoHost_);
    form->addRow(QStringLiteral("Deco (mot de passe appli / compte TP-Link)"), decoPassword_);
    auto* decoHint = new QLabel(
        QStringLiteral("C'est le mot de passe du compte dans l'appli Deco, pas un second "
                       "admin. Derriere une Livebox, coller l'IP d'un nœud (souvent "
                       "192.168.1.12, .18, .22) : le login local passe en HTTPS avec un "
                       "certificat interne, pas 192.168.68.1."),
        this);
    decoHint->setWordWrap(true);
    form->addRow(decoHint);
    form->addRow(QStringLiteral("Timeout ping (ms)"), pingTimeout_);
    form->addRow(QStringLiteral("Timeout TCP (ms)"), tcpTimeout_);
    form->addRow(QStringLiteral("Pings paralleles"), parallel_);
    form->addRow(deep_);

    // Section pilotage : local keys Tuya par appareil. C'est ici qu'on les saisit
    // ou qu'on les corrige (elles ne sont pas sur le LAN, elles viennent du cloud
    // Tuya via tinytuya) ; elles sont rangees dans les reglages, jamais dans le git.
    auto* tuyaBox = new QGroupBox(QStringLiteral("Pilotage - local keys Tuya"), this);
    auto* tuyaLay = new QVBoxLayout(tuyaBox);
    auto* tuyaHint = new QLabel(
        QStringLiteral("Une ligne par appareil : identifiant (gwId, visible dans le "
                       "dossier) et local key (16 caracteres). Modifiable a tout moment."),
        tuyaBox);
    tuyaHint->setWordWrap(true);
    tuyaLay->addWidget(tuyaHint);
    tuyaKeys_ = new QTableWidget(0, 2, tuyaBox);
    tuyaKeys_->setHorizontalHeaderLabels(
        {QStringLiteral("Identifiant appareil"), QStringLiteral("Local key")});
    tuyaKeys_->horizontalHeader()->setStretchLastSection(true);
    tuyaKeys_->verticalHeader()->setVisible(false);
    tuyaKeys_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tuyaKeys_->setSelectionMode(QAbstractItemView::SingleSelection);
    tuyaLay->addWidget(tuyaKeys_);
    auto* tuyaBtns = new QHBoxLayout();
    auto* addBtn = new QPushButton(QStringLiteral("Ajouter"), tuyaBox);
    auto* rmBtn = new QPushButton(QStringLiteral("Retirer"), tuyaBox);
    connect(addBtn, &QPushButton::clicked, this, &SettingsDialog::addTuyaRow);
    connect(rmBtn, &QPushButton::clicked, this, &SettingsDialog::removeTuyaRow);
    tuyaBtns->addWidget(addBtn);
    tuyaBtns->addWidget(rmBtn);
    tuyaBtns->addStretch(1);
    tuyaLay->addLayout(tuyaBtns);
    loadTuyaKeys();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    // Persister les cles Tuya avant la fermeture (elles ont leur propre stockage,
    // independant du reste des reglages).
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::persistTuyaKeys);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* box = new QVBoxLayout(this);
    box->addLayout(form);
    box->addWidget(tuyaBox);
    box->addWidget(buttons);
}

void SettingsDialog::loadTuyaKeys()
{
    QSettings s = AppSettings::store();
    s.beginGroup(QStringLiteral("tuya/keys"));
    const QStringList ids = s.childKeys();
    s.endGroup();
    for (const QString& id : ids) {
        const int r = tuyaKeys_->rowCount();
        tuyaKeys_->insertRow(r);
        tuyaKeys_->setItem(r, 0, new QTableWidgetItem(id));
        tuyaKeys_->setItem(r, 1, new QTableWidgetItem(AppSettings::tuyaLocalKey(id)));
    }
}

void SettingsDialog::addTuyaRow()
{
    const int r = tuyaKeys_->rowCount();
    tuyaKeys_->insertRow(r);
    tuyaKeys_->setItem(r, 0, new QTableWidgetItem());
    tuyaKeys_->setItem(r, 1, new QTableWidgetItem());
    tuyaKeys_->editItem(tuyaKeys_->item(r, 0));
    tuyaKeys_->selectRow(r);
}

void SettingsDialog::removeTuyaRow()
{
    const int r = tuyaKeys_->currentRow();
    if (r >= 0)
        tuyaKeys_->removeRow(r);
}

void SettingsDialog::persistTuyaKeys()
{
    // Repartir a zero puis reecrire : gere aussi les suppressions de lignes.
    QSettings s = AppSettings::store();
    s.beginGroup(QStringLiteral("tuya/keys"));
    s.remove(QString());
    s.endGroup();
    for (int r = 0; r < tuyaKeys_->rowCount(); ++r) {
        const QString id = tuyaKeys_->item(r, 0) ? tuyaKeys_->item(r, 0)->text().trimmed() : QString();
        const QString key = tuyaKeys_->item(r, 1) ? tuyaKeys_->item(r, 1)->text().trimmed() : QString();
        if (!id.isEmpty() && !key.isEmpty())
            AppSettings::setTuyaLocalKey(id, key);
    }
}

void SettingsDialog::focusTuyaDevice(const QString& deviceId, const QString& label)
{
    Q_UNUSED(label);
    if (deviceId.isEmpty())
        return;
    for (int r = 0; r < tuyaKeys_->rowCount(); ++r) {
        if (tuyaKeys_->item(r, 0) && tuyaKeys_->item(r, 0)->text().trimmed() == deviceId) {
            tuyaKeys_->selectRow(r);
            tuyaKeys_->scrollToItem(tuyaKeys_->item(r, 0));
            return;
        }
    }
    // Absent : creer une ligne pre-remplie avec l'identifiant, prete pour la cle.
    const int r = tuyaKeys_->rowCount();
    tuyaKeys_->insertRow(r);
    tuyaKeys_->setItem(r, 0, new QTableWidgetItem(deviceId));
    tuyaKeys_->setItem(r, 1, new QTableWidgetItem());
    tuyaKeys_->selectRow(r);
    tuyaKeys_->editItem(tuyaKeys_->item(r, 1));
}

AppSettings SettingsDialog::values() const
{
    AppSettings s;
    s.liveboxHost = liveboxHost_->text().trimmed();
    s.liveboxUser = liveboxUser_->text().trimmed();
    s.liveboxPassword = liveboxPassword_->text();
    s.decoHost = decoHost_->text().trimmed();
    s.decoPassword = decoPassword_->text();
    s.pingTimeoutMs = pingTimeout_->value();
    s.tcpTimeoutMs = tcpTimeout_->value();
    s.maxParallelPings = parallel_->value();
    s.deepPortScan = deep_->isChecked();
    return s;
}

void SettingsDialog::setValues(const AppSettings& s)
{
    liveboxHost_->setText(s.liveboxHost);
    liveboxUser_->setText(s.liveboxUser);
    liveboxPassword_->setText(s.liveboxPassword);
    decoHost_->setText(s.decoHost);
    decoPassword_->setText(s.decoPassword);
    pingTimeout_->setValue(s.pingTimeoutMs);
    tcpTimeout_->setValue(s.tcpTimeoutMs);
    parallel_->setValue(s.maxParallelPings);
    deep_->setChecked(s.deepPortScan);
}
