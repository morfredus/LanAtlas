#include "ui/MorfServicesPage.h"
#include "scan/BeaconListener.h"

#include <QAbstractItemView>
#include <QDesktopServices>
#include <QFont>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {
const int kKeyRole = Qt::UserRole;
const int kKindRole = Qt::UserRole + 1;

void paintRow(QTreeWidgetItem* item, const MorfService& s)
{
    const QString st = s.state.toLower();
    const bool bad = !s.stale()
                     && (st.contains(QLatin1String("err")) || st == QLatin1String("fail")
                         || st == QLatin1String("down") || st == QLatin1String("offline")
                         || st.contains(QLatin1String("fault")) || st.contains(QLatin1String("crit")));
    const bool warn = !s.stale() && !bad
                      && (st.contains(QLatin1String("warn")) || st == QLatin1String("degraded")
                          || st.contains(QLatin1String("degrad")));
    QColor ink(0x1A, 0x12, 0x08);
    QColor bg(0xFF, 0xFE, 0xFA);
    if (s.stale()) {
        ink = QColor(0x6E, 0x68, 0x5C);
        bg = QColor(0xE8, 0xE2, 0xD4);
    } else if (bad) {
        ink = QColor(0xB4, 0x23, 0x18);
        bg = QColor(0xFD, 0xEC, 0xEC);
    } else if (warn) {
        ink = QColor(0x9A, 0x67, 0x00);
        bg = QColor(0xFF, 0xF4, 0xD6);
    }
    for (int col = 0; col < item->columnCount(); ++col) {
        item->setForeground(col, ink);
        item->setBackground(col, bg);
    }
}
} // namespace

MorfServicesPage::MorfServicesPage(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setText(QStringLiteral(
        "Ecoute des heartbeats morfBeacon (UDP 45454). Les services s'annoncent "
        "tout seuls ; ils sont regroupes par le nom d'hote du datagramme."));
    lay->addWidget(summary_);

    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(QStringLiteral("Filtrer : hote, application, capacite, IP..."));
    connect(filter_, &QLineEdit::textChanged, this, &MorfServicesPage::rebuild);
    lay->addWidget(filter_);

    auto* split = new QSplitter(Qt::Horizontal, this);
    tree_ = new QTreeWidget(split);
    tree_->setColumnCount(6);
    tree_->setHeaderLabels({QStringLiteral("Service"), QStringLiteral("Version"),
                            QStringLiteral("Etat"), QStringLiteral("Port"),
                            QStringLiteral("Capacites"), QStringLiteral("Vu")});
    tree_->setUniformRowHeights(true);
    tree_->setRootIsDecorated(true);
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->header()->setStretchLastSection(true);

    auto* right = new QWidget(split);
    auto* rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    auto* bar = new QHBoxLayout();
    auto* openBtn = new QPushButton(QStringLiteral("Ouvrir /status"), right);
    openBtn->setObjectName(QStringLiteral("openStatus"));
    connect(openBtn, &QPushButton::clicked, this, [this]() {
        for (const MorfService& s : listener_->services()) {
            if (s.key == selectedKey_) {
                const QString u = s.statusUrl();
                if (!u.isEmpty())
                    QDesktopServices::openUrl(QUrl(u));
                return;
            }
        }
    });
    bar->addWidget(openBtn);
    auto* healthBtn = new QPushButton(QStringLiteral("Ouvrir /healthz"), right);
    connect(healthBtn, &QPushButton::clicked, this, [this]() {
        for (const MorfService& s : listener_->services()) {
            if (s.key == selectedKey_ && !s.ip.isEmpty() && s.statusPort) {
                QDesktopServices::openUrl(QUrl(QStringLiteral("http://%1:%2/healthz")
                                                   .arg(s.ip).arg(s.statusPort)));
                return;
            }
        }
    });
    bar->addWidget(healthBtn);
    bar->addStretch(1);
    rightLay->addLayout(bar);
    detail_ = new QPlainTextEdit(right);
    detail_->setReadOnly(true);
    detail_->setPlaceholderText(QStringLiteral(
        "Choisir un service : heartbeat UDP, puis /status si le port repond."));
    rightLay->addWidget(detail_, 1);
    split->addWidget(tree_);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);

    listener_ = new BeaconListener(this);
    if (!listener_->start()) {
        summary_->setText(QStringLiteral("Impossible d'ecouter UDP 45454 : ")
                          + listener_->bindError()
                          + QStringLiteral(". Un autre programme monopolise peut-etre le port."));
    }
    connect(listener_, &BeaconListener::changed, this, [this]() {
        rebuild();
        emit directoryChanged();
    });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &MorfServicesPage::onSelectionChanged);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item || item->data(0, kKindRole).toString() != QLatin1String("service"))
            return;
        selectedKey_ = item->data(0, kKeyRole).toString();
        for (const MorfService& s : listener_->services()) {
            if (s.key == selectedKey_) {
                const QString u = s.statusUrl();
                if (!u.isEmpty())
                    QDesktopServices::openUrl(QUrl(u));
                return;
            }
        }
    });

    http_ = new QNetworkAccessManager(this);

    auto* staleTimer = new QTimer(this);
    connect(staleTimer, &QTimer::timeout, this, &MorfServicesPage::tickStale);
    staleTimer->start(2000);
}

QVector<MorfService> MorfServicesPage::services() const
{
    return listener_ ? listener_->services() : QVector<MorfService>{};
}

void MorfServicesPage::tickStale()
{
    if (!tree_->topLevelItemCount())
        return;
    QHash<QString, MorfService> byKey;
    int live = 0;
    const auto all = listener_->services();
    for (const MorfService& s : all) {
        byKey.insert(s.key, s);
        if (!s.stale())
            ++live;
    }
    QSet<QString> hosts;
    for (const MorfService& s : all)
        hosts.insert(s.host.isEmpty() ? s.ip : s.host);
    if (listener_->bindError().isEmpty()) {
        summary_->setText(QStringLiteral(
            "%1 hote(s), %2 service(s) entendus (%3 vus il y a moins de 45 s). "
            "UDP 45454 - regroupement par nom d'hote beacon.")
                              .arg(hosts.size())
                              .arg(all.size())
                              .arg(live));
    }
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* hostItem = tree_->topLevelItem(i);
        for (int j = 0; j < hostItem->childCount(); ++j) {
            QTreeWidgetItem* row = hostItem->child(j);
            const QString key = row->data(0, kKeyRole).toString();
            if (!byKey.contains(key))
                continue;
            const MorfService& s = byKey[key];
            row->setText(2, s.stale() ? QStringLiteral("silence") : s.state);
            row->setText(5, s.lastSeen.toString(QStringLiteral("HH:mm:ss")));
            paintRow(row, s);
        }
    }
}

void MorfServicesPage::rebuild()
{
    QSet<QString> expanded;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = tree_->topLevelItem(i);
        if (it->isExpanded())
            expanded.insert(it->data(0, kKeyRole).toString());
    }
    const QString keep = selectedKey_;

    const auto all = listener_->services();
    const QString needle = filter_ ? filter_->text().trimmed() : QString();
    auto matches = [&](const MorfService& s) {
        if (needle.isEmpty())
            return true;
        const QString blob = s.app + s.host + s.ip + s.version + s.state
                             + s.capabilities.join(QLatin1Char(' ')) + s.key;
        return blob.contains(needle, Qt::CaseInsensitive);
    };
    QMap<QString, QVector<MorfService>> byHost;
    for (const MorfService& s : all) {
        if (!matches(s))
            continue;
        const QString host = s.host.isEmpty() ? s.ip : s.host;
        byHost[host].append(s);
    }

    tree_->clear();
    int live = 0;
    for (const MorfService& s : all) {
        if (!s.stale())
            ++live;
    }
    if (listener_->bindError().isEmpty()) {
        summary_->setText(QStringLiteral(
            "%1 hote(s), %2 service(s) entendus (%3 vus il y a moins de 45 s). "
            "UDP 45454 - regroupement par nom d'hote beacon.")
                              .arg(byHost.size())
                              .arg(all.size())
                              .arg(live));
    }

    for (auto it = byHost.begin(); it != byHost.end(); ++it) {
        auto list = it.value();
        std::sort(list.begin(), list.end(), [](const MorfService& a, const MorfService& b) {
            return a.app.compare(b.app, Qt::CaseInsensitive) < 0;
        });
        QStringList ips;
        int hostLive = 0;
        for (const MorfService& s : list) {
            if (!ips.contains(s.ip) && !s.ip.isEmpty())
                ips << s.ip;
            if (!s.stale())
                ++hostLive;
        }
        auto* hostItem = new QTreeWidgetItem(tree_);
        const QString hostKey = QStringLiteral("host:") + it.key();
        hostItem->setData(0, kKeyRole, hostKey);
        hostItem->setData(0, kKindRole, QStringLiteral("host"));
        hostItem->setText(0, it.key() + QStringLiteral("  (") + ips.join(QStringLiteral(", "))
                              + QStringLiteral(")  -  ")
                              + QString::number(list.size()) + QStringLiteral(" service(s), ")
                              + QString::number(hostLive) + QStringLiteral(" actif(s)"));
        hostItem->setFirstColumnSpanned(true);
        QFont hf = hostItem->font(0);
        hf.setBold(true);
        hostItem->setFont(0, hf);
        if (expanded.contains(hostKey) || expanded.isEmpty())
            hostItem->setExpanded(true);

        for (const MorfService& s : list) {
            auto* row = new QTreeWidgetItem(hostItem);
            row->setData(0, kKeyRole, s.key);
            row->setData(0, kKindRole, QStringLiteral("service"));
            row->setText(0, s.app);
            row->setText(1, s.version);
            row->setText(2, s.stale() ? QStringLiteral("silence") : s.state);
            row->setText(3, s.statusPort ? QString::number(s.statusPort) : QString());
            row->setText(4, s.capabilities.join(QStringLiteral(", ")));
            row->setText(5, s.lastSeen.toString(QStringLiteral("HH:mm:ss")));
            paintRow(row, s);
            if (s.key == keep) {
                tree_->blockSignals(true);
                tree_->setCurrentItem(row);
                tree_->blockSignals(false);
            }
        }
    }
    tree_->resizeColumnToContents(0);
    tree_->resizeColumnToContents(1);
    tree_->resizeColumnToContents(2);
    tree_->resizeColumnToContents(3);
}

void MorfServicesPage::onSelectionChanged()
{
    QTreeWidgetItem* item = tree_->currentItem();
    if (!item) {
        detail_->clear();
        selectedKey_.clear();
        return;
    }
    const QString kind = item->data(0, kKindRole).toString();
    if (kind == QLatin1String("host")) {
        selectedKey_ = item->data(0, kKeyRole).toString();
        QStringList lines;
        lines << item->text(0);
        lines << QString();
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* c = item->child(i);
            lines << c->text(0) + QStringLiteral("  ") + c->text(1) + QStringLiteral("  ")
                         + c->text(2) + QStringLiteral("  :") + c->text(3);
        }
        detail_->setPlainText(lines.join('\n'));
        return;
    }
    selectedKey_ = item->data(0, kKeyRole).toString();
    for (const MorfService& s : listener_->services()) {
        if (s.key == selectedKey_) {
            showService(s);
            fetchStatus(s);
            return;
        }
    }
}

void MorfServicesPage::showService(const MorfService& s)
{
    QStringList lines;
    lines << QStringLiteral("Application : ") + s.app;
    lines << QStringLiteral("Instance    : ") + s.key;
    lines << QStringLiteral("Hote        : ") + s.host;
    lines << QStringLiteral("IP source   : ") + s.ip;
    lines << QStringLiteral("Role        : ") + s.role;
    lines << QStringLiteral("Version     : ") + s.version;
    lines << QStringLiteral("Etat        : ") + (s.stale() ? QStringLiteral("silence (>45 s)") : s.state);
    lines << QStringLiteral("Port HTTP   : ") + (s.statusPort ? QString::number(s.statusPort) : QStringLiteral("n/a"));
    lines << QStringLiteral("/status     : ") + s.statusUrl();
    lines << QStringLiteral("Capacites   : ") + s.capabilities.join(QStringLiteral(", "));
    lines << QStringLiteral("Vu          : ") + s.lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    lines << QString();
    lines << QStringLiteral("Heartbeat (UDP) :");
    lines << QString::fromUtf8(QJsonDocument(s.datagram).toJson(QJsonDocument::Indented));
    detail_->setPlainText(lines.join('\n'));
}

void MorfServicesPage::fetchStatus(const MorfService& s)
{
    const QString url = s.statusUrl();
    if (url.isEmpty())
        return;
    pendingStatusKey_ = s.key;
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(2500);
    req.setRawHeader("User-Agent", "LanAtlas/0.1 (morfsystem)");
    QNetworkReply* reply = http_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key = s.key]() {
        reply->deleteLater();
        if (selectedKey_ != key || pendingStatusKey_ != key)
            return;
        QString extra;
        if (reply->error() != QNetworkReply::NoError) {
            extra = QStringLiteral("\n/status : ") + reply->errorString();
        } else {
            extra = QStringLiteral("\n/status HTTP :\n")
                    + QString::fromUtf8(reply->readAll());
        }
        detail_->setPlainText(detail_->toPlainText() + extra);
    });
}
