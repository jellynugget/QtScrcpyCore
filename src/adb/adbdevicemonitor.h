#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QStringList>

namespace qsc {

class AdbDeviceMonitor : public QObject
{
    Q_OBJECT

public:
    explicit AdbDeviceMonitor(QObject *parent = nullptr);
    ~AdbDeviceMonitor();

    void start();
    void stop();
    bool isMonitoring() const { return m_monitoring; }

signals:
    void newDeviceConnected(const QString &serial);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void reconnect();

private:
    void sendTrackDevicesRequest();
    bool consumeOkay();
    QString nextPacket();
    void handlePacket(const QString &packet);
    QStringList parseConnectedDevices(const QString &packet);
    QString parseLength(const QByteArray &data);
    void startReconnectTimer();

private:
    QTcpSocket *m_socket;
    QTimer *m_reconnectTimer;
    QByteArray m_buffer;
    QStringList m_connectedDevices;
    bool m_monitoring;
    bool m_okayConsumed;
    int m_reconnectDelay;
    static const int ADB_PORT = 5037;
    static const QByteArray TRACK_DEVICES_REQUEST;
    static const int RECONNECT_DELAY_MS = 1000;
    static const int MAX_RECONNECT_DELAY_MS = 5000;
};

} // namespace qsc

