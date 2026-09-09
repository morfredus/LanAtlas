#pragma once

#include "core/Device.h"
#include <QGraphicsView>
#include <QVector>

class TopologyView : public QGraphicsView {
    Q_OBJECT
public:
    explicit TopologyView(QWidget* parent = nullptr);
    void setInventory(const QVector<Device>& connected, const QVector<Device>& historical);
    void resetCamera();

public slots:
    void zoomIn();
    void zoomOut();
    void zoomReset();   // 100 %, ancre en haut a gauche
    void fitMap();      // vue d'ensemble (ajuster a la fenetre)
    void exportPng();   // enregistre la carte en image

signals:
    // Double-clic sur une carte d'appareil -> id de l'appareil (pour l'Inventaire).
    void deviceActivated(const QString& id);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void applyZoom(qreal factor);
    bool userZoom_ = false;
    bool laidOutOnce_ = false;
    bool fitting_ = false;
};
