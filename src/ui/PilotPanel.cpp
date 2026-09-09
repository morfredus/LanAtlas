#include "ui/PilotPanel.h"
#include "control/TuyaController.h"
#include "control/TuyaDriver.h"
#include "AppSettings.h"

#include <QColor>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

PilotPanel::PilotPanel(QWidget* parent)
    : QWidget(parent)
{
    qRegisterMetaType<DeviceControlState>();

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(6);

    title_ = new QLabel(QStringLiteral("Piloter"), this);
    QFont tf = title_->font();
    tf.setBold(true);
    title_->setFont(tf);
    lay->addWidget(title_);

    // Saisie inline de la local key (visible si elle manque, ou apres un echec) :
    // pratique pour la premiere saisie. La modification passe aussi par les
    // Parametres (bouton ci-dessous). La cle vit dans les reglages, jamais le git.
    keyRow_ = new QWidget(this);
    auto* krl = new QHBoxLayout(keyRow_);
    krl->setContentsMargins(0, 0, 0, 0);
    krl->addWidget(new QLabel(QStringLiteral("Local key :"), keyRow_));
    keyEdit_ = new QLineEdit(keyRow_);
    keyEdit_->setEchoMode(QLineEdit::Password);
    keyEdit_->setPlaceholderText(QStringLiteral("16 caracteres (via tinytuya)"));
    krl->addWidget(keyEdit_, 1);
    keySave_ = new QPushButton(QStringLiteral("Enregistrer"), keyRow_);
    connect(keySave_, &QPushButton::clicked, this, &PilotPanel::saveKey);
    connect(keyEdit_, &QLineEdit::returnPressed, this, &PilotPanel::saveKey);
    krl->addWidget(keySave_);
    lay->addWidget(keyRow_);

    // Renvoi vers les Parametres pour saisir/corriger la cle (par appareil).
    settingsBtn_ = new QPushButton(QStringLiteral("Modifier dans les Parametres..."), this);
    connect(settingsBtn_, &QPushButton::clicked, this, [this]() {
        emit openSettingsRequested(deviceId_, displayName_);
    });
    lay->addWidget(settingsBtn_);

    // Bloc de controles (visible quand la cle est connue).
    controls_ = new QWidget(this);
    auto* g = new QGridLayout(controls_);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(6);
    int r = 0;

    power_ = new QPushButton(QStringLiteral("Allumer"), controls_);
    power_->setCheckable(true);
    connect(power_, &QPushButton::clicked, this, [this](bool on) {
        if (updating_) return;
        QMetaObject::invokeMethod(controller_, "setPower", Qt::QueuedConnection, Q_ARG(bool, on));
    });
    g->addWidget(power_, r, 0, 1, 2);
    ++r;

    g->addWidget(new QLabel(QStringLiteral("Luminosite"), controls_), r, 0);
    bright_ = new QSlider(Qt::Horizontal, controls_);
    bright_->setRange(10, 1000);
    connect(bright_, &QSlider::sliderReleased, this, [this]() {
        QMetaObject::invokeMethod(controller_, "setBrightness", Qt::QueuedConnection,
                                  Q_ARG(int, bright_->value()));
    });
    g->addWidget(bright_, r, 1);
    ++r;

    auto* modeRow = new QHBoxLayout();
    modeWhite_ = new QPushButton(QStringLiteral("Blanc"), controls_);
    modeWhite_->setCheckable(true);
    modeColour_ = new QPushButton(QStringLiteral("Couleur"), controls_);
    modeColour_->setCheckable(true);
    connect(modeWhite_, &QPushButton::clicked, this, [this]() {
        if (updating_) return;
        QMetaObject::invokeMethod(controller_, "setWhiteMode", Qt::QueuedConnection);
    });
    connect(modeColour_, &QPushButton::clicked, this, [this]() {
        if (updating_) return;
        sendColour();
    });
    modeRow->addWidget(modeWhite_);
    modeRow->addWidget(modeColour_);
    g->addWidget(new QLabel(QStringLiteral("Mode"), controls_), r, 0);
    g->addLayout(modeRow, r, 1);
    ++r;

    g->addWidget(new QLabel(QStringLiteral("Temperature"), controls_), r, 0);
    temp_ = new QSlider(Qt::Horizontal, controls_);
    temp_->setRange(0, 1000);
    connect(temp_, &QSlider::sliderReleased, this, [this]() {
        QMetaObject::invokeMethod(controller_, "setColorTemp", Qt::QueuedConnection,
                                  Q_ARG(int, temp_->value()));
    });
    g->addWidget(temp_, r, 1);
    ++r;

    g->addWidget(new QLabel(QStringLiteral("Teinte"), controls_), r, 0);
    hue_ = new QSlider(Qt::Horizontal, controls_);
    hue_->setRange(0, 360);
    connect(hue_, &QSlider::sliderReleased, this, &PilotPanel::sendColour);
    g->addWidget(hue_, r, 1);
    ++r;

    g->addWidget(new QLabel(QStringLiteral("Saturation"), controls_), r, 0);
    sat_ = new QSlider(Qt::Horizontal, controls_);
    sat_->setRange(0, 1000);
    sat_->setValue(1000);
    connect(sat_, &QSlider::sliderReleased, this, &PilotPanel::sendColour);
    g->addWidget(sat_, r, 1);
    ++r;

    swatch_ = new QLabel(controls_);
    swatch_->setFixedHeight(18);
    swatch_->setAutoFillBackground(true);
    g->addWidget(swatch_, r, 0, 1, 2);
    ++r;

    lay->addWidget(controls_);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color:#8A6D2F;"));
    lay->addWidget(status_);

    // Reflet visuel de la teinte/saturation pendant le glissement (sans reseau).
    auto liveSwatch = [this]() {
        const QColor c = QColor::fromHsv(hue_->value(),
                                         qBound(0, sat_->value() * 255 / 1000, 255), 255);
        swatch_->setStyleSheet(QStringLiteral("background:%1;border:1px solid #999;")
                                   .arg(c.name()));
    };
    connect(hue_, &QSlider::valueChanged, this, [liveSwatch](int) { liveSwatch(); });
    connect(sat_, &QSlider::valueChanged, this, [liveSwatch](int) { liveSwatch(); });
    liveSwatch();

    // Controleur dans un thread de travail.
    controller_ = new TuyaController();
    controller_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, controller_, &QObject::deleteLater);
    connect(controller_, &TuyaController::stateChanged, this, &PilotPanel::onStateChanged);
    connect(controller_, &TuyaController::failed, this, &PilotPanel::onFailed);
    connect(controller_, &TuyaController::busyChanged, this, &PilotPanel::onBusy);
    thread_.start();

    setEnabled(false);   // rien tant qu'aucun appareil n'est cible
}

PilotPanel::~PilotPanel()
{
    thread_.quit();
    thread_.wait(3000);
}

void PilotPanel::setDevice(const QString& host, const QString& deviceId, const QString& displayName)
{
    // Meme appareil deja charge : ne rien refaire. Sans ce garde-fou, chaque
    // rafraichissement de la carte (broadcasts Tuya, enrichissement web...)
    // reselectionne la ligne et relancerait une lecture reseau en boucle.
    if (loaded_ && host == host_ && deviceId == deviceId_) {
        title_->setText(QStringLiteral("Piloter - ") + (displayName.isEmpty() ? host : displayName));
        displayName_ = displayName;
        return;
    }
    host_ = host;
    deviceId_ = deviceId;
    displayName_ = displayName;
    loaded_ = true;
    setEnabled(true);
    title_->setText(QStringLiteral("Piloter - ") + (displayName.isEmpty() ? host : displayName));
    status_->clear();
    refreshKeyState();
}

void PilotPanel::refreshKeyState()
{
    const QString key = AppSettings::tuyaLocalKey(deviceId_);
    const bool hasKey = !key.isEmpty();
    keyRow_->setVisible(!hasKey);
    settingsBtn_->setVisible(!hasKey);
    controls_->setEnabled(hasKey);
    if (!hasKey) {
        status_->setText(
            QStringLiteral("Local key requise. Identifiant : %1")
                .arg(deviceId_.isEmpty() ? QStringLiteral("inconnu") : deviceId_));
        return;
    }
    QMetaObject::invokeMethod(controller_, "configure", Qt::QueuedConnection,
                              Q_ARG(QString, host_),
                              Q_ARG(QByteArray, deviceId_.toUtf8()),
                              Q_ARG(QByteArray, key.toUtf8()));
    QMetaObject::invokeMethod(controller_, "refresh", Qt::QueuedConnection);
}

void PilotPanel::saveKey()
{
    const QString key = keyEdit_->text().trimmed();
    if (key.size() != 16) {
        status_->setText(QStringLiteral("La local key Tuya fait 16 caracteres."));
        return;
    }
    AppSettings::setTuyaLocalKey(deviceId_, key);
    keyEdit_->clear();
    refreshKeyState();
}

void PilotPanel::reloadDeviceKey()
{
    refreshKeyState();
}

void PilotPanel::sendColour()
{
    if (updating_)
        return;
    QMetaObject::invokeMethod(controller_, "setColorHsv", Qt::QueuedConnection,
                              Q_ARG(int, hue_->value()),
                              Q_ARG(int, qMax(1, sat_->value())),
                              Q_ARG(int, 1000));
}

void PilotPanel::applyStateToUi(const DeviceControlState& st)
{
    updating_ = true;
    power_->setChecked(st.power);
    power_->setText(st.power ? QStringLiteral("Eteindre") : QStringLiteral("Allumer"));
    bright_->setValue(qBound(bright_->minimum(), st.brightness, bright_->maximum()));
    temp_->setValue(qBound(0, st.colorTemp, 1000));
    modeWhite_->setChecked(!st.colourMode);
    modeColour_->setChecked(st.colourMode);
    temp_->setEnabled(!st.colourMode);
    hue_->setEnabled(st.colourMode);
    sat_->setEnabled(st.colourMode);
    if (st.hasColor) {
        hue_->setValue(qBound(0, st.hue, 360));
        sat_->setValue(qBound(0, st.sat, 1000));
    }
    updating_ = false;
}

void PilotPanel::onStateChanged(const DeviceControlState& st)
{
    applyStateToUi(st);
    status_->setText(st.power ? QStringLiteral("Allumee.") : QStringLiteral("Eteinte."));
}

void PilotPanel::onFailed(const QString& message)
{
    // Un echec vient souvent d'une local key erronee (l'appareil ne repond pas) :
    // on redonne acces aux Parametres pour la corriger.
    status_->setText(QStringLiteral("Echec : ") + message
                     + QStringLiteral("\nVerifier la local key (saisie ci-dessus ou Parametres)."));
    keyRow_->setVisible(true);
    settingsBtn_->setVisible(true);
}

void PilotPanel::onBusy(bool busy)
{
    controls_->setEnabled(!busy && !AppSettings::tuyaLocalKey(deviceId_).isEmpty());
    if (busy)
        status_->setText(QStringLiteral("Communication..."));
}
