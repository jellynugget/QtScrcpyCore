#ifndef INPUTCONVERT_H
#define INPUTCONVERT_H

#include "inputconvertbase.h"
#include <QPoint>
#include <QSize>
#include <QTimer>

class InputConvertNormal : public InputConvertBase
{
    Q_OBJECT
public:
    InputConvertNormal(Controller *controller);
    virtual ~InputConvertNormal();

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize);

private:
    AndroidMotioneventButtons convertMouseButtons(Qt::MouseButtons buttonState);
    AndroidMotioneventButtons convertMouseButton(Qt::MouseButton button);
    AndroidKeycode convertKeyCode(int key, Qt::KeyboardModifiers modifiers);
    AndroidMetastate convertMetastate(Qt::KeyboardModifiers modifiers);
    
    // Pinch-to-zoom support (Ctrl+click+drag)
    void simulateVirtualFinger(AndroidMotioneventAction action, const QPointF &point, const QSize &frameSize);
    QPointF inversePoint(const QPointF &point, const QPointF &center, bool invertX, bool invertY);
    QPointF clampToScreen(const QPointF &point, const QSize &frameSize);
    
    // Pinch gesture state for Shift+scroll
    void sendPinchGesture(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    void sendPinchTouchEvent(quint64 id, AndroidMotioneventAction action, QPoint pos, const QSize &frameSize, float pressure);
    void onPinchGestureStep();
    
    struct PinchState {
        QSize frameSize;
        QPoint startPoint1;
        QPoint startPoint2;
        QPoint endPoint1;
        QPoint endPoint2;
        quint64 touchId1;
        quint64 touchId2;
        int step;
        QTimer *timer;
    } m_pinchState;
    
    bool m_vfingerDown = false;
    bool m_vfingerInvertX = false;
    bool m_vfingerInvertY = false;
    QPointF m_pinchCenter; // Center point for pinch-to-zoom (initial pointer position)
};

#endif // INPUTCONVERT_H
