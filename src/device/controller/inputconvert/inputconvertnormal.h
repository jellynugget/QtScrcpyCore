#ifndef INPUTCONVERT_H
#define INPUTCONVERT_H

#include "inputconvertbase.h"

class QTimer;

class InputConvertNormal : public InputConvertBase
{
    Q_OBJECT
public:
    InputConvertNormal(Controller *controller);
    virtual ~InputConvertNormal();

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize);

private slots:
    void onPinchGestureStep();

private:
    void sendPinchGesture(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    void sendPinchTouchEvent(quint64 id, AndroidMotioneventAction action, QPoint pos, const QSize &frameSize, float pressure);
    AndroidMotioneventButtons convertMouseButtons(Qt::MouseButtons buttonState);
    AndroidMotioneventButtons convertMouseButton(Qt::MouseButton button);
    AndroidKeycode convertKeyCode(int key, Qt::KeyboardModifiers modifiers);
    AndroidMetastate convertMetastate(Qt::KeyboardModifiers modifiers);

private:
    struct PinchGestureState {
        QSize frameSize;
        QPoint startPoint1, startPoint2;
        QPoint endPoint1, endPoint2;
        quint64 touchId1, touchId2;
        int step;
        QTimer* timer;
    } m_pinchState;
};

#endif // INPUTCONVERT_H
