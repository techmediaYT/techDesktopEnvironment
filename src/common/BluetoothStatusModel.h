#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusObjectPath>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QVariantMap>
#include <QDebug>

// Real Bluetooth adapter power state and connection state via BlueZ's
// D-Bus API (org.bluez, system bus) -- same service every other Linux
// desktop's Bluetooth quick-toggle talks to.
//
// BlueZ models this as: one or more org.bluez.Adapter1 objects (radios,
// e.g. "/org/bluez/hci0") and zero or more org.bluez.Device1 objects
// (paired/known devices, each with its own Connected property). This
// only needs the first adapter it finds -- multi-adapter systems are
// rare enough that picking one deterministically (lowest path) is fine
// for a quick-settings toggle; a full Bluetooth settings app would list
// all of them.
class BluetoothStatusModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool powered READ powered WRITE setPowered NOTIFY changed)
    Q_PROPERTY(bool deviceConnected READ deviceConnected NOTIFY changed)
    Q_PROPERTY(QString connectedDeviceName READ connectedDeviceName NOTIFY changed)

public:
    explicit BluetoothStatusModel(QObject *parent = nullptr) : QObject(parent)
    {
        m_objectManager = new QDBusInterface(
            QStringLiteral("org.bluez"),
            QStringLiteral("/"),
            QStringLiteral("org.freedesktop.DBus.ObjectManager"),
            QDBusConnection::systemBus(),
            this);

        m_available = m_objectManager->isValid();
        if (!m_available) {
            // bluetoothd not running / no Bluetooth hardware / BlueZ
            // not installed -- honest "unavailable" state, same
            // reasoning as NetworkStatusModel.
            qWarning() << "BluetoothStatusModel: could not reach org.bluez"
                       << "-- Bluetooth tile will show as unavailable";
            return;
        }

        QDBusConnection::systemBus().connect(
            QStringLiteral("org.bluez"),
            QStringLiteral("/"),
            QStringLiteral("org.freedesktop.DBus.ObjectManager"),
            QStringLiteral("InterfacesAdded"),
            this, SLOT(handleObjectsChanged()));
        QDBusConnection::systemBus().connect(
            QStringLiteral("org.bluez"),
            QStringLiteral("/"),
            QStringLiteral("org.freedesktop.DBus.ObjectManager"),
            QStringLiteral("InterfacesRemoved"),
            this, SLOT(handleObjectsChanged()));

        // Property changes (Powered, Connected) come per-object over
        // org.freedesktop.DBus.Properties.PropertiesChanged, but that
        // signal doesn't carry the object path in its arguments (it's
        // implicit in the message's own path header) -- so this listens
        // system-wide, keyed to the interface names we care about,
        // rather than one subscription per adapter/device object.
        QDBusConnection::systemBus().connect(
            QString(), QString(),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"),
            this, SLOT(handlePropertiesChanged(QString, QVariantMap, QStringList)));

        refresh();
    }

    bool available() const { return m_available; }
    bool powered() const { return m_powered; }
    bool deviceConnected() const { return m_deviceConnected; }
    QString connectedDeviceName() const { return m_connectedDeviceName; }

    void setPowered(bool on)
    {
        if (!m_available || m_adapterPath.isEmpty() || on == m_powered) {
            return;
        }
        QDBusInterface adapterProps(
            QStringLiteral("org.bluez"),
            m_adapterPath,
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QDBusConnection::systemBus());
        adapterProps.call(QStringLiteral("Set"),
                           QStringLiteral("org.bluez.Adapter1"),
                           QStringLiteral("Powered"),
                           QVariant::fromValue(QDBusVariant(on)));
        // Same reasoning as NetworkStatusModel::setWifiHardwareEnabled:
        // wait for the real PropertiesChanged signal rather than
        // optimistically flipping local state.
    }

public slots:
    Q_INVOKABLE void togglePowered()
    {
        setPowered(!m_powered);
    }

signals:
    void changed();

private slots:
    void handleObjectsChanged() { refresh(); }

    void handlePropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
    {
        Q_UNUSED(changed)
        Q_UNUSED(invalidated)
        if (interface == QLatin1String("org.bluez.Adapter1") || interface == QLatin1String("org.bluez.Device1")) {
            refresh();
        }
    }

private:
    void refresh()
    {
        if (!m_available) {
            return;
        }

        bool changedAny = false;

        QDBusReply<QVariantMap> reply = m_objectManager->call(QStringLiteral("GetManagedObjects"));
        // GetManagedObjects actually returns
        // Dict<ObjectPath, Dict<String, Dict<String, Variant>>>, which
        // doesn't fit QVariantMap directly -- pull the raw QDBusArgument
        // out of the reply message instead of relying on QDBusReply's
        // automatic demarshalling for this particular shape.
        QDBusMessage msg = m_objectManager->call(QStringLiteral("GetManagedObjects"));
        if (msg.type() != QDBusMessage::ReplyMessage || msg.arguments().isEmpty()) {
            return;
        }

        QString newAdapterPath;
        bool newPowered = false;
        bool newDeviceConnected = false;
        QString newConnectedName;

        const QDBusArgument arg = msg.arguments().first().value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            QDBusObjectPath objectPath;
            arg >> objectPath;

            QMap<QString, QVariantMap> interfaces;
            arg >> interfaces;
            arg.endMapEntry();

            if (interfaces.contains(QStringLiteral("org.bluez.Adapter1"))) {
                if (newAdapterPath.isEmpty() || objectPath.path() < newAdapterPath) {
                    newAdapterPath = objectPath.path();
                    newPowered = interfaces[QStringLiteral("org.bluez.Adapter1")]
                                     .value(QStringLiteral("Powered")).toBool();
                }
            }
            if (interfaces.contains(QStringLiteral("org.bluez.Device1"))) {
                const QVariantMap deviceProps = interfaces[QStringLiteral("org.bluez.Device1")];
                if (deviceProps.value(QStringLiteral("Connected")).toBool()) {
                    newDeviceConnected = true;
                    newConnectedName = deviceProps.value(QStringLiteral("Name")).toString();
                }
            }
        }
        arg.endMap();

        if (newAdapterPath != m_adapterPath) { m_adapterPath = newAdapterPath; changedAny = true; }
        if (newPowered != m_powered) { m_powered = newPowered; changedAny = true; }
        if (newDeviceConnected != m_deviceConnected) { m_deviceConnected = newDeviceConnected; changedAny = true; }
        if (newConnectedName != m_connectedDeviceName) { m_connectedDeviceName = newConnectedName; changedAny = true; }

        if (changedAny) {
            emit changed();
        }
    }

    QDBusInterface *m_objectManager = nullptr;
    bool m_available = false;
    QString m_adapterPath;
    bool m_powered = false;
    bool m_deviceConnected = false;
    QString m_connectedDeviceName;
};
