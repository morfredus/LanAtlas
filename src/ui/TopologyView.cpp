#include "ui/TopologyView.h"

#include <QFont>
#include <QFontMetrics>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPainter>
#include <QPen>
#include <QPalette>
#include <QScrollBar>
#include <QtGlobal>
#include <QFileDialog>
#include <QImage>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTransform>
#include <QWheelEvent>
#include <algorithm>

namespace {
const QColor kInk(0x1A, 0x1A, 0x1A);
const QColor kMuted(0x3D, 0x3A, 0x34);
const QColor kPaper(0xFF, 0xFC, 0xF4);
const QColor kScene(0xE6, 0xE0, 0xD2);
const QColor kLine(0x6A, 0x63, 0x56);
const QColor kBoxStripe(0xC4, 0x5C, 0x26);
const QColor kDecoStripe(0x2F, 0x7A, 0x6B);
const QColor kDecoMain(0x1F, 0x5C, 0x50);
const QColor kClientStripe(0x5A, 0x6A, 0x78);

QGraphicsSimpleTextItem* addLabel(QGraphicsScene* sc, const QString& text, qreal x, qreal y,
                                  const QFont& font, const QColor& color = kInk)
{
    auto* t = sc->addSimpleText(text, font);
    t->setBrush(color);
    t->setPos(x, y);
    t->setZValue(2);
    return t;
}

bool looksMainDeco(const Device& d)
{
    const QString blob = (d.notes + d.extra.value(QStringLiteral("role")).toString()
                          + d.extra.value(QStringLiteral("device_role")).toString())
                             .toLower();
    return blob.contains(QLatin1String("main")) || blob.contains(QLatin1String("primary"))
           || blob.contains(QLatin1String("master")) || blob.contains(QLatin1String("principal"));
}

const QColor kGhostPaper(0xEE, 0xE6, 0xD8);
const QColor kGhostStripe(0x8A, 0x80, 0x70);
const QColor kGhostInk(0x4A, 0x44, 0x3C);

QFont titleFont()
{
    QFont f(QStringLiteral("Segoe UI"), 11, QFont::DemiBold);
    if (f.family() != QLatin1String("Segoe UI"))
        f = QFont(QStringLiteral("DejaVu Sans"), 11, QFont::DemiBold);
    return f;
}

QFont smallFont()
{
    QFont f = titleFont();
    f.setPointSize(9);
    f.setWeight(QFont::Normal);
    return f;
}

QString elideLine(const QFont& font, const QString& text, int maxPx)
{
    return QFontMetrics(font).elidedText(text, Qt::ElideRight, qMax(24, maxPx));
}

void addClippedLine(QGraphicsRectItem* card, const QString& text, qreal x, qreal y,
                    const QFont& font, const QColor& color, int maxPx)
{
    auto* t = new QGraphicsSimpleTextItem(elideLine(font, text, maxPx), card);
    t->setFont(font);
    t->setBrush(color);
    t->setPos(x, y);
    t->setZValue(2);
    if (text != t->text())
        t->setToolTip(text);
}

qreal addCard(QGraphicsScene* sc, qreal x, qreal y, qreal w, const Device& d, const QColor& stripe,
              const QString& badge, bool compact, bool ghost)
{
    QStringList body;
    if (!badge.isEmpty())
        body << badge;
    if (!d.ip.isEmpty())
        body << d.ip;
    const QString link = d.linkShort();
    if (!link.isEmpty())
        body << link;
    if (!compact) {
        if (!d.morfApps.isEmpty())
            body << QStringLiteral("morf: %1 service(s)").arg(d.morfApps.size());
        else if (!d.openPorts.isEmpty())
            body << QStringLiteral("tcp: %1 port(s)").arg(d.openPorts.size());
    } else if (!d.morfApps.isEmpty()) {
        body << QStringLiteral("morf: %1").arg(d.morfApps.size());
    }
    if (ghost)
        body.prepend(QStringLiteral("absent"));

    const qreal lineH = compact ? 17 : 18;
    const qreal h = 26 + body.size() * lineH + 8;
    const qreal padL = 14;
    const int inner = int(w - padL - 8);

    QPen border(ghost ? QColor(0xA8, 0x98, 0x80) : QColor(0xC4, 0xBC, 0xAA));
    if (ghost)
        border.setStyle(Qt::DashLine);
    auto* card = sc->addRect(0, 0, w, h, border, QBrush(ghost ? kGhostPaper : kPaper));
    card->setPos(x, y);
    card->setZValue(1);
    card->setData(0, d.id());   // pour le double-clic -> Inventaire
    card->setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);
    auto* bar = new QGraphicsRectItem(0, 0, 8, h, card);
    bar->setPen(Qt::NoPen);
    bar->setBrush(QBrush(ghost ? kGhostStripe : stripe));
    bar->setZValue(1);

    const QColor ink = ghost ? kGhostInk : kInk;
    const QColor muted = ghost ? kGhostInk : kMuted;
    addClippedLine(card, d.displayName(), padL, 5, titleFont(), ink, inner);
    for (int i = 0; i < body.size(); ++i)
        addClippedLine(card, body[i], padL, 24 + i * lineH, smallFont(), muted, inner);

    const QString tip = d.detailText();
    card->setToolTip(tip);
    bar->setToolTip(tip);
    return h;
}

qreal addHeaderCard(QGraphicsScene* sc, qreal x, qreal y, qreal w, const QString& title,
                    const QString& sub)
{
    const qreal h = sub.isEmpty() ? 40 : 54;
    const int inner = int(w - 20);
    auto* card = sc->addRect(0, 0, w, h, QPen(QColor(0xC4, 0xBC, 0xAA)), QBrush(kPaper));
    card->setPos(x, y);
    card->setZValue(1);
    card->setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);
    addClippedLine(card, title, 12, 6, titleFont(), kInk, inner);
    if (!sub.isEmpty())
        addClippedLine(card, sub, 12, 28, smallFont(), kMuted, inner);
    return h;
}

bool matchInfra(const Device& node, const Device& c)
{
    if (c.parentId.isEmpty() && c.parentName.isEmpty())
        return false;
    if (!c.parentId.isEmpty()
        && (c.parentId == node.id() || c.parentId == node.deviceId || c.parentId == node.ip
            || normalizedMac(c.parentId) == normalizedMac(node.mac)))
        return true;
    if (!c.parentName.isEmpty()
        && (c.parentName.compare(node.displayName(), Qt::CaseInsensitive) == 0
            || c.parentName.compare(node.hostname, Qt::CaseInsensitive) == 0
            || c.parentName == node.ip
            || (!node.deviceId.isEmpty()
                && c.parentName.compare(node.deviceId, Qt::CaseInsensitive) == 0)))
        return true;
    return false;
}

qreal stackClients(QGraphicsScene* sc, qreal x, qreal y, qreal w, const QVector<const Device*>& list,
                   bool ghost)
{
    for (const Device* c : list) {
        const qreal ch = addCard(sc, x, y, w, *c, kClientStripe, {}, true, ghost);
        y += ch + 6;
    }
    return y;
}
} // namespace

TopologyView::TopologyView(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setBackgroundBrush(kScene);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setStyleSheet(QStringLiteral("QGraphicsView { background: #E6E0D2; color: #1A1A1A; border: none; }"));
    QPalette pal = palette();
    pal.setColor(QPalette::Window, kScene);
    pal.setColor(QPalette::Base, kPaper);
    pal.setColor(QPalette::Text, kInk);
    pal.setColor(QPalette::WindowText, kInk);
    setPalette(pal);
}

void TopologyView::fitMap()
{
    if (!scene() || scene()->items().isEmpty())
        return;
    fitting_ = true;
    const QRectF r = scene()->sceneRect().adjusted(-16, -16, 16, 16);
    fitInView(r, Qt::KeepAspectRatio);
    // Ne jamais grossir au-dela de 1:1, ni descendre sous un plancher lisible :
    // une vue d'ensemble ecrasee a 0,2x est justement le probleme a eviter.
    if (transform().m11() > 1.0) {
        resetTransform();
    } else if (transform().m11() < 0.45) {
        resetTransform();
        scale(0.45, 0.45);
        horizontalScrollBar()->setValue(0);
        verticalScrollBar()->setValue(0);
    }
    fitting_ = false;
    userZoom_ = true;
}

void TopologyView::applyZoom(qreal factor)
{
    userZoom_ = true;
    const qreal next = transform().m11() * factor;
    if (next < 0.25 || next > 3.0)
        return;
    scale(factor, factor);
}

void TopologyView::zoomIn() { applyZoom(1.15); }
void TopologyView::zoomOut() { applyZoom(1.0 / 1.15); }

void TopologyView::zoomReset()
{
    userZoom_ = true;
    resetTransform();
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
}

void TopologyView::resetCamera()
{
    userZoom_ = false;
    laidOutOnce_ = false;
}

void TopologyView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    if (!fitting_ && (dx != 0 || dy != 0))
        userZoom_ = true;
}

void TopologyView::resizeEvent(QResizeEvent* event)
{
    // Ne plus reajuster automatiquement : cela reduisait tout a une taille
    // illisible des que le mesh etait un peu grand. La vue garde son echelle.
    QGraphicsView::resizeEvent(event);
}

void TopologyView::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
}

void TopologyView::mouseDoubleClickEvent(QMouseEvent* event)
{
    QGraphicsItem* it = itemAt(event->pos());
    while (it) {
        const QVariant v = it->data(0);
        if (v.isValid() && !v.toString().isEmpty()) {
            emit deviceActivated(v.toString());
            event->accept();
            return;
        }
        it = it->parentItem();
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void TopologyView::exportPng()
{
    if (!scene() || scene()->items().isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Exporter la carte"), QStringLiteral("lanatlas-carte.png"),
        QStringLiteral("Image PNG (*.png)"));
    if (path.isEmpty())
        return;
    const QRectF r = scene()->itemsBoundingRect().adjusted(-16, -16, 16, 16);
    QImage img(r.size().toSize(), QImage::Format_ARGB32);
    img.fill(QColor(0xE6, 0xE0, 0xD2));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    scene()->render(&p, QRectF(), r);
    p.end();
    img.save(path, "PNG");
}

void TopologyView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        userZoom_ = true;
        const qreal f = event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12;
        scale(f, f);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void TopologyView::setInventory(const QVector<Device>& connected, const QVector<Device>& historical)
{
    const QTransform cam = transform();
    const QPointF focus = mapToScene(viewport()->rect().center());
    const bool restore = laidOutOnce_;

    QGraphicsScene* sc = scene();
    sc->clear();
    sc->setBackgroundBrush(kScene);

    const Device* box = nullptr;
    QVector<const Device*> decos;
    QVector<const Device*> clients;
    for (const Device& d : connected) {
        if (d.category == QLatin1String("box"))
            box = &d;
        else if (d.category == QLatin1String("mesh_node"))
            decos.append(&d);
        else
            clients.append(&d);
    }

    std::sort(decos.begin(), decos.end(), [](const Device* a, const Device* b) {
        const int am = looksMainDeco(*a) ? 0 : 1;
        const int bm = looksMainDeco(*b) ? 0 : 1;
        if (am != bm)
            return am < bm;
        return a->displayName().compare(b->displayName(), Qt::CaseInsensitive) < 0;
    });

    QVector<QVector<const Device*>> decoClients(decos.size());
    QVector<const Device*> boxClients;
    QVector<const Device*> loose;
    for (const Device* c : clients) {
        int decoIdx = -1;
        for (int i = 0; i < decos.size(); ++i) {
            if (matchInfra(*decos[i], *c)) {
                decoIdx = i;
                break;
            }
        }
        if (decoIdx >= 0)
            decoClients[decoIdx].append(c);
        else if (box && matchInfra(*box, *c))
            boxClients.append(c);
        else
            loose.append(c);
    }

    QFont hint = smallFont();
    hint.setPointSize(10);
    hint.setWeight(QFont::DemiBold);

    const qreal colW = 228;
    const qreal gap = 16;
    const qreal x0 = 20;
    const int extra = (boxClients.isEmpty() ? 0 : 1) + (loose.isEmpty() ? 0 : 1);
    const int nMesh = qMax(1, decos.size());
    const int nCols = (decos.isEmpty() && box) ? (1 + extra) : (nMesh + extra);
    const qreal meshW = nMesh * colW + (nMesh - 1) * gap;
    qreal maxBottom = 80;

    addLabel(sc, QStringLiteral("Present maintenant  -  cartouches compacts, detail dans Inventaire"),
             x0, 6, hint, kMuted);

    qreal yMesh = 32;
    QPointF boxBottomCenter;

    if (box) {
        const qreal boxW = qMin(qMax(meshW, colW), colW * 2 + gap);
        const qreal boxX = x0 + qMax(0.0, (meshW - boxW) / 2.0);
        const QString boxBadge = QStringLiteral("passerelle  ·  %1 direct(s)")
                                     .arg(boxClients.size());
        const qreal bh = addCard(sc, boxX, 28, boxW, *box, kBoxStripe, boxBadge, false, false);
        boxBottomCenter = QPointF(boxX + boxW / 2, 28 + bh);
        yMesh = 28 + bh + 28;
    }

    if (decos.isEmpty()) {
        if (box) {
            maxBottom = stackClients(sc, x0, yMesh, colW, boxClients, false);
            if (!loose.isEmpty()) {
                const qreal x = x0 + colW + gap;
                const qreal hh = addHeaderCard(sc, x, yMesh - 8, colW, QStringLiteral("Non rattache"),
                                               QStringLiteral("%1 appareil(s)").arg(loose.size()));
                maxBottom = qMax(maxBottom, stackClients(sc, x, yMesh - 8 + hh + 8, colW, loose, false));
            }
        } else {
            addLabel(sc,
                     QStringLiteral("Aucun nœud Livebox/Deco. Mot de passe dans Parametres, puis Scanner."),
                     24, 40, hint, kInk);
        }
    } else {
        QVector<QPointF> decoTops;
        for (int i = 0; i < decos.size(); ++i) {
            const qreal x = x0 + i * (colW + gap);
            const bool main = looksMainDeco(*decos[i]);
            const QString badge = main
                ? QStringLiteral("Deco principal  ·  %1 client(s)").arg(decoClients[i].size())
                : QStringLiteral("repeteur  ·  %1 client(s)").arg(decoClients[i].size());
            const qreal h = addCard(sc, x, yMesh, colW, *decos[i],
                                    main ? kDecoMain : kDecoStripe, badge, false, false);
            decoTops.append(QPointF(x + colW / 2, yMesh));
            const qreal yClients = yMesh + h + 12;
            sc->addLine(x + colW / 2, yMesh + h, x + colW / 2, yClients, QPen(kLine, 1.2));
            const qreal colBottom = stackClients(sc, x, yClients, colW, decoClients[i], false);
            maxBottom = qMax(maxBottom, colBottom);
            // Couloir teinte derriere la colonne : on voit d'un coup d'œil quels
            // clients sont rattaches a ce Deco (z=0, sous les cartouches).
            const QColor laneBase = main ? kDecoMain : kDecoStripe;
            QColor laneFill(laneBase);
            laneFill.setAlpha(20);
            QColor laneEdge(laneBase);
            laneEdge.setAlpha(70);
            auto* lane = sc->addRect(x - 8, yMesh - 10, colW + 16,
                                     qMax(colBottom, yClients) - (yMesh - 10) + 4,
                                     QPen(laneEdge, 1.2), QBrush(laneFill));
            lane->setZValue(0);
        }

        if (box) {
            for (const QPointF& t : decoTops)
                sc->addLine(boxBottomCenter.x(), boxBottomCenter.y(), t.x(), t.y(), QPen(kLine, 2));
        } else if (decoTops.size() >= 2) {
            sc->addLine(decoTops.first().x(), yMesh - 12, decoTops.last().x(), yMesh - 12,
                        QPen(kLine, 2));
        }

        qreal extraX = x0 + nMesh * (colW + gap);
        if (!boxClients.isEmpty()) {
            const qreal hh = addHeaderCard(sc, extraX, yMesh, colW,
                                           QStringLiteral("Direct Livebox"),
                                           QStringLiteral("%1 filaire(s)").arg(boxClients.size()));
            maxBottom = qMax(maxBottom, stackClients(sc, extraX, yMesh + hh + 8, colW, boxClients, false));
            extraX += colW + gap;
        }
        if (!loose.isEmpty()) {
            const qreal hh = addHeaderCard(sc, extraX, yMesh, colW, QStringLiteral("Non rattache"),
                                           QStringLiteral("%1 appareil(s)").arg(loose.size()));
            maxBottom = qMax(maxBottom, stackClients(sc, extraX, yMesh + hh + 8, colW, loose, false));
        }
    }

    if (!historical.isEmpty()) {
        maxBottom += 28;
        addLabel(sc,
                 QStringLiteral("Historique / actuellement absents (%1)  -  trait pointille")
                     .arg(historical.size()),
                 x0, maxBottom, hint, kGhostInk);
        maxBottom += 22;
        QVector<const Device*> ghosts;
        for (const Device& d : historical) {
            if (d.category == QLatin1String("box") || d.category == QLatin1String("mesh_node"))
                continue;
            ghosts.append(&d);
        }
        std::sort(ghosts.begin(), ghosts.end(), [](const Device* a, const Device* b) {
            return a->lastSeen > b->lastSeen;
        });
        qreal x = x0;
        qreal y = maxBottom;
        qreal rowH = 0;
        int col = 0;
        const int histCols = qMax(nCols, 3);
        for (const Device* g : ghosts) {
            const qreal ch = addCard(sc, x, y, colW, *g, kGhostStripe, g->parentName, true, true);
            rowH = qMax(rowH, ch);
            ++col;
            if (col >= histCols) {
                col = 0;
                x = x0;
                y += rowH + 8;
                rowH = 0;
                maxBottom = y;
            } else {
                x += colW + gap;
            }
        }
        maxBottom = qMax(maxBottom, y + rowH);
    }

    const qreal totalW = x0 + qMax(nCols, 3) * (colW + gap) + 8;
    sc->setSceneRect(QRectF(0, 0, totalW, maxBottom + 28));
    if (restore) {
        setTransform(cam);
        centerOn(focus);
    } else {
        // Premier rendu : echelle 1:1 lisible, ancree en haut a gauche. On ne
        // fait plus de "fit" qui ecrasait tout ; l'utilisateur ajuste au besoin
        // via les boutons Ajuster / 100 % / +/-.
        resetTransform();
        horizontalScrollBar()->setValue(0);
        verticalScrollBar()->setValue(0);
        laidOutOnce_ = true;
    }
}
