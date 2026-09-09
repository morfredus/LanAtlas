#include "ui/MainWindow.h"
#include "ui/TopologyView.h"
#include "ui/SettingsDialog.h"
#include "ui/MorfServicesPage.h"
#include "ui/Theme.h"
#include "ui/PilotPanel.h"
#include "core/Device.h"
#include "core/MorfService.h"
#include "scan/TuyaListener.h"
#include "core/NetHints.h"
#include "core/WebLookup.h"
#include "core/WebEnricher.h"
#include "AppSettings.h"

#include <morfupdate/UpdateDialog.h>
#include <QAbstractSocket>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QStandardPaths>
#include <QCollator>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFont>
#include <QHeaderView>
#include <QHostAddress>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QProgressBar>
#include <QShortcut>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QCursor>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <algorithm>

namespace {
const QString kAllParents = QStringLiteral("Tous les parents");
const QString kUnattached = QStringLiteral("Non rattaches");
const QString kShowAll = QStringLiteral("Tous");
const QString kShowConn = QStringLiteral("Connectes");
const QString kShowHist = QStringLiteral("Historique");
const QString kShowTech = QStringLiteral("Adresses techniques");

bool isInfra(const Device& d)
{
    return d.infrastructure || d.category == QLatin1String("box")
           || d.category == QLatin1String("mesh_node");
}

bool isUnattached(const Device& d)
{
    return !isInfra(d) && d.parentName.isEmpty() && d.parentId.isEmpty();
}

int cmpText(const QString& a, const QString& b, bool asc)
{
    QCollator col;
    col.setCaseSensitivity(Qt::CaseInsensitive);
    col.setNumericMode(true);
    const int c = col.compare(a, b);
    return asc ? c : -c;
}

quint32 ipv4(const QString& ip)
{
    const QHostAddress a(ip);
    if (a.protocol() != QAbstractSocket::IPv4Protocol)
        return 0;
    return a.toIPv4Address();
}

int cmpDevices(const Device* a, const Device* b, int col, bool asc, bool historical)
{
    auto byName = [&]() { return cmpText(a->displayName(), b->displayName(), true); };
    auto byParent = [&]() { return cmpText(a->parentName, b->parentName, true); };
    if (col < 0) {
        if (historical) {
            if (a->lastSeen != b->lastSeen)
                return a->lastSeen > b->lastSeen ? -1 : 1;
            return byName();
        }
        const int p = byParent();
        if (p != 0)
            return p;
        return byName();
    }
    int c = 0;
    switch (col) {
    case 0:
        c = cmpText(a->displayName(), b->displayName(), true);
        break;
    case 1:
        c = cmpText(a->presenceLabel(), b->presenceLabel(), true);
        break;
    case 2: {
        const quint32 ia = ipv4(a->ip);
        const quint32 ib = ipv4(b->ip);
        c = (ia < ib) ? -1 : (ia > ib ? 1 : cmpText(a->ip, b->ip, true));
        break;
    }
    case 3:
        c = cmpText(normalizedMac(a->mac), normalizedMac(b->mac), true);
        break;
    case 4:
        c = cmpText(a->vendor, b->vendor, true);
        break;
    case 5:
        c = cmpText(a->model, b->model, true);
        break;
    case 6:
        c = cmpText(a->parentName, b->parentName, true);
        break;
    case 7:
        c = cmpText(a->connection + a->band, b->connection + b->band, true);
        break;
    case 8:
        c = cmpText(NetHints::namedPorts(a->openPorts), NetHints::namedPorts(b->openPorts), true);
        break;
    case 9:
        c = cmpText(a->proofShort(), b->proofShort(), true);
        break;
    case 10:
        if (a->lastSeen == b->lastSeen)
            c = 0;
        else
            c = a->lastSeen < b->lastSeen ? -1 : 1;
        break;
    default:
        c = byName();
        break;
    }
    return asc ? c : -c;
}

void sortGroup(QVector<const Device*>& group, int col, bool asc, bool historical)
{
    std::sort(group.begin(), group.end(), [&](const Device* a, const Device* b) {
        return cmpDevices(a, b, col, asc, historical) < 0;
    });
}

void configureInventoryTable(QTableWidget* t)
{
    t->setColumnCount(11);
    t->setHorizontalHeaderLabels(
        {QStringLiteral("Nom"), QStringLiteral("Etat / presence"), QStringLiteral("IP"),
         QStringLiteral("MAC"), QStringLiteral("Fabricant"), QStringLiteral("Modele"),
         QStringLiteral("Parent"), QStringLiteral("Connexion"), QStringLiteral("Ports (indices)"),
         QStringLiteral("Preuve morf"), QStringLiteral("Derniere vue")});
    t->horizontalHeader()->setStretchLastSection(true);
    t->horizontalHeader()->setSectionsClickable(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->setSortingEnabled(false);
    t->setContextMenuPolicy(Qt::CustomContextMenu);
}

void writeDeviceRow(QTableWidget* table, int r, const Device& d)
{
    auto set = [&](int c, const QString& v) {
        auto* item = new QTableWidgetItem(v);
        item->setForeground(QColor(0x1A, 0x12, 0x08));
        table->setItem(r, c, item);
    };
    set(0, d.typeEmoji() + QStringLiteral("  ") + d.displayName());
    table->item(r, 0)->setData(Qt::UserRole, d.id());
    if (d.justAppeared) {
        table->item(r, 0)->setBackground(QColor(0xDD, 0xF3, 0xDD));
        table->item(r, 0)->setToolTip(QStringLiteral("Nouveau depuis le dernier scan"));
    } else if (d.justGone) {
        table->item(r, 0)->setBackground(QColor(0xF7, 0xDE, 0xD8));
        table->item(r, 0)->setToolTip(QStringLiteral("Disparu depuis le dernier scan"));
    }
    set(1, d.presenceLabel());
    set(2, d.ip);
    set(3, normalizedMac(d.mac));
    set(4, d.vendor);
    set(5, d.model);
    set(6, d.parentName);
    set(7, d.linkShort());
    set(8, NetHints::namedPorts(d.openPorts));
    set(9, d.proofShort());
    set(10, d.lastSeen.isValid() ? d.lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                 : QString());
}
} // namespace

// Petit retour visuel a l'endroit du clic (defini plus bas).
static void flashTip(const QString& msg);

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("LanAtlas %1  -  cartographie du reseau local")
                       .arg(QStringLiteral(LA_APP_VERSION)));
    resize(1480, 900);

    const QString ouiPath = QCoreApplication::applicationDirPath() + QStringLiteral("/oui.json");
    if (!oui_.loadFile(ouiPath))
        oui_.loadFile(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            QStringLiteral("../data/oui.json")));

    orchestrator_ = new ScanOrchestrator(&inventory_, &oui_);
    orchestrator_->moveToThread(&worker_);
    connect(&worker_, &QThread::finished, orchestrator_, &QObject::deleteLater);
    connect(orchestrator_, &ScanOrchestrator::progress, this, &MainWindow::onProgress);
    connect(orchestrator_, &ScanOrchestrator::finished, this, &MainWindow::onFinished);
    qRegisterMetaType<AppSettings>();
    worker_.start();

    enricher_ = new WebEnricher(this);
    enricher_->setInventory(&inventory_);
    connect(enricher_, &WebEnricher::deviceUpdated, this, &MainWindow::onWebDeviceUpdated);
    connect(enricher_, &WebEnricher::wanUpdated, this, [this]() { onWebDeviceUpdated(QString()); });

    // Ecoute permanente des broadcasts Tuya : demarree tout de suite pour avoir
    // capte les annonces (une toutes les ~10 s) avant meme le premier scan. Les
    // annonces sont superposees a l'inventaire dans rebuildShown(). On coalesce
    // les rafraichissements : plusieurs appareils peuvent parler en rafale.
    tuya_ = new TuyaListener(this);
    tuya_->start();
    tuyaTimer_ = new QTimer(this);
    tuyaTimer_->setSingleShot(true);
    tuyaTimer_->setInterval(500);
    connect(tuyaTimer_, &QTimer::timeout, this, [this]() {
        if (!shown_.isEmpty() || !inventory_.devices().isEmpty()
            || !tuya_->announces().isEmpty())
            refreshViews();
    });
    connect(tuya_, &TuyaListener::changed, this, [this]() { tuyaTimer_->start(); });

    auto* tabs = new QTabWidget(this);

    auto* mapPage = new QWidget(tabs);
    auto* mapLay = new QVBoxLayout(mapPage);
    mapLay->setContentsMargins(8, 8, 8, 8);
    statsLabel_ = new QLabel(mapPage);
    statsLabel_->setWordWrap(true);
    mapLay->addWidget(statsLabel_);

    // Cree tot pour cabler les boutons de zoom dessus.
    topology_ = new TopologyView(mapPage);

    auto* mapCtl = new QHBoxLayout();
    auto mapButton = [&](const QString& text, void (TopologyView::*slot)(), int w) {
        auto* b = new QPushButton(text, mapPage);
        b->setFixedHeight(30);
        if (w > 0)
            b->setFixedWidth(w);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, topology_, slot);
        mapCtl->addWidget(b);
        return b;
    };
    mapButton(QStringLiteral("Ajuster"), &TopologyView::fitMap, 0);
    mapButton(QStringLiteral("100 %"), &TopologyView::zoomReset, 0);
    mapButton(QString::fromUtf8("\xE2\x88\x92"), &TopologyView::zoomOut, 40);   // signe moins
    mapButton(QStringLiteral("+"), &TopologyView::zoomIn, 40);
    mapButton(QStringLiteral("Exporter PNG"), &TopologyView::exportPng, 0);
    mapCtl->addSpacing(18);
    mapCtl->addWidget(new QLabel(QStringLiteral("Liaison :"), mapPage));
    mapConnFilter_ = new QComboBox(mapPage);
    mapConnFilter_->addItems({QStringLiteral("Toutes"), QStringLiteral("Wi-Fi"),
                              QStringLiteral("Filaire")});
    connect(mapConnFilter_, &QComboBox::currentTextChanged, this,
            [this](const QString&) { paintMap(); });
    mapCtl->addWidget(mapConnFilter_);
    mapCtl->addStretch(1);
    mapHistory_ = new QCheckBox(QStringLiteral("Historique (absents en pointille)"), mapPage);
    mapHistory_->setChecked(true);
    connect(mapHistory_, &QCheckBox::toggled, this, [this](bool) { paintMap(); });
    mapCtl->addWidget(mapHistory_);
    mapLay->addLayout(mapCtl);

    auto* legend = new QLabel(mapPage);
    legend->setTextFormat(Qt::RichText);
    legend->setText(QString::fromUtf8(
        "<span style='color:#1F5C50;'>\xE2\x96\xA0</span> Deco principal &nbsp;&nbsp;"
        "<span style='color:#2F7A6B;'>\xE2\x96\xA0</span> Repeteur &nbsp;&nbsp;"
        "<span style='color:#C45C26;'>\xE2\x96\xA0</span> Livebox &nbsp;&nbsp;"
        "<span style='color:#5A6A78;'>\xE2\x96\xA0</span> Client &nbsp;&nbsp;"
        "<span style='color:#8A8070;'>\xE2\x96\xA1</span> Historique (absent)"));
    legend->setContentsMargins(2, 0, 2, 2);
    mapLay->addWidget(legend);

    mapLay->addWidget(topology_, 1);

    auto* invPage = new QWidget(tabs);
    auto* rightLay = new QVBoxLayout(invPage);
    rightLay->setContentsMargins(8, 8, 8, 8);

    auto* filterRow = new QHBoxLayout();
    filterRow->addWidget(new QLabel(QStringLiteral("Afficher :"), invPage));
    presenceFilter_ = new QComboBox(invPage);
    presenceFilter_->addItems({kShowAll, kShowConn, kShowHist, kShowTech});
    connect(presenceFilter_, &QComboBox::currentTextChanged, this, &MainWindow::fillTable);
    filterRow->addWidget(presenceFilter_);
    filterRow->addWidget(new QLabel(QStringLiteral("Parent :"), invPage));
    parentFilter_ = new QComboBox(invPage);
    parentFilter_->addItem(kAllParents);
    parentFilter_->addItem(kUnattached);
    connect(parentFilter_, &QComboBox::currentTextChanged, this, &MainWindow::fillTable);
    filterRow->addWidget(parentFilter_, 1);
    rightLay->addLayout(filterRow);

    filter_ = new QLineEdit(invPage);
    filter_->setPlaceholderText(
        QStringLiteral("Recherche : nom / IP / MAC / fabricant / modele..."));
    connect(filter_, &QLineEdit::textChanged, this, &MainWindow::fillTable);
    rightLay->addWidget(filter_);

    table_ = new QTableWidget(invPage);
    configureInventoryTable(table_);
    connect(table_->horizontalHeader(), &QHeaderView::sectionClicked,
            this, &MainWindow::onHeaderClicked);
    connect(table_, &QTableWidget::customContextMenuRequested, this, &MainWindow::tableContextMenu);
    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { showDeviceRow(row); });

    auto* actions = new QHBoxLayout();
    auto addBtn = [&](const QString& label, void (MainWindow::*slot)()) {
        auto* b = new QPushButton(label, invPage);
        connect(b, &QPushButton::clicked, this, slot);
        actions->addWidget(b);
    };
    addBtn(QStringLiteral("Renommer"), &MainWindow::renameCurrent);
    addBtn(QStringLiteral("Copier IP"), &MainWindow::copyIp);
    addBtn(QStringLiteral("Copier MAC"), &MainWindow::copyMac);
    addBtn(QStringLiteral("Ping"), &MainWindow::pingCurrent);
    addBtn(QStringLiteral("HTTP"), &MainWindow::openHttpCurrent);
    addBtn(QStringLiteral("Completer (BDD web)"), &MainWindow::enrichCurrent);
    actions->addStretch(1);
    rightLay->addWidget(table_, 3);
    rightLay->addLayout(actions);

    detail_ = new QPlainTextEdit(invPage);
    detail_->setReadOnly(true);
    detail_->setPlaceholderText(
        QStringLiteral("Dossier : preuves et donnees. Un port ouvert n'identifie pas un service morfSystem."));
    detail_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // Dossier a gauche, panneau Piloter a droite (visible seulement pour un
    // appareil controlable, aujourd'hui Tuya).
    pilot_ = new PilotPanel(invPage);
    pilot_->setVisible(false);
    connect(pilot_, &PilotPanel::openSettingsRequested, this, &MainWindow::openSettingsFor);
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addWidget(detail_, 3);
    bottomRow->addWidget(pilot_, 1);
    rightLay->addLayout(bottomRow, 2);

    auto* histPage = new QWidget(tabs);
    auto* histLay = new QVBoxLayout(histPage);
    histLay->setContentsMargins(8, 8, 8, 8);
    auto* histHint = new QLabel(
        QStringLiteral("Appareils connus sans preuve de presence actuelle. "
                       "Tries par derniere vue, le plus recent en haut."),
        histPage);
    histHint->setWordWrap(true);
    histLay->addWidget(histHint);
    histTable_ = new QTableWidget(histPage);
    configureInventoryTable(histTable_);
    connect(histTable_, &QTableWidget::customContextMenuRequested, this, &MainWindow::tableContextMenu);
    connect(histTable_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { showHistRow(row); });
    histLay->addWidget(histTable_, 3);
    histDetail_ = new QPlainTextEdit(histPage);
    histDetail_->setReadOnly(true);
    histDetail_->setPlaceholderText(QStringLiteral("Dossier de l'appareil historique selectionne."));
    histDetail_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    histLay->addWidget(histDetail_, 2);

    log_ = new QPlainTextEdit(tabs);
    log_->setReadOnly(true);
    morfPage_ = new MorfServicesPage(tabs);
    connect(morfPage_, &MorfServicesPage::directoryChanged, this, &MainWindow::onMorfDirectoryChanged);
    tabs->addTab(mapPage, QStringLiteral("Carte"));
    tabs->addTab(invPage, QStringLiteral("Inventaire"));
    tabs->addTab(histPage, QStringLiteral("Historique"));
    tabs->addTab(morfPage_, QStringLiteral("Parc morfSystem"));
    tabs->addTab(log_, QStringLiteral("Journal"));
    setCentralWidget(tabs);

    // Double-clic sur une carte -> ouvrir la fiche dans l'Inventaire.
    connect(topology_, &TopologyView::deviceActivated, this,
            [this, tabs, invPage](const QString& id) {
                selectedId_ = id;
                tabs->setCurrentWidget(invPage);
                fillTable();
                for (int r = 0; r < table_->rowCount(); ++r) {
                    if (table_->item(r, 0)
                        && table_->item(r, 0)->data(Qt::UserRole).toString() == id) {
                        table_->selectRow(r);
                        table_->scrollToItem(table_->item(r, 0),
                                             QAbstractItemView::PositionAtCenter);
                        table_->setFocus();
                        break;
                    }
                }
            });

    scanAction_ = new QAction(QStringLiteral("Scan complet"), this);
    scanAction_->setToolTip(QStringLiteral("Repart de zero et reconstruit tout l'inventaire."));
    connect(scanAction_, &QAction::triggered, this, &MainWindow::startScan);
    rescanAction_ = new QAction(QStringLiteral("Rescan (mise a jour)"), this);
    rescanAction_->setToolTip(
        QStringLiteral("Garde la carte existante et met a jour : les appareils revus "
                       "restent, les absents passent en historique."));
    connect(rescanAction_, &QAction::triggered, this, &MainWindow::startRescan);
    auto* settingsAct = new QAction(QStringLiteral("Parametres"), this);
    connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);
    auto* exportAct = new QAction(QStringLiteral("Exporter JSON"), this);
    connect(exportAct, &QAction::triggered, this, &MainWindow::exportJson);
    auto* csvAct = new QAction(QStringLiteral("Exporter CSV"), this);
    connect(csvAct, &QAction::triggered, this, &MainWindow::exportCsv);

    auto* tb = addToolBar(QStringLiteral("Principal"));
    tb->addAction(scanAction_);
    tb->addAction(rescanAction_);
    tb->addSeparator();
    tb->addAction(settingsAct);
    tb->addAction(exportAct);
    tb->addAction(csvAct);
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("Fichier"));
    fileMenu->addAction(exportAct);
    fileMenu->addAction(csvAct);
    menuBar()->addMenu(QStringLiteral("Edition"))->addAction(settingsAct);
    auto* scanMenu = menuBar()->addMenu(QStringLiteral("Scan"));
    scanMenu->addAction(scanAction_);
    scanMenu->addAction(rescanAction_);
    scanMenu->addSeparator();
    autoTimer_ = new QTimer(this);
    autoTimer_->setInterval(5 * 60 * 1000);   // 5 minutes
    connect(autoTimer_, &QTimer::timeout, this, [this]() {
        if (scanAction_->isEnabled())         // ne pas empiler si un scan tourne
            startRescan();
    });
    auto* autoAct = new QAction(QStringLiteral("Rescan automatique (5 min)"), this);
    autoAct->setCheckable(true);
    connect(autoAct, &QAction::toggled, this, [this](bool on) {
        if (on)
            autoTimer_->start();
        else
            autoTimer_->stop();
        statusLabel_->setText(on ? QStringLiteral("Rescan automatique active (toutes les 5 min).")
                                 : QStringLiteral("Rescan automatique desactive."));
    });
    scanMenu->addAction(autoAct);

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("Aide"));
    auto* updateAct = new QAction(QStringLiteral("Rechercher les mises a jour..."), this);
    connect(updateAct, &QAction::triggered, this, [this]() {
        morfupdate::morfUpdateConfig cfg;
        cfg.owner = QStringLiteral("morfredus");
        cfg.repo = QStringLiteral("LanAtlas");
        cfg.currentVersion = QStringLiteral(LA_APP_VERSION);
        // false : afficher un retour meme si deja a jour (verification manuelle).
        morfupdate::checkAndNotify(this, QStringLiteral("LanAtlas"), cfg, false);
    });
    helpMenu->addAction(updateAct);
    auto* aboutAct = new QAction(QStringLiteral("A propos de LanAtlas"), this);
    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this, QStringLiteral("A propos de LanAtlas"),
            QStringLiteral(
                "<h3>LanAtlas <span style='color:#2F6F62;'>%1</span></h3>"
                "<p>Cartographie du reseau local : Livebox, mesh Deco et clients.</p>"
                "<p>Composant <b>morfSystem</b> - morfredus.<br>"
                "<a href='https://morfredus.fr' style='color:#2F6F62;'>morfredus.fr</a></p>")
                .arg(QStringLiteral(LA_APP_VERSION)));
    });
    helpMenu->addAction(aboutAct);

    auto* findSc = new QShortcut(QKeySequence::Find, this);
    connect(findSc, &QShortcut::activated, this, [this]() { filter_->setFocus(); filter_->selectAll(); });

    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setFixedWidth(200);
    progress_->setTextVisible(false);
    progressPct_ = new QLabel(QStringLiteral("0 %"));
    progressPct_->setMinimumWidth(52);
    progressPct_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressPct_->setStyleSheet(
        QStringLiteral("QLabel { color: #1A1208; font-weight: 700; font-size: 14px; "
                       "background: #EFE8DA; padding: 2px 8px; }"));
    statusLabel_ = new QLabel(QStringLiteral("Pret - renseigner Livebox et Deco dans Parametres, puis Scanner."));
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(progressPct_);
    statusBar()->addPermanentWidget(progress_);

    // Noms personnalises d'abord (appliques par rebuildShown), puis la carte de
    // la session precedente pour l'afficher immediatement.
    loadLabels();
    loadState();
}

MainWindow::~MainWindow()
{
    worker_.quit();
    worker_.wait(4000);
}

QString MainWindow::statePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir);
    return dir + QStringLiteral("/session.json");
}

void MainWindow::saveState() const
{
    QFile f(statePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(inventory_.toJson()).toJson(QJsonDocument::Compact));
}

void MainWindow::loadState()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return;
    inventory_.loadJson(doc.object());
    if (inventory_.devices().isEmpty())
        return;
    // Memoriser l'etat connecte charge : le prochain Rescan pourra marquer les
    // appareils apparus/disparus par rapport a la session precedente.
    prevConnected_.clear();
    for (const Device& d : inventory_.devices()) {
        if (d.presence == Device::Presence::Connected)
            prevConnected_.insert(d.id());
    }
    inventory_.log(QStringLiteral("Session precedente rechargee : %1 appareil(s). "
                                  "Rescan pour mettre a jour.")
                       .arg(inventory_.devices().size()));
    refreshViews();
    statusLabel_->setText(
        QStringLiteral("Carte de la session precedente affichee - Rescan pour mettre a jour."));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveState();
    QMainWindow::closeEvent(event);
}

QString MainWindow::labelsPath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir);
    return dir + QStringLiteral("/labels.json");
}

void MainWindow::loadLabels()
{
    QFile f(labelsPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        labels_.insert(it.key(), it.value().toString());
}

void MainWindow::saveLabels() const
{
    QJsonObject o;
    for (auto it = labels_.constBegin(); it != labels_.constEnd(); ++it)
        o.insert(it.key(), it.value());
    QFile f(labelsPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void MainWindow::renameCurrent()
{
    const Device* d = currentDevice();
    if (!d) {
        flashTip(QStringLiteral("Aucune ligne selectionnee"));
        return;
    }
    const QString id = d->id();
    const QString cur = labels_.value(id);
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Renommer l'appareil"),
        QStringLiteral("Nom personnalise (laisser vide pour revenir au nom detecte) :"),
        QLineEdit::Normal, cur.isEmpty() ? d->displayName() : cur, &ok);
    if (!ok)
        return;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        labels_.remove(id);
    else
        labels_.insert(id, trimmed);
    saveLabels();
    refreshViews();
    flashTip(trimmed.isEmpty() ? QStringLiteral("Nom reinitialise")
                               : QStringLiteral("Renomme : %1").arg(trimmed));
}

void MainWindow::startScan()
{
    launchScan(false);
}

void MainWindow::startRescan()
{
    launchScan(true);
}

void MainWindow::launchScan(bool incremental)
{
    const AppSettings s = AppSettings::load();
    // Reference pour les badges "nouveau / disparu" : etat connecte avant ce scan.
    prevConnected_.clear();
    for (const Device& d : shown_) {
        if (d.presence == Device::Presence::Connected)
            prevConnected_.insert(d.id());
    }
    scanAction_->setEnabled(false);
    rescanAction_->setEnabled(false);
    progress_->setValue(0);
    if (progressPct_)
        progressPct_->setText(QStringLiteral("0 %"));
    // Scan complet : on recadre la carte. Rescan : on garde la vue actuelle pour
    // ne pas desorienter l'utilisateur qui suit un appareil.
    if (!incremental)
        topology_->resetCamera();
    statusLabel_->setText(incremental ? QStringLiteral("Mise a jour en cours…")
                                      : QStringLiteral("Scan complet en cours…"));
    QMetaObject::invokeMethod(orchestrator_, "run", Qt::QueuedConnection, Q_ARG(AppSettings, s),
                              Q_ARG(bool, incremental));
}

void MainWindow::onProgress(int percent, const QString& phase)
{
    progress_->setValue(percent);
    if (progressPct_)
        progressPct_->setText(QStringLiteral("%1 %").arg(percent));
    statusLabel_->setText(phase);
}

void MainWindow::onFinished()
{
    scanAction_->setEnabled(true);
    rescanAction_->setEnabled(true);
    if (progressPct_)
        progressPct_->setText(QStringLiteral("100 %"));
    progress_->setValue(100);
    refreshViews();
    saveState();   // persiste la carte pour la prochaine ouverture
    if (enricher_)
        enricher_->enrichAfterScan(inventory_.wanIpv4);
    int connected = 0;
    for (const Device& d : shown_) {
        if (d.presence == Device::Presence::Connected)
            ++connected;
    }
    statusLabel_->setText(
        QStringLiteral("Termine - %1 connecte(s), %2 connu(s).  Livebox: %3  Deco: %4")
            .arg(connected)
            .arg(shown_.size())
            .arg(inventory_.liveboxSummary.isEmpty() ? QStringLiteral("n/a") : inventory_.liveboxSummary)
            .arg(inventory_.decoSummary.isEmpty() ? QStringLiteral("n/a") : inventory_.decoSummary));
}

void MainWindow::openSettings()
{
    openSettingsFor(QString(), QString());
}

void MainWindow::openSettingsFor(const QString& tuyaDeviceId, const QString& label)
{
    SettingsDialog dlg(this);
    dlg.setValues(AppSettings::load());
    if (!tuyaDeviceId.isEmpty())
        dlg.focusTuyaDevice(tuyaDeviceId, label);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.values().save();
        // La cle Tuya a pu changer : recharger le panneau Piloter.
        if (pilot_)
            pilot_->reloadDeviceKey();
    }
}

void MainWindow::exportJson()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter l'inventaire"),
                                                      QStringLiteral("lanatlas-inventory.json"),
                                                      QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("Export"), f.errorString());
        return;
    }
    QJsonObject root = inventory_.toJson();
    QJsonArray morf;
    if (morfPage_) {
        for (const MorfService& s : morfPage_->services()) {
            QJsonObject o;
            o["key"] = s.key;
            o["app"] = s.app;
            o["host"] = s.host;
            o["ip"] = s.ip;
            o["version"] = s.version;
            o["state"] = s.state;
            o["role"] = s.role;
            o["status_port"] = s.statusPort;
            o["stale"] = s.stale();
            o["status_url"] = s.statusUrl();
            QJsonArray caps;
            for (const QString& c : s.capabilities)
                caps.append(c);
            o["capabilities"] = caps;
            o["last_seen"] = s.lastSeen.toString(Qt::ISODate);
            o["heartbeat"] = s.datagram;
            morf.append(o);
        }
    }
    root["morf_services"] = morf;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void MainWindow::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter CSV"),
                                                      QStringLiteral("lanatlas-inventory.csv"),
                                                      QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Export"), f.errorString());
        return;
    }
    QString csv = QStringLiteral("nom,presence,ip,mac,vendor,model,parent,connexion,last_seen\n");
    for (const Device& d : shown_) {
        auto esc = [](QString s) {
            s.replace('"', QStringLiteral("\"\""));
            return QStringLiteral("\"") + s + QLatin1Char('"');
        };
        csv += esc(d.displayName()) + QLatin1Char(',')
               + esc(d.presenceLabel()) + QLatin1Char(',')
               + esc(d.ip) + QLatin1Char(',')
               + esc(normalizedMac(d.mac)) + QLatin1Char(',')
               + esc(d.vendor) + QLatin1Char(',')
               + esc(d.model) + QLatin1Char(',')
               + esc(d.parentName) + QLatin1Char(',')
               + esc(d.connection) + QLatin1Char(',')
               + esc(d.lastSeen.toString(Qt::ISODate)) + QLatin1Char('\n');
    }
    f.write(csv.toUtf8());
}

QString MainWindow::statsText() const
{
    int wifi = 0, eth = 0, morf = 0, conn = 0, hist = 0, tech = 0;
    for (const Device& d : shown_) {
        if (d.presence == Device::Presence::Connected)
            ++conn;
        else if (d.presence == Device::Presence::Technical)
            ++tech;
        else
            ++hist;
        const QString c = d.connection.toLower();
        if (d.presence == Device::Presence::Technical)
            continue;
        if (c.contains(QLatin1String("wifi")) || c.contains(QLatin1String("wlan"))
            || c.contains(QLatin1String("2.4")) || c.contains(QLatin1String("5g")))
            ++wifi;
        else if (c.contains(QLatin1String("eth")) || c.contains(QLatin1String("wire")))
            ++eth;
        if (!d.morfApps.isEmpty())
            ++morf;
    }
    return QStringLiteral("%1 connectes  ·  %2 historique  ·  %3 adresse(s) technique(s)  ·  "
                          "%4 hote(s) avec preuve morfSystem  ·  %5 Wi-Fi  ·  %6 filaire")
        .arg(conn)
        .arg(hist)
        .arg(tech)
        .arg(morf)
        .arg(wifi)
        .arg(eth);
}

void MainWindow::rebuildShown()
{
    shown_ = inventory_.devices();
    if (morfPage_)
        attachMorfServices(shown_, morfPage_->services());
    // Identite Tuya superposee par IP, avant le calcul de presence (une annonce
    // recente compte comme preuve de presence).
    if (tuya_)
        attachTuyaDevices(shown_, tuya_->announces());
    classifyPresence(shown_);
    for (Device& d : shown_) {
        const QString label = labels_.value(d.id());
        if (!label.isEmpty())
            d.customName = label;
        // Badges "nouveau / disparu" par rapport a l'etat au dernier scan.
        if (!prevConnected_.isEmpty()) {
            const bool conn = d.presence == Device::Presence::Connected;
            d.justAppeared = conn && !prevConnected_.contains(d.id());
            d.justGone = !conn && prevConnected_.contains(d.id());
        }
    }
}

void MainWindow::onMorfDirectoryChanged()
{
    if (inventory_.devices().isEmpty())
        return;
    refreshViews();
}

void MainWindow::refreshViews()
{
    rebuildShown();
    const QString keepParent = parentFilter_->currentText();
    parentFilter_->blockSignals(true);
    parentFilter_->clear();
    parentFilter_->addItem(kAllParents);
    parentFilter_->addItem(kUnattached);
    QStringList parents;
    for (const Device& d : shown_) {
        if (d.presence == Device::Presence::Technical)
            continue;
        if (!d.parentName.isEmpty() && !parents.contains(d.parentName))
            parents << d.parentName;
    }
    parents.sort(Qt::CaseInsensitive);
    parentFilter_->addItems(parents);
    const int idx = parentFilter_->findText(keepParent);
    parentFilter_->setCurrentIndex(idx >= 0 ? idx : 0);
    parentFilter_->blockSignals(false);

    statsLabel_->setText(statsText());
    paintMap();
    fillTable();
    fillHistoryTable();
    log_->setPlainText(inventory_.logLines.join('\n'));
}

void MainWindow::paintMap()
{
    if (!topology_)
        return;
    const QString link = mapConnFilter_ ? mapConnFilter_->currentText() : QString();
    auto passesLink = [&](const Device& d) {
        if (link.isEmpty() || link == QLatin1String("Toutes") || isInfra(d))
            return true;
        const QString blob = (d.connection + QLatin1Char(' ') + d.band).toLower();
        const bool wired = blob.contains(QLatin1String("filaire")) || blob.contains(QLatin1String("wired"))
                           || blob.contains(QLatin1String("ether")) || blob.contains(QLatin1String("lan"))
                           || blob.contains(QLatin1String("plc"));
        const bool wifi = blob.contains(QLatin1String("wi-fi")) || blob.contains(QLatin1String("wifi"))
                          || blob.contains(QLatin1String("ghz")) || blob.contains(QLatin1String("band"));
        if (link == QLatin1String("Filaire"))
            return wired && !wifi;
        if (link == QLatin1String("Wi-Fi"))
            return wifi;
        return true;
    };

    QVector<Device> conn;
    QVector<Device> hist;
    for (const Device& d : shown_) {
        if (d.presence == Device::Presence::Connected) {
            if (passesLink(d))
                conn.append(d);
        } else if (d.presence == Device::Presence::Historical && mapHistory_
                   && mapHistory_->isChecked()) {
            hist.append(d);
        }
    }
    topology_->setInventory(conn, hist);
}

void MainWindow::onHeaderClicked(int column)
{
    if (column == sortColumn_)
        sortAsc_ = !sortAsc_;
    else {
        sortColumn_ = column;
        sortAsc_ = (column != 10);
    }
    fillTable();
}

void MainWindow::fillTable()
{
    const QString needle = filter_->text().trimmed();
    const QString parentWant = parentFilter_->currentText();
    const QString show = presenceFilter_->currentText();

    QVector<const Device*> conn;
    QVector<const Device*> hist;
    QVector<const Device*> tech;
    for (const Device& d : shown_) {
        if (show == kShowTech) {
            if (d.presence != Device::Presence::Technical)
                continue;
        } else if (show == kShowConn) {
            if (d.presence != Device::Presence::Connected)
                continue;
        } else if (show == kShowHist) {
            if (d.presence != Device::Presence::Historical)
                continue;
        } else if (d.presence == Device::Presence::Technical) {
            continue;
        }
        if (parentWant == kUnattached) {
            if (!isUnattached(d))
                continue;
        } else if (parentWant != kAllParents && d.parentName != parentWant) {
            continue;
        }
        if (!needle.isEmpty()) {
            const QString blob = d.displayName() + d.hostname + d.ip + normalizedMac(d.mac)
                                 + d.vendor + d.model + d.parentName;
            if (!blob.contains(needle, Qt::CaseInsensitive))
                continue;
        }
        if (d.presence == Device::Presence::Connected)
            conn.append(&d);
        else if (d.presence == Device::Presence::Technical)
            tech.append(&d);
        else
            hist.append(&d);
    }

    sortGroup(conn, sortColumn_, sortAsc_, false);
    sortGroup(hist, sortColumn_, sortAsc_, sortColumn_ < 0);
    sortGroup(tech, sortColumn_, sortAsc_, false);

    struct Line {
        bool header = false;
        QString title;
        const Device* d = nullptr;
    };
    QVector<Line> lines;
    auto addSec = [&](const QString& title, const QVector<const Device*>& g) {
        if (g.isEmpty())
            return;
        lines.append({true, title, nullptr});
        for (const Device* p : g)
            lines.append({false, QString(), p});
    };
    if (show == kShowTech)
        addSec(QStringLiteral("Adresses techniques (%1)").arg(tech.size()), tech);
    else {
        addSec(QStringLiteral("Appareils connectes (%1)").arg(conn.size()), conn);
        addSec(QStringLiteral("Historique / actuellement absents (%1)").arg(hist.size()), hist);
    }

    // Preserver la position de lecture : sans cela, chaque actualisation (scan,
    // enrichissement web) faisait sauter la ligne selectionnee hors de l'ecran.
    const int savedScroll = table_->verticalScrollBar()->value();
    const bool tableHadFocus = table_->hasFocus();

    table_->clearSpans();
    table_->setRowCount(lines.size());
    const int cols = table_->columnCount();
    const QColor hdrBg(0xE6, 0xE0, 0xD2);
    for (int r = 0; r < lines.size(); ++r) {
        if (lines[r].header) {
            auto* it = new QTableWidgetItem(lines[r].title);
            it->setData(Qt::UserRole, QString());
            QFont f = it->font();
            f.setBold(true);
            it->setFont(f);
            it->setBackground(hdrBg);
            it->setForeground(QColor(0x1A, 0x12, 0x08));
            table_->setItem(r, 0, it);
            for (int c = 1; c < cols; ++c) {
                auto* empty = new QTableWidgetItem();
                empty->setBackground(hdrBg);
                table_->setItem(r, c, empty);
            }
            table_->setSpan(r, 0, 1, cols);
            continue;
        }
        const Device& d = *lines[r].d;
        writeDeviceRow(table_, r, d);
    }
    table_->resizeColumnsToContents();
    int found = -1;
    for (int r = 0; r < lines.size(); ++r) {
        if (table_->item(r, 0) && table_->item(r, 0)->data(Qt::UserRole).toString() == selectedId_
            && !selectedId_.isEmpty()) {
            found = r;
            break;
        }
    }
    table_->blockSignals(true);
    if (found >= 0)
        table_->selectRow(found);
    else {
        int first = -1;
        for (int r = 0; r < lines.size(); ++r) {
            if (table_->item(r, 0) && !table_->item(r, 0)->data(Qt::UserRole).toString().isEmpty()) {
                first = r;
                break;
            }
        }
        if (first >= 0)
            table_->selectRow(first);
        else
            table_->clearSelection();
    }
    table_->blockSignals(false);
    // Ne pas bouger la vue lors d'une actualisation : l'ascenseur reste
    // exactement ou l'utilisateur l'a laisse, meme si la ligne selectionnee
    // n'est plus visible. On ne force aucun defilement vers la selection.
    table_->verticalScrollBar()->setValue(savedScroll);
    if (tableHadFocus)
        table_->setFocus();
    if (table_->currentRow() >= 0)
        showDeviceRow(table_->currentRow());
    else
        detail_->clear();
}

void MainWindow::fillHistoryTable()
{
    if (!histTable_)
        return;
    QVector<const Device*> hist;
    for (const Device& d : shown_) {
        if (d.presence == Device::Presence::Historical)
            hist.append(&d);
    }
    sortGroup(hist, 10, false, true);
    histTable_->setRowCount(hist.size());
    for (int r = 0; r < hist.size(); ++r)
        writeDeviceRow(histTable_, r, *hist[r]);
    histTable_->resizeColumnsToContents();
    int found = -1;
    for (int r = 0; r < hist.size(); ++r) {
        if (histTable_->item(r, 0)
            && histTable_->item(r, 0)->data(Qt::UserRole).toString() == histSelectedId_
            && !histSelectedId_.isEmpty()) {
            found = r;
            break;
        }
    }
    histTable_->blockSignals(true);
    if (found >= 0)
        histTable_->selectRow(found);
    else if (!hist.isEmpty())
        histTable_->selectRow(0);
    histTable_->blockSignals(false);
    if (histTable_->currentRow() >= 0)
        showHistRow(histTable_->currentRow());
    else if (histDetail_)
        histDetail_->clear();
}

const Device* MainWindow::currentDevice() const
{
    auto from = [this](QTableWidget* t) -> const Device* {
        if (!t)
            return nullptr;
        const int row = t->currentRow();
        if (row < 0 || !t->item(row, 0))
            return nullptr;
        const QString id = t->item(row, 0)->data(Qt::UserRole).toString();
        if (id.isEmpty())
            return nullptr;
        for (const Device& d : shown_) {
            if (d.id() == id)
                return &d;
        }
        return nullptr;
    };
    if (histTable_ && histTable_->hasFocus()) {
        if (const Device* d = from(histTable_))
            return d;
    }
    if (const Device* d = from(table_))
        return d;
    return from(histTable_);
}

void MainWindow::showDeviceFromTable(QTableWidget* table, int row, QPlainTextEdit* dest,
                                     QString* selId)
{
    if (!dest || !table)
        return;
    if (row < 0 || !table->item(row, 0)) {
        dest->clear();
        return;
    }
    const QString id = table->item(row, 0)->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return;
    for (const Device& d : shown_) {
        if (d.id() == id) {
            QString extra;
            extra += QStringLiteral(
                "\n--- Preuves ---\n"
                "Un port TCP ouvert est un indice, jamais une identite morfSystem.\n");
            extra += QStringLiteral("Preuve courte : ") + d.proofShort() + QLatin1Char('\n');
            if (!d.morfApps.isEmpty()) {
                extra += QStringLiteral("Apps : ") + d.morfApps.join(QStringLiteral(", "))
                         + QLatin1Char('\n');
            } else {
                extra += QStringLiteral("Aucune preuve morfSystem sur cet hote.\n");
            }
            extra += QStringLiteral("\nBases web (OUI/MAC, WAN) : extra JSON web_maclookup / web_wan.\n");
            // Ne reecrire que si le contenu a change, et garder la position de
            // lecture : sinon chaque actualisation faisait remonter le panneau
            // en haut et clignoter le texte.
            const QString newText = d.detailText() + extra;
            if (dest->toPlainText() != newText) {
                const int sb = dest->verticalScrollBar()->value();
                dest->setPlainText(newText);
                dest->verticalScrollBar()->setValue(sb);
            }
            if (selId)
                *selId = id;
            if (enricher_ && !d.extra.contains(QStringLiteral("web_maclookup"))
                && !d.extra.contains(QStringLiteral("web_macvendors")))
                enricher_->enrichMac(d.id(), d.mac);
            return;
        }
    }
    dest->clear();
}

void MainWindow::updatePilot(const Device* d)
{
    if (!pilot_)
        return;
    const bool tuya = d && (d->capabilities.contains(QLatin1String("tuya-lan"))
                            || d->sources.contains(QLatin1String("tuya")));
    if (tuya && !d->ip.isEmpty()) {
        const QString devId = d->extra.value(QStringLiteral("tuya_device_id")).toString();
        pilot_->setDevice(d->ip, devId, d->displayName());
        pilot_->setVisible(true);
    } else {
        pilot_->setVisible(false);
    }
}

void MainWindow::showDeviceRow(int row)
{
    showDeviceFromTable(table_, row, detail_, &selectedId_);
    // Mettre a jour le panneau Piloter selon l'appareil selectionne.
    const Device* sel = nullptr;
    for (const Device& d : shown_) {
        if (d.id() == selectedId_) {
            sel = &d;
            break;
        }
    }
    updatePilot(sel);
}

void MainWindow::showHistRow(int row)
{
    showDeviceFromTable(histTable_, row, histDetail_, &histSelectedId_);
}

// Petit retour visuel a l'endroit du clic : confirme (ou non) l'action.
static void flashTip(const QString& msg)
{
    QToolTip::showText(QCursor::pos(), msg);
}

void MainWindow::copyIp()
{
    const Device* d = currentDevice();
    if (!d) {
        flashTip(QStringLiteral("Aucune ligne selectionnee"));
        return;
    }
    if (d->ip.isEmpty()) {
        flashTip(QStringLiteral("Pas d'adresse IP"));
        return;
    }
    QApplication::clipboard()->setText(d->ip);
    flashTip(QStringLiteral("IP copiee : %1").arg(d->ip));
}

void MainWindow::copyMac()
{
    const Device* d = currentDevice();
    if (!d) {
        flashTip(QStringLiteral("Aucune ligne selectionnee"));
        return;
    }
    const QString m = normalizedMac(d->mac);
    if (m.size() < 17) {
        flashTip(QStringLiteral("Pas d'adresse MAC"));
        return;
    }
    QApplication::clipboard()->setText(m);
    flashTip(QStringLiteral("MAC copiee : %1").arg(m));
}

void MainWindow::pingCurrent()
{
    const Device* d = currentDevice();
    if (!d || d->ip.isEmpty()) {
        flashTip(QStringLiteral("Ping impossible : pas d'adresse IP"));
        return;
    }
    const QString ip = d->ip;
    flashTip(QStringLiteral("Ping de %1 en cours...").arg(ip));
    auto* p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int, QProcess::ExitStatus) {
        QMessageBox::information(this, QStringLiteral("Ping"),
                                 QString::fromLocal8Bit(p->readAllStandardOutput()
                                                        + p->readAllStandardError()));
        p->deleteLater();
    });
#ifdef Q_OS_WIN
    p->start(QStringLiteral("ping"), {QStringLiteral("-n"), QStringLiteral("1"),
                                      QStringLiteral("-w"), QStringLiteral("1000"), ip});
#else
    p->start(QStringLiteral("ping"), {QStringLiteral("-c"), QStringLiteral("1"),
                                      QStringLiteral("-W"), QStringLiteral("1"), ip});
#endif
}

void MainWindow::onWebDeviceUpdated(const QString&)
{
    const QString keep = selectedId_;
    rebuildShown();
    statsLabel_->setText(statsText());
    paintMap();
    fillTable();
    fillHistoryTable();
    log_->setPlainText(inventory_.logLines.join('\n'));
    selectedId_ = keep;
}

void MainWindow::enrichCurrent()
{
    const Device* d = currentDevice();
    if (!d || !enricher_) {
        flashTip(QStringLiteral("Aucune ligne selectionnee"));
        return;
    }
    const bool already = d->extra.contains(QStringLiteral("web_maclookup"))
                         || d->extra.contains(QStringLiteral("web_macvendors"));
    flashTip(already ? QStringLiteral("Deja complete - actualisation...")
                     : QStringLiteral("Mise a jour en cours (bases OUI web)..."));
    enricher_->enrichMac(d->id(), d->mac);
    statusLabel_->setText(QStringLiteral("Interrogation des bases OUI (maclookup.app / macvendors.com)..."));
}

void MainWindow::webSearchCurrent()
{
    enrichCurrent();
}

void MainWindow::macLookupCurrent()
{
    enrichCurrent();
}

void MainWindow::openHttpCurrent()
{
    const Device* d = currentDevice();
    if (!d) {
        flashTip(QStringLiteral("Aucune ligne selectionnee"));
        return;
    }
    if (d->ip.isEmpty()) {
        flashTip(QStringLiteral("Pas d'IP : ouverture HTTP impossible"));
        return;
    }
    WebLookup::openHttp(*d);
    flashTip(QStringLiteral("Ouverture de %1 dans le navigateur").arg(d->ip));
}

void MainWindow::tableContextMenu(const QPoint& pos)
{
    const Device* d = currentDevice();
    if (!d)
        return;
    QMenu m(this);
    m.addAction(QStringLiteral("Renommer..."), this, &MainWindow::renameCurrent);
    m.addSeparator();
    m.addAction(QStringLiteral("Copier IP"), this, &MainWindow::copyIp);
    m.addAction(QStringLiteral("Copier MAC"), this, &MainWindow::copyMac);
    m.addAction(QStringLiteral("Ping"), this, &MainWindow::pingCurrent);
    m.addAction(QStringLiteral("Ouvrir HTTP/HTTPS"), this, &MainWindow::openHttpCurrent);
    m.addSeparator();
    m.addAction(QStringLiteral("Completer depuis les BDD web (OUI/MAC)"), this, &MainWindow::enrichCurrent);
    if (!d->morfApps.isEmpty() && !d->ip.isEmpty()) {
        m.addSeparator();
        for (const MorfService& s : morfPage_->services()) {
            if (s.ip != d->ip || s.stale())
                continue;
            const QString url = s.statusUrl();
            m.addAction(QStringLiteral("morf /status  ") + s.app, this, [url]() { WebLookup::openStatus(url); });
        }
    }
    QTableWidget* tw = qobject_cast<QTableWidget*>(sender());
    QWidget* vp = tw ? tw->viewport() : table_->viewport();
    m.exec(vp->mapToGlobal(pos));
}
