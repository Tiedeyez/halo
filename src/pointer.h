#pragma once

#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>

// Watches the window's pointer without taking anything from it: every press
// and move anywhere in the window is reported (in window coordinates) even
// when a button or list under it handles the event. The water background
// ripples from these.
class PointerWatch : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

signals:
    void pressed(double x, double y);
    void moved(double x, double y);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            const QPointF p = static_cast<QMouseEvent *>(event)->scenePosition();
            emit pressed(p.x(), p.y());
            break;
        }
        case QEvent::MouseMove: {
            const QPointF p = static_cast<QMouseEvent *>(event)->scenePosition();
            emit moved(p.x(), p.y());
            break;
        }
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }
};
