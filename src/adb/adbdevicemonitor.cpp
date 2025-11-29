#include <QDebug>
#include <QHostAddress>
#include <QRegularExpression>
#include <QtGlobal>

#include "adbdevicemonitor.h"

namespace qsc {

const QByteArray AdbDeviceMonitor::TRACK_DEVICES_REQUEST = QByteArray("0012host:track-devices", 22);

AdbDeviceMonitor::AdbDeviceMonitor(QObject *parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_reconnectTimer(nullptr)
    , m_monitoring(false)
    , m_okayConsumed(false)
    , m_reconnectDelay(RECONNECT_DELAY_MS)
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &AdbDeviceMonitor::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &AdbDeviceMonitor::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &AdbDeviceMonitor::onSocketReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_socket, &QTcpSocket::errorOccurred, this, &AdbDeviceMonitor::onSocketError);
#else
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &AdbDeviceMonitor::onSocketError);
#endif

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &AdbDeviceMonitor::reconnect);
}

AdbDeviceMonitor::~AdbDeviceMonitor()
{
    stop();
}

void AdbDeviceMonitor::start()
{
    if (m_monitoring) {
        return;
    }

    m_monitoring = true;
    m_connectedDevices.clear();
    m_buffer.clear();
    m_reconnectDelay = RECONNECT_DELAY_MS;
    reconnect();
}

void AdbDeviceMonitor::stop()
{
    if (!m_monitoring) {
        return;
    }

    m_monitoring = false;
    m_reconnectTimer->stop();
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }
    m_buffer.clear();
    m_connectedDevices.clear();
}

void AdbDeviceMonitor::reconnect()
{
    if (!m_monitoring) {
        return;
    }

    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }

    m_socket->connectToHost(QHostAddress::LocalHost, ADB_PORT);
}

void AdbDeviceMonitor::onSocketConnected()
{
    qDebug() << "AdbDeviceMonitor: Connected to ADB daemon";
    m_reconnectDelay = RECONNECT_DELAY_MS;
    m_buffer.clear();
    m_okayConsumed = false;
    sendTrackDevicesRequest();
}

void AdbDeviceMonitor::onSocketDisconnected()
{
    qDebug() << "AdbDeviceMonitor: Disconnected from ADB daemon";
    m_buffer.clear();
    if (m_monitoring) {
        startReconnectTimer();
    }
}

void AdbDeviceMonitor::onSocketReadyRead()
{
    m_buffer.append(m_socket->readAll());

    // First, consume OKAY if we haven't yet
    if (m_buffer.size() >= 4 && m_buffer.left(4) == "OKAY") {
        m_buffer.remove(0, 4);
    }

    // Process packets
    while (true) {
        if (m_buffer.size() < 4) {
            break;
        }

        // Parse packet length (4 hex digits)
        bool ok;
        int packetLength = m_buffer.left(4).toInt(&ok, 16);
        if (!ok || packetLength <= 0) {
            qWarning() << "AdbDeviceMonitor: Invalid packet length";
            m_buffer.clear();
            break;
        }

        int totalLength = 4 + packetLength;
        if (m_buffer.size() < totalLength) {
            // Not enough data yet, wait for more
            break;
        }

        // Extract packet content
        QByteArray packetData = m_buffer.mid(4, packetLength);
        QString packet = QString::fromUtf8(packetData);
        m_buffer.remove(0, totalLength);

        handlePacket(packet);
    }
}

void AdbDeviceMonitor::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    qWarning() << "AdbDeviceMonitor: Socket error:" << m_socket->errorString();
    if (m_monitoring) {
        startReconnectTimer();
    }
}

void AdbDeviceMonitor::sendTrackDevicesRequest()
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    qint64 written = m_socket->write(TRACK_DEVICES_REQUEST);
    if (written != TRACK_DEVICES_REQUEST.size()) {
        qWarning() << "AdbDeviceMonitor: Failed to write track-devices request";
    }
}

void AdbDeviceMonitor::startReconnectTimer()
{
    m_reconnectTimer->start(m_reconnectDelay);
    // Exponential backoff, but cap at MAX_RECONNECT_DELAY_MS
    m_reconnectDelay = qMin(m_reconnectDelay * 2, MAX_RECONNECT_DELAY_MS);
}

void AdbDeviceMonitor::handlePacket(const QString &packet)
{
    QStringList currentDevices = parseConnectedDevices(packet);
    
    // Check for new devices
    for (const QString &serial : currentDevices) {
        if (!m_connectedDevices.contains(serial)) {
            qDebug() << "AdbDeviceMonitor: New device detected:" << serial;
            emit newDeviceConnected(serial);
        }
    }
    
    m_connectedDevices = currentDevices;
}

QStringList AdbDeviceMonitor::parseConnectedDevices(const QString &packet)
{
    QStringList devices;
    QStringList lines = packet.split('\n', Qt::SkipEmptyParts);
    
    for (const QString &line : lines) {
        QString trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty()) {
            continue;
        }
        
        QStringList parts = trimmedLine.split('\t');
        if (parts.size() >= 2) {
            QString serial = parts[0].trimmed();
            QString state = parts[1].trimmed();
            
            // Only consider devices in "device" state (authorized and ready)
            if (state == "device") {
                devices.append(serial);
            }
        }
    }
    
    return devices;
}

} // namespace qsc

