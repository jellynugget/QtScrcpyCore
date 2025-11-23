#include <cmath>
#include <QDebug>
#include <QTimer>

#include "inputconvertnormal.h"
#include "controller.h"

InputConvertNormal::InputConvertNormal(Controller *controller) 
    : InputConvertBase(controller)
    , m_pinchState{ QSize(), QPoint(), QPoint(), QPoint(), QPoint(), 0, 0, 0, nullptr }
{
    m_pinchState.timer = new QTimer(this);
    m_pinchState.timer->setSingleShot(true);
    connect(m_pinchState.timer, &QTimer::timeout, this, &InputConvertNormal::onPinchGestureStep);
    
    // Initialize pinch-to-zoom state
    m_vfingerDown = false;
    m_vfingerInvertX = false;
    m_vfingerInvertY = false;
    m_pinchCenter = QPointF(0, 0);
}

InputConvertNormal::~InputConvertNormal() 
{
    if (m_pinchState.timer && m_pinchState.timer->isActive()) {
        m_pinchState.timer->stop();
    }
}

void InputConvertNormal::onPinchGestureStep()
{
    const QSize &frameSize = m_pinchState.frameSize;
    
    switch (m_pinchState.step) {
    case 1: {
        // Send second DOWN
        sendPinchTouchEvent(m_pinchState.touchId2, AMOTION_EVENT_ACTION_DOWN, 
                           m_pinchState.startPoint2, frameSize, 1.0f);
        // Step 2: Send MOVE events after 20ms
        m_pinchState.step = 2;
        m_pinchState.timer->start(20);
        break;
    }
    case 2: {
        // Send both MOVE events (both touches moving together)
        sendPinchTouchEvent(m_pinchState.touchId1, AMOTION_EVENT_ACTION_MOVE, 
                           m_pinchState.endPoint1, frameSize, 1.0f);
        sendPinchTouchEvent(m_pinchState.touchId2, AMOTION_EVENT_ACTION_MOVE, 
                           m_pinchState.endPoint2, frameSize, 1.0f);
        // Step 3: Send UP events after 10ms
        m_pinchState.step = 3;
        m_pinchState.timer->start(10);
        break;
    }
    case 3: {
        // Send both UP events
        sendPinchTouchEvent(m_pinchState.touchId2, AMOTION_EVENT_ACTION_UP, 
                           m_pinchState.endPoint2, frameSize, 0.0f);
        sendPinchTouchEvent(m_pinchState.touchId1, AMOTION_EVENT_ACTION_UP, 
                           m_pinchState.endPoint1, frameSize, 0.0f);
        // Gesture complete
        m_pinchState.step = 0;
        break;
    }
    default:
        m_pinchState.step = 0;
        break;
    }
}

void InputConvertNormal::sendPinchTouchEvent(quint64 id, AndroidMotioneventAction action, 
                                             QPoint pos, const QSize &frameSize, float pressure)
{
    ControlMsg *msg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    if (msg) {
        msg->setInjectTouchMsgData(
            id,
            action,
            static_cast<AndroidMotioneventButtons>(0),
            static_cast<AndroidMotioneventButtons>(0),
            QRect(pos, frameSize),
            pressure);
        sendControlMsg(msg);
        qDebug() << "  -> Sent touch event: ID=" << id << " Action=" << action << " Pos=" << pos;
    }
}

void InputConvertNormal::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (!from) {
        return;
    }

    // action
    AndroidMotioneventAction action;
    switch (from->type()) {
    case QEvent::MouseButtonPress:
        action = AMOTION_EVENT_ACTION_DOWN;
        break;
    case QEvent::MouseButtonRelease:
        action = AMOTION_EVENT_ACTION_UP;
        break;
    case QEvent::MouseMove:
        // only support left button drag
        if (!(from->buttons() & Qt::LeftButton)) {
            return;
        }
        action = AMOTION_EVENT_ACTION_MOVE;
        break;
    default:
        return;
    }

    // pos
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPointF pos = from->localPos();
#else
    QPointF pos = from->position();
#endif
    // convert pos
    pos.setX(pos.x() * frameSize.width() / showSize.width());
    pos.setY(pos.y() * frameSize.height() / showSize.height());

    // Check for pinch-to-zoom mode (Ctrl+click)
    bool ctrl_pressed = from->modifiers() & Qt::ControlModifier;
    bool shift_pressed = from->modifiers() & Qt::ShiftModifier;
    bool isLeftButton = (from->type() == QEvent::MouseButtonPress && from->button() == Qt::LeftButton) ||
                        (from->type() == QEvent::MouseButtonRelease && from->button() == Qt::LeftButton) ||
                        (from->type() == QEvent::MouseMove && (from->buttons() & Qt::LeftButton));
    bool down = (action == AMOTION_EVENT_ACTION_DOWN);
    bool up = (action == AMOTION_EVENT_ACTION_UP);
    
    // Determine if we should change virtual finger state
    bool change_vfinger = isLeftButton && 
                         ((down && !m_vfingerDown && (ctrl_pressed || shift_pressed)) ||
                          (up && m_vfingerDown));

    // set data for the main touch event
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    if (!controlMsg) {
        return;
    }
    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(POINTER_ID_GENERIC_FINGER),
        action,
        convertMouseButton(from->button()),
        convertMouseButtons(from->buttons()),
        QRect(pos.toPoint(), frameSize),
        AMOTION_EVENT_ACTION_DOWN == action ? 1.0f : 0.0f);
    sendControlMsg(controlMsg);

    // Handle pinch-to-zoom virtual finger
    if (change_vfinger) {
        if (down) {
            // Ctrl  Shift     invert_x  invert_y
            // ----  ----- ==> --------  --------
            //   0     0           0         0      -
            //   0     1           1         0      vertical tilt
            //   1     0           1         1      rotate (pinch-to-zoom)
            //   1     1           0         1      horizontal tilt
            // Always use vertical placement for predictable experience
            // Set inversion flags for vertical placement
            m_vfingerInvertX = false;
            m_vfingerInvertY = true;
            
            // Calculate distance percentages to try (70%, 50%, 40%)
            qreal baseDistance = qMin(frameSize.width(), frameSize.height());
            qreal distancePercentages[] = {0.7, 0.5, 0.4};
            
            // Calculate available space vertically
            qreal distanceToTop = pos.y();
            qreal distanceToBottom = frameSize.height() - pos.y();
            bool placeAbove = (distanceToTop >= distanceToBottom);
            qreal availableSpace = placeAbove ? distanceToTop : distanceToBottom;
            
            QPointF vfinger;
            qreal finalDistance = 0;
            bool placementFound = false;
            
            // Try different distance percentages until we find one that fits
            for (int i = 0; i < 3 && !placementFound; i++) {
                qreal testDistance = baseDistance * distancePercentages[i];
                
                if (testDistance <= availableSpace) {
                    // Enough space, place virtual finger
                    if (placeAbove) {
                        vfinger = QPointF(pos.x(), pos.y() - testDistance);
                        m_pinchCenter = QPointF(pos.x(), pos.y() - testDistance / 2.0);
                    } else {
                        vfinger = QPointF(pos.x(), pos.y() + testDistance);
                        m_pinchCenter = QPointF(pos.x(), pos.y() + testDistance / 2.0);
                    }
                    finalDistance = testDistance;
                    placementFound = true;
                }
            }
            
            // If still no placement found (very edge case), use 80% of available space
            if (!placementFound) {
                finalDistance = qMax(availableSpace * 0.8, baseDistance * 0.2); // Use 80% of available or 20% minimum
                if (placeAbove) {
                    vfinger = QPointF(pos.x(), pos.y() - finalDistance);
                    m_pinchCenter = QPointF(pos.x(), pos.y() - finalDistance / 2.0);
                } else {
                    vfinger = QPointF(pos.x(), pos.y() + finalDistance);
                    m_pinchCenter = QPointF(pos.x(), pos.y() + finalDistance / 2.0);
                }
            }
            
            // Clamp virtual finger to screen bounds (safety check)
            vfinger = clampToScreen(vfinger, frameSize);
            
            // If clamped, adjust center to midpoint (but keep mouse position fixed)
            if (vfinger.x() != pos.x() || vfinger.y() != pos.y()) {
                // Recalculate center, but ensure mouse position stays fixed
                m_pinchCenter = QPointF((pos.x() + vfinger.x()) / 2.0, (pos.y() + vfinger.y()) / 2.0);
            }
            
            AndroidMotioneventAction vfingerAction = AMOTION_EVENT_ACTION_DOWN;
            simulateVirtualFinger(vfingerAction, vfinger, frameSize);
            m_vfingerDown = down;
        } else {
            // Release virtual finger
            QPointF vfinger;
            if (m_vfingerInvertY && !m_vfingerInvertX) {
                // Vertical placement: maintain same X, invert Y through center
                vfinger = QPointF(pos.x(), 2.0 * m_pinchCenter.y() - pos.y());
            } else if (m_vfingerInvertX && !m_vfingerInvertY) {
                // Horizontal placement: maintain same Y, invert X through center
                vfinger = QPointF(2.0 * m_pinchCenter.x() - pos.x(), pos.y());
            } else {
                // Both axes: use inverse point
                vfinger = inversePoint(pos, m_pinchCenter, m_vfingerInvertX, m_vfingerInvertY);
            }
            vfinger = clampToScreen(vfinger, frameSize);
            AndroidMotioneventAction vfingerAction = AMOTION_EVENT_ACTION_UP;
            simulateVirtualFinger(vfingerAction, vfinger, frameSize);
            m_vfingerDown = down;
        }
    } else if (m_vfingerDown && action == AMOTION_EVENT_ACTION_MOVE) {
        // Update virtual finger position on move
        // Use the same placement logic: maintain distance and direction from initial setup
        QPointF vfinger;
        if (m_vfingerInvertY && !m_vfingerInvertX) {
            // Vertical placement: maintain same X, invert Y through center
            vfinger = QPointF(pos.x(), 2.0 * m_pinchCenter.y() - pos.y());
        } else if (m_vfingerInvertX && !m_vfingerInvertY) {
            // Horizontal placement: maintain same Y, invert X through center
            vfinger = QPointF(2.0 * m_pinchCenter.x() - pos.x(), pos.y());
        } else {
            // Both axes: use inverse point
            vfinger = inversePoint(pos, m_pinchCenter, m_vfingerInvertX, m_vfingerInvertY);
        }
        // Clamp virtual finger to screen bounds to ensure it stays on screen
        vfinger = clampToScreen(vfinger, frameSize);
        simulateVirtualFinger(AMOTION_EVENT_ACTION_MOVE, vfinger, frameSize);
    }
}

void InputConvertNormal::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    qDebug() << "InputConvertNormal::wheelEvent called";
    
    if (!from) {
        qDebug() << "  -> Event is null, returning";
        return;
    }

    qDebug() << "  -> Modifiers:" << from->modifiers() << "ShiftModifier:" << Qt::ShiftModifier;
    qDebug() << "  -> Has Shift:" << (from->modifiers() & Qt::ShiftModifier);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    qDebug() << "  -> AngleDelta:" << from->angleDelta();
#else
    qDebug() << "  -> Delta:" << from->delta();
#endif

    // Check if Shift is pressed for pinch-to-zoom gesture
    // Allow pinch gesture even if delta is null (some systems may send null delta with Shift)
    if (from->modifiers() & Qt::ShiftModifier) {
        qDebug() << "  -> Shift detected, calling sendPinchGesture";
        // For pinch gesture, we'll use a default delta if angleDelta is null
        sendPinchGesture(from, frameSize, showSize);
        return;
    }

    // For normal scroll, check if delta is null
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (from->angleDelta().isNull()) {
        qDebug() << "  -> AngleDelta is null, returning";
        return;
    }
#else
    if (from->delta() == 0) {
        qDebug() << "  -> Delta is zero, returning";
        return;
    }
#endif
    
    qDebug() << "  -> No Shift, processing normal scroll";

    // delta
    float hScroll = from->angleDelta().x() / 64.0f;
    float vScroll = from->angleDelta().y() / 64.0f;

    // pos
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    QPointF pos = from->position();
#else
    QPointF pos = from->posF();
#endif
    // convert pos
    pos.setX(pos.x() * frameSize.width() / showSize.width());
    pos.setY(pos.y() * frameSize.height() / showSize.height());

    // set data
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_SCROLL);
    if (!controlMsg) {
        return;
    }
    controlMsg->setInjectScrollMsgData(QRect(pos.toPoint(), frameSize), hScroll, vScroll, convertMouseButtons(from->buttons()));
    sendControlMsg(controlMsg);
}

void InputConvertNormal::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    Q_UNUSED(frameSize)
    Q_UNUSED(showSize)
    if (!from) {
        return;
    }

    bool repeat = from->isAutoRepeat();

    // action
    AndroidKeyeventAction action;
    switch (from->type()) {
    case QEvent::KeyPress:
        action = AKEY_EVENT_ACTION_DOWN;
        break;
    case QEvent::KeyRelease:
        action = AKEY_EVENT_ACTION_UP;
        break;
    default:
        return;
    }

    // key code
    AndroidKeycode keyCode = convertKeyCode(from->key(), from->modifiers());
    if (AKEYCODE_UNKNOWN == keyCode) {
        return;
    }

    // set data
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlMsg) {
        return;
    }

    if (repeat) {
        m_repeat++;
    } else {
        m_repeat = 0;
    }

    controlMsg->setInjectKeycodeMsgData(action, keyCode, m_repeat, convertMetastate(from->modifiers()));
    sendControlMsg(controlMsg);
}

void InputConvertNormal::sendPinchGesture(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    qDebug() << "sendPinchGesture called - Shift+scroll detected";
    
    // Get mouse position and convert to frame coordinates
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    QPointF pos = from->position();
#else
    QPointF pos = from->posF();
#endif
    pos.setX(pos.x() * frameSize.width() / showSize.width());
    pos.setY(pos.y() * frameSize.height() / showSize.height());
    
    qDebug() << "Mouse pos (frame coords):" << pos << "Frame size:" << frameSize;

    // Calculate scroll delta to determine zoom direction
    float scrollDelta = 0.0f;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (!from->angleDelta().isNull()) {
        scrollDelta = from->angleDelta().y() / 120.0f; // Normalize scroll delta
    } else if (!from->pixelDelta().isNull()) {
        // Fallback to pixelDelta if angleDelta is null
        scrollDelta = from->pixelDelta().y() / 10.0f; // Approximate conversion
    } else {
        // If both are null, use a default small zoom
        scrollDelta = 1.0f; // Default zoom in
        qDebug() << "  -> Both angleDelta and pixelDelta are null, using default zoom";
    }
#else
    if (from->delta() != 0) {
        scrollDelta = from->delta() / 120.0f;
    } else {
        // If delta is zero, use a default small zoom
        scrollDelta = 1.0f; // Default zoom in
        qDebug() << "  -> Delta is zero, using default zoom";
    }
#endif
    
    qDebug() << "  -> Calculated scrollDelta:" << scrollDelta;

    // scrcpy's approach: virtual finger positioned symmetrically opposite through screen center
    // Screen center in frame coordinates
    QPointF screenCenter(frameSize.width() / 2.0f, frameSize.height() / 2.0f);
    
    // Calculate virtual finger position (symmetric through center)
    // Vx = Cx - (Mx - Cx) = 2*Cx - Mx
    // Vy = Cy - (My - Cy) = 2*Cy - My
    QPointF virtualPos = 2.0f * screenCenter - pos;
    
    // For pinch gesture: zoom in = fingers move closer, zoom out = fingers move farther
    // Calculate initial distance between the two points
    QPointF initialVector = virtualPos - pos;
    float initialDistance = std::sqrt(initialVector.x() * initialVector.x() + initialVector.y() * initialVector.y());
    
    // Distance change based on scroll (positive = zoom in = closer, negative = zoom out = farther)
    // Make the gesture much more pronounced - Android needs a significant distance change
    // Use at least 50% change to ensure Android recognizes it as a pinch gesture
    float minChange = initialDistance * 0.5f;
    float maxChange = initialDistance * 0.8f;
    
    // Calculate desired change based on scroll delta
    float desiredChange = scrollDelta * initialDistance * 0.4f;
    
    // Ensure minimum gesture size - always use at least 50% of initial distance
    if (qAbs(desiredChange) < minChange) {
        desiredChange = desiredChange >= 0 ? minChange : -minChange;
    }
    
    // Cap at maximum
    float distanceChange = qBound(-maxChange, desiredChange, maxChange);
    float finalDistance = initialDistance - distanceChange;
    
    qDebug() << "  -> Initial distance:" << initialDistance << "Change:" << distanceChange << "Final distance:" << finalDistance;
    qDebug() << "  -> Distance change percentage:" << (distanceChange / initialDistance * 100.0f) << "%";
    
    // Calculate unit vector from pos to virtualPos
    QPointF unitVector = initialDistance > 0.01f ? initialVector / initialDistance : QPointF(1, 0);
    
    // Start positions (current mouse and virtual positions)
    QPointF startPoint1 = pos;
    QPointF startPoint2 = virtualPos;
    
    // End positions: maintain center point, adjust distance
    QPointF centerPoint = (pos + virtualPos) / 2.0f;
    QPointF endPoint1 = centerPoint - unitVector * (finalDistance / 2.0f);
    QPointF endPoint2 = centerPoint + unitVector * (finalDistance / 2.0f);
    
    // Clamp points to frame bounds
    startPoint1.setX(qBound(0.0, startPoint1.x(), static_cast<qreal>(frameSize.width())));
    startPoint1.setY(qBound(0.0, startPoint1.y(), static_cast<qreal>(frameSize.height())));
    startPoint2.setX(qBound(0.0, startPoint2.x(), static_cast<qreal>(frameSize.width())));
    startPoint2.setY(qBound(0.0, startPoint2.y(), static_cast<qreal>(frameSize.height())));
    endPoint1.setX(qBound(0.0, endPoint1.x(), static_cast<qreal>(frameSize.width())));
    endPoint1.setY(qBound(0.0, endPoint1.y(), static_cast<qreal>(frameSize.height())));
    endPoint2.setX(qBound(0.0, endPoint2.x(), static_cast<qreal>(frameSize.width())));
    endPoint2.setY(qBound(0.0, endPoint2.y(), static_cast<qreal>(frameSize.height())));

    // Use numeric touch IDs (0 and 1) as per controlmsg.h comment: "id 代表一个触摸点，最多支持10个触摸点[0,9]"
    const quint64 touchId1 = 0;
    const quint64 touchId2 = 1;

    qDebug() << "Pinch gesture - Point1:" << startPoint1 << "Point2:" << startPoint2;
    qDebug() << "End positions - Point1:" << endPoint1 << "Point2:" << endPoint2;
    qDebug() << "Touch IDs:" << touchId1 << touchId2;

    // Stop any existing pinch gesture
    if (m_pinchState.timer && m_pinchState.timer->isActive()) {
        m_pinchState.timer->stop();
    }

    // Store gesture state for timer-based sending
    m_pinchState.frameSize = frameSize;
    m_pinchState.startPoint1 = startPoint1.toPoint();
    m_pinchState.startPoint2 = startPoint2.toPoint();
    m_pinchState.endPoint1 = endPoint1.toPoint();
    m_pinchState.endPoint2 = endPoint2.toPoint();
    m_pinchState.touchId1 = touchId1;
    m_pinchState.touchId2 = touchId2;
    m_pinchState.step = 0;

    // Step 0: Send first DOWN immediately
    sendPinchTouchEvent(touchId1, AMOTION_EVENT_ACTION_DOWN, m_pinchState.startPoint1, frameSize, 1.0f);
    
    // Step 1: Send second DOWN after 10ms (so Android sees both as simultaneous)
    m_pinchState.step = 1;
    m_pinchState.timer->start(10);
}

AndroidMotioneventButtons InputConvertNormal::convertMouseButtons(Qt::MouseButtons buttonState)
{
    quint32 buttons = 0;
    if (buttonState & Qt::LeftButton) {
        buttons |= AMOTION_EVENT_BUTTON_PRIMARY;
    }
    if (buttonState & Qt::RightButton) {
        buttons |= AMOTION_EVENT_BUTTON_SECONDARY;
    }
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    if (buttonState & Qt::MiddleButton) {
#else
    if (buttonState & Qt::MidButton) {
#endif    
        buttons |= AMOTION_EVENT_BUTTON_TERTIARY;
    }
    if (buttonState & Qt::XButton1) {
        buttons |= AMOTION_EVENT_BUTTON_BACK;
    }
    if (buttonState & Qt::XButton2) {
        buttons |= AMOTION_EVENT_BUTTON_FORWARD;
    }
    return static_cast<AndroidMotioneventButtons>(buttons);
}

AndroidMotioneventButtons InputConvertNormal::convertMouseButton(Qt::MouseButton button)
{
    if (button == Qt::LeftButton) {
        return AMOTION_EVENT_BUTTON_PRIMARY;
    }
    if (button == Qt::RightButton) {
        return AMOTION_EVENT_BUTTON_SECONDARY;
    }
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    if (button == Qt::MiddleButton) {
#else
    if (button == Qt::MidButton) {
#endif
        return AMOTION_EVENT_BUTTON_TERTIARY;
    }
    if (button == Qt::XButton1) {
        return AMOTION_EVENT_BUTTON_BACK;
    }
    if (button == Qt::XButton2) {
        return AMOTION_EVENT_BUTTON_FORWARD;
    }

    return static_cast<AndroidMotioneventButtons>(0);
}

AndroidKeycode InputConvertNormal::convertKeyCode(int key, Qt::KeyboardModifiers modifiers)
{
    AndroidKeycode keyCode = AKEYCODE_UNKNOWN;
    // functional keys
    switch (key) {
    case Qt::Key_Return:
        keyCode = AKEYCODE_ENTER;
        break;
    case Qt::Key_Enter:
        keyCode = AKEYCODE_NUMPAD_ENTER;
        break;
    case Qt::Key_Escape:
        keyCode = AKEYCODE_ESCAPE;
        break;
    case Qt::Key_Backspace:
        keyCode = AKEYCODE_DEL;
        break;
    case Qt::Key_Delete:
        keyCode = AKEYCODE_FORWARD_DEL;
        break;
    case Qt::Key_Tab:
        keyCode = AKEYCODE_TAB;
        break;
    case Qt::Key_Home:
        keyCode = AKEYCODE_MOVE_HOME;
        break;
    case Qt::Key_End:
        keyCode = AKEYCODE_MOVE_END;
        break;
    case Qt::Key_PageUp:
        keyCode = AKEYCODE_PAGE_UP;
        break;
    case Qt::Key_PageDown:
        keyCode = AKEYCODE_PAGE_DOWN;
        break;
    case Qt::Key_Left:
        keyCode = AKEYCODE_DPAD_LEFT;
        break;
    case Qt::Key_Right:
        keyCode = AKEYCODE_DPAD_RIGHT;
        break;
    case Qt::Key_Up:
        keyCode = AKEYCODE_DPAD_UP;
        break;
    case Qt::Key_Down:
        keyCode = AKEYCODE_DPAD_DOWN;
        break;
    }
    if (AKEYCODE_UNKNOWN != keyCode) {
        return keyCode;
    }

    // if ALT and META are pressed, dont handle letters and space
    if (modifiers & (Qt::AltModifier | Qt::MetaModifier)) {
        return keyCode;
    }

    // character keys
    switch (key) {
    case Qt::Key_A:
        keyCode = AKEYCODE_A;
        break;
    case Qt::Key_B:
        keyCode = AKEYCODE_B;
        break;
    case Qt::Key_C:
        keyCode = AKEYCODE_C;
        break;
    case Qt::Key_D:
        keyCode = AKEYCODE_D;
        break;
    case Qt::Key_E:
        keyCode = AKEYCODE_E;
        break;
    case Qt::Key_F:
        keyCode = AKEYCODE_F;
        break;
    case Qt::Key_G:
        keyCode = AKEYCODE_G;
        break;
    case Qt::Key_H:
        keyCode = AKEYCODE_H;
        break;
    case Qt::Key_I:
        keyCode = AKEYCODE_I;
        break;
    case Qt::Key_J:
        keyCode = AKEYCODE_J;
        break;
    case Qt::Key_K:
        keyCode = AKEYCODE_K;
        break;
    case Qt::Key_L:
        keyCode = AKEYCODE_L;
        break;
    case Qt::Key_M:
        keyCode = AKEYCODE_M;
        break;
    case Qt::Key_N:
        keyCode = AKEYCODE_N;
        break;
    case Qt::Key_O:
        keyCode = AKEYCODE_O;
        break;
    case Qt::Key_P:
        keyCode = AKEYCODE_P;
        break;
    case Qt::Key_Q:
        keyCode = AKEYCODE_Q;
        break;
    case Qt::Key_R:
        keyCode = AKEYCODE_R;
        break;
    case Qt::Key_S:
        keyCode = AKEYCODE_S;
        break;
    case Qt::Key_T:
        keyCode = AKEYCODE_T;
        break;
    case Qt::Key_U:
        keyCode = AKEYCODE_U;
        break;
    case Qt::Key_V:
        keyCode = AKEYCODE_V;
        break;
    case Qt::Key_W:
        keyCode = AKEYCODE_W;
        break;
    case Qt::Key_X:
        keyCode = AKEYCODE_X;
        break;
    case Qt::Key_Y:
        keyCode = AKEYCODE_Y;
        break;
    case Qt::Key_Z:
        keyCode = AKEYCODE_Z;
        break;
    case Qt::Key_0:
        keyCode = AKEYCODE_0;
        break;
    case Qt::Key_1:
    case Qt::Key_Exclam: // !
        keyCode = AKEYCODE_1;
        break;
    case Qt::Key_2:
        keyCode = AKEYCODE_2;
        break;
    case Qt::Key_3:
        keyCode = AKEYCODE_3;
        break;
    case Qt::Key_4:
    case Qt::Key_Dollar: //$
        keyCode = AKEYCODE_4;
        break;
    case Qt::Key_5:
    case Qt::Key_Percent: // %
        keyCode = AKEYCODE_5;
        break;
    case Qt::Key_6:
    case Qt::Key_AsciiCircum: //^
        keyCode = AKEYCODE_6;
        break;
    case Qt::Key_7:
    case Qt::Key_Ampersand: //&
        keyCode = AKEYCODE_7;
        break;
    case Qt::Key_8:
        keyCode = AKEYCODE_8;
        break;
    case Qt::Key_9:
        keyCode = AKEYCODE_9;
        break;
    case Qt::Key_Space:
        keyCode = AKEYCODE_SPACE;
        break;
    case Qt::Key_Comma: //,
    case Qt::Key_Less:  //<
        keyCode = AKEYCODE_COMMA;
        break;
    case Qt::Key_Period:  //.
    case Qt::Key_Greater: //>
        keyCode = AKEYCODE_PERIOD;
        break;
    case Qt::Key_Minus:      //-
    case Qt::Key_Underscore: //_
        keyCode = AKEYCODE_MINUS;
        break;
    case Qt::Key_Equal: //=
        keyCode = AKEYCODE_EQUALS;
        break;
    case Qt::Key_BracketLeft: //[
    case Qt::Key_BraceLeft:   //{
        keyCode = AKEYCODE_LEFT_BRACKET;
        break;
    case Qt::Key_BracketRight: //]
    case Qt::Key_BraceRight:   //}
        keyCode = AKEYCODE_RIGHT_BRACKET;
        break;
    case Qt::Key_Backslash: // \ ????
    case Qt::Key_Bar:       //|
        keyCode = AKEYCODE_BACKSLASH;
        break;
    case Qt::Key_Semicolon: //;
    case Qt::Key_Colon:     //:
        keyCode = AKEYCODE_SEMICOLON;
        break;
    case Qt::Key_Apostrophe: //'
    case Qt::Key_QuoteDbl:   //"
        keyCode = AKEYCODE_APOSTROPHE;
        break;
    case Qt::Key_Slash:    // /
    case Qt::Key_Question: //?
        keyCode = AKEYCODE_SLASH;
        break;
    case Qt::Key_At: //@
        keyCode = AKEYCODE_AT;
        break;
    case Qt::Key_Plus: //+
        keyCode = AKEYCODE_PLUS;
        break;
    case Qt::Key_QuoteLeft:  //`
    case Qt::Key_AsciiTilde: //~
        keyCode = AKEYCODE_GRAVE;
        break;
    case Qt::Key_NumberSign: //#
        keyCode = AKEYCODE_POUND;
        break;
    case Qt::Key_ParenLeft: //(
        keyCode = AKEYCODE_NUMPAD_LEFT_PAREN;
        break;
    case Qt::Key_ParenRight: //)
        keyCode = AKEYCODE_NUMPAD_RIGHT_PAREN;
        break;
    case Qt::Key_Asterisk: //*
        keyCode = AKEYCODE_STAR;
        break;
    }
    return keyCode;
}

AndroidMetastate InputConvertNormal::convertMetastate(Qt::KeyboardModifiers modifiers)
{
    int metastate = AMETA_NONE;

    if (modifiers & Qt::ShiftModifier) {
        metastate |= AMETA_SHIFT_ON;
    }
    if (modifiers & Qt::ControlModifier) {
        metastate |= AMETA_CTRL_ON;
    }
    if (modifiers & Qt::AltModifier) {
        metastate |= AMETA_ALT_ON;
    }
    if (modifiers & Qt::MetaModifier) {
        metastate |= AMETA_META_ON;
    }
    /*
    if (mod & KMOD_NUM) {
        metastate |= AMETA_NUM_LOCK_ON;
    }
    if (mod & KMOD_CAPS) {
        metastate |= AMETA_CAPS_LOCK_ON;
    }
    if (mod & KMOD_MODE) { // Alt Gr
        // no mapping?
    }
    */
    return static_cast<AndroidMetastate>(metastate);
}

void InputConvertNormal::simulateVirtualFinger(AndroidMotioneventAction action, const QPointF &point, const QSize &frameSize)
{
    bool up = (action == AMOTION_EVENT_ACTION_UP);
    
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    if (!controlMsg) {
        return;
    }
    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(POINTER_ID_VIRTUAL_FINGER),
        action,
        static_cast<AndroidMotioneventButtons>(0),
        static_cast<AndroidMotioneventButtons>(0),
        QRect(point.toPoint(), frameSize),
        up ? 0.0f : 1.0f);
    sendControlMsg(controlMsg);
}

QPointF InputConvertNormal::inversePoint(const QPointF &point, const QPointF &center, bool invertX, bool invertY)
{
    // Invert point through the center: P' = 2*C - P
    QPointF result = point;
    if (invertX) {
        result.setX(2.0 * center.x() - point.x());
    }
    if (invertY) {
        result.setY(2.0 * center.y() - point.y());
    }
    return result;
}

QPointF InputConvertNormal::clampToScreen(const QPointF &point, const QSize &frameSize)
{
    // Clamp the point to stay within screen bounds
    QPointF clamped = point;
    clamped.setX(qBound(0.0, point.x(), static_cast<qreal>(frameSize.width() - 1)));
    clamped.setY(qBound(0.0, point.y(), static_cast<qreal>(frameSize.height() - 1)));
    return clamped;
}
