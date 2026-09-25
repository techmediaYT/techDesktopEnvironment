#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusObjectPath>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDebug>

// Real Wi-Fi/Ethernet state and control via org.freedesktop.NetworkManager
// on the system bus. This is what every major Linux desktop (GNOME,
// KDE, elementary) actually talks to for network state -- there's no
// tDE-specific daemon needed for this, NetworkManager already is one.
//
// Registered as a QML singleton (see main.cpp), same pattern as
// BatteryMonitor.
//
// NM device-type constants (NM_DEVICE_TYPE_* from NetworkManager.h):
//   1 = Ethernet, 2 = Wi-Fi
// NM device-state constants (NM_DEVICE_STATE_*):
//   100 = Activated (i.e. actually connected, not just present)
class NetworkStatusModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool wifiHardwareEnabled READ wifiHardwareEnabled WRITE setWifiHardwareEnabled NOTIFY changed)
    Q_PROPERTY(bool wifiConnected READ wifiConnected NOTIFY changed)
    Q_PROPERTY(QString wifiSsid READ wifiSsid NOTIFY changed)
    Q_PROPERTY(bool ethernetConnected READ ethernetConnected NOTIFY changed)

public:
    explicit NetworkStatusModel(QObject *parent = nullptr) : QObject(parent)
    {
        m_iface = new QDBusInterface(
            QStringLiteral("org.freedesktop.NetworkManager"),
            QStringLiteral("/org/freedesktop/NetworkManager"),
            QStringLiteral("org.freedesktop.NetworkManager"),
            QDBusConnection::systemBus(),
            this);

        m_available = m_iface->isValid();
        if (!m_available) {
            // No system bus / NetworkManager not running (e.g. a
            // container or minimal test image) -- every property below
            // just reports safe "nothing connected" defaults forever,
            // same honest-empty-state approach as the rest of this
            // project rather than pretending to have a connection.
            qWarning() << "NetworkStatusModel: could not reach org.freedesktop.NetworkManager"
                       << "-- network tiles will show as unavailable";
            return;
        }

        // NetworkManager emits PropertiesChanged (via the standard
        // org.freedesktop.DBus.Properties interface) whenever
        // WirelessEnabled, PrimaryConnection, etc change -- subscribe
        // instead of polling.
        QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.NetworkManager"),
            QStringLiteral("/org/freedesktop/NetworkManager"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"),
            this, SLOT(handlePropertiesChanged(QString, QVariantMap, QStringList)));

        refresh();
    }

    bool available() const { return m_available; }
    bool wifiHardwareEnabled() const { return m_wifiHardwareEnabled; }
    bool wifiConnected() const { return m_wifiConnected; }
    QString wifiSsid() const { return m_wifiSsid; }
    bool ethernetConnected() const { return m_ethernetConnected; }

    // Setter path for the Q_PROPERTY WRITE above (bindable from QML as
    // NetworkStatus.wifiHardwareEnabled = false). Also exposed as its
    // own Q_INVOKABLE below for the tile's whole-area click handler,
    // which reads more clearly as "toggle" than a property assignment.
    void setWifiHardwareEnabled(bool enabled)
    {
        if (!m_available || enabled == m_wifiHardwareEnabled) {
            return;
        }
        m_iface->setProperty("WirelessEnabled", enabled);
        // Don't flip m_wifiHardwareEnabled locally here -- wait for the
        // PropertiesChanged signal NetworkManager sends back, so the UI
        // reflects what actually happened rather than what we asked
        // for (the request can silently fail, e.g. rfkill-blocked
        // hardware).
    }

public slots:
    // Whole-tile click target for the Wi-Fi QuickSettingTile.
    Q_INVOKABLE void toggleWifi()
    {
        setWifiHardwareEnabled(!m_wifiHardwareEnabled);
    }

signals:
    void changed();

private slots:
    void handlePropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
    {
        Q_UNUSED(interface)
        Q_UNUSED(changed)
        Q_UNUSED(invalidated)
        refresh();
    }

private:
    void refresh()
    {
        if (!m_available) {
            return;
        }

        bool changedAny = false;

        const bool newWifiHw = m_iface->property("WirelessEnabled").toBool();
        if (newWifiHw != m_wifiHardwareEnabled) {
            m_wifiHardwareEnabled = newWifiHw;
            changedAny = true;
        }

        // Walk GetDevices() rather than trust just PrimaryConnection --
        // PrimaryConnection only names ONE active connection (whichever
        // NM currently treats as the default route), but both Wi-Fi and
        // Ethernet can be active at once, and the status pill needs to
        // know about both independently to decide which icon to show.
        bool newWifiConnected = false;
        bool newEthConnected = false;
        QString newSsid;

        QDBusReply<QList<QDBusObjectPath>> devicesReply = m_iface->call(QStringLiteral("GetDevices"));
        if (devicesReply.isValid()) {
            for (const QDBusObjectPath &devicePath : devicesReply.value()) {
                QDBusInterface deviceProps(
                    QStringLiteral("org.freedesktop.NetworkManager"),
                    devicePath.path(),
                    QStringLiteral("org.freedesktop.NetworkManager.Device"),
                    QDBusConnection::systemBus());
                if (!deviceProps.isValid()) {
                    continue;
                }

                const uint deviceType = deviceProps.property("DeviceType").toUInt();
                const uint deviceState = deviceProps.property("State").toUInt();
                const bool isActivated = (deviceState == 100); // NM_DEVICE_STATE_ACTIVATED

                if (deviceType == 2 && isActivated) { // Wi-Fi
                    newWifiConnected = true;
                    QDBusInterface wifiIface(
                        QStringLiteral("org.freedesktop.NetworkManager"),
                        devicePath.path(),
                        QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless"),
                        QDBusConnection::systemBus());
                    if (wifiIface.isValid()) {
                        const QDBusObjectPath apPath =
                            wifiIface.property("ActiveAccessPoint").value<QDBusObjectPath>();
                        if (!apPath.path().isEmpty() && apPath.path() != QStringLiteral("/")) {
                            QDBusInterface apIface(
                                QStringLiteral("org.freedesktop.NetworkManager"),
                                apPath.path(),
                                QStringLiteral("org.freedesktop.NetworkManager.AccessPoint"),
                                QDBusConnection::systemBus());
                            if (apIface.isValid()) {
                                const QByteArray ssidBytes = apIface.property("Ssid").toByteArray();
                                newSsid = QString::fromUtf8(ssidBytes);
                            }
                        }
                    }
                } else if (deviceType == 1 && isActivated) { // Ethernet
                    newEthConnected = true;
                }
            }
        }

        if (newWifiConnected != m_wifiConnected) { m_wifiConnected = newWifiConnected; changedAny = true; }
        if (newEthConnected != m_ethernetConnected) { m_ethernetConnected = newEthConnected; changedAny = true; }
        if (newSsid != m_wifiSsid) { m_wifiSsid = newSsid; changedAny = true; }

        if (changedAny) {
            emit changed();
        }
    }

    QDBusInterface *m_iface = nullptr;
    bool m_available = false;
    bool m_wifiHardwareEnabled = false;
    bool m_wifiConnected = false;
    bool m_ethernetConnected = false;
    QString m_wifiSsid;
};
