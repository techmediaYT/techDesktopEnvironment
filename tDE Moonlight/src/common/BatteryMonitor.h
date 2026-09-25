#pragma once

#include <QObject>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QString>

// Reads real battery state from /sys/class/power_supply -- this works on
// any Linux system regardless of desktop environment, without depending
// on UPower/D-Bus being present. Polls on a timer since sysfs doesn't
// push change notifications the way D-Bus properties do.
//
// Registered as a QML singleton explicitly via qmlRegisterSingletonInstance
// in each component's main.cpp, rather than QML_ELEMENT/QML_SINGLETON --
// that macro-based approach needs the class visible to the QML type
// registrar via an angle-bracket __has_include check, which silently
// fails unless the header's directory is added to the include path in
// exactly the right way. Explicit registration avoids that entirely.
class BatteryMonitor : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int percentage READ percentage NOTIFY changed)
    Q_PROPERTY(bool charging READ charging NOTIFY changed)
    Q_PROPERTY(bool present READ present NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)

public:
    explicit BatteryMonitor(QObject *parent = nullptr) : QObject(parent)
    {
        findBatteryDevice();
        refresh();

        // Battery percentage doesn't need sub-second precision; every
        // 30s keeps the UI honest without pointless wakeups/disk reads.
        m_timer.setInterval(30000);
        connect(&m_timer, &QTimer::timeout, this, &BatteryMonitor::refresh);
        m_timer.start();
    }

    int percentage() const { return m_percentage; }
    bool charging() const { return m_charging; }
    bool present() const { return !m_devicePath.isEmpty(); }

    // Adwaita has no plain "battery-charging-symbolic" icon -- it only
    // ships bucketed "battery-level-<0,10,...,100>[-charging|-charged|
    // -plugged-in]-symbolic" names. Using an icon name that doesn't
    // actually exist in the theme is exactly why this rendered blank;
    // this builds a name that's guaranteed to resolve.
    QString iconName() const
    {
        if (m_percentage >= 100) {
            return m_charging
                ? QStringLiteral("battery-level-100-charged-symbolic")
                : QStringLiteral("battery-level-100-symbolic");
        }

        // Round down to the nearest 10 -- Adwaita only ships icons at
        // that granularity (0, 10, 20, ... 90).
        const int bucket = qBound(0, (m_percentage / 10) * 10, 90);
        return QStringLiteral("battery-level-%1%2-symbolic")
            .arg(bucket)
            .arg(m_charging ? QStringLiteral("-charging") : QString());
    }

signals:
    void changed();

private:
    void findBatteryDevice()
    {
        // Look for the first BAT* entry under /sys/class/power_supply.
        // Systems can have zero (desktops), one, or multiple batteries
        // (some laptops report a secondary/peripheral battery too) --
        // this takes the first real battery-type device it finds.
        const QDir base(QStringLiteral("/sys/class/power_supply"));
        const auto entries = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            const QString typePath = base.filePath(entry) + QStringLiteral("/type");
            QFile typeFile(typePath);
            if (typeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString type = QString::fromUtf8(typeFile.readAll()).trimmed();
                if (type == QStringLiteral("Battery")) {
                    m_devicePath = base.filePath(entry);
                    return;
                }
            }
        }
        // No battery found (desktop machine, or this VM) -- present()
        // will report false and callers should hide the battery icon.
    }

    void refresh()
    {
        if (m_devicePath.isEmpty()) {
            return;
        }

        bool changedAny = false;

        int newPercentage = m_percentage;
        QFile capacityFile(m_devicePath + QStringLiteral("/capacity"));
        if (capacityFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            bool ok = false;
            const int value = QString::fromUtf8(capacityFile.readAll()).trimmed().toInt(&ok);
            if (ok) {
                newPercentage = value;
            }
        }
        if (newPercentage != m_percentage) {
            m_percentage = newPercentage;
            changedAny = true;
        }

        bool newCharging = m_charging;
        QFile statusFile(m_devicePath + QStringLiteral("/status"));
        if (statusFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString status = QString::fromUtf8(statusFile.readAll()).trimmed();
            // sysfs reports "Charging", "Discharging", "Full", "Not charging",
            // "Unknown" -- anything other than literally "Charging" counts
            // as not-charging for our purposes.
            newCharging = (status == QStringLiteral("Charging"));
        }
        if (newCharging != m_charging) {
            m_charging = newCharging;
            changedAny = true;
        }

        if (changedAny) {
            emit changed();
        }
    }

    QString m_devicePath;
    int m_percentage = 0;
    bool m_charging = false;
    QTimer m_timer;
};
