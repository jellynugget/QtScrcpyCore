#ifndef INPUTCONVERT_H
#define INPUTCONVERT_H

#include "inputconvertbase.h"

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
    
    // Pinch-to-zoom support
    void simulateVirtualFinger(AndroidMotioneventAction action, const QPointF &point, const QSize &frameSize);
    QPointF inversePoint(const QPointF &point, const QSize &frameSize, bool invertX, bool invertY);
    
    bool m_vfingerDown = false;
    bool m_vfingerInvertX = false;
    bool m_vfingerInvertY = false;
};

#endif // INPUTCONVERT_H
