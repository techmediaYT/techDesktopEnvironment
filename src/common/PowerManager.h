#pragma once

#include <QObject>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>

// Real power/session actions via org.freedesktop.login1 (systemd-
// logind, system bus) -- the standard way every logind-based desktop
// (GNOME, KDE, and effectively everything else on a systemd distro)
// shuts down, reboots, suspends, or locks the session, with logind's
// own polkit rules deciding whether the calling user is allowed to
// (typically yes, for the seat's active session, without a password
// prompt).
//
// This deliberately does not implement its own confirmation dialog
// logic -- PowerMenu.qml owns "are you sure", this class only performs
// the call once asked. Also deliberately doesn't try to detect "can
// this machine suspend" beyond CanSuspend()'s own answer -- logind
// already accounts for missing hardware support, disabled polkit
// rules, and everything else that could make an action unavailable.
//
// Registered as a QML singleton (see topbar/main.cpp).
class PowerManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool canPowerOff READ canPowerOff NOTIFY changed)
    Q_PROPERTY(bool canReboot READ canReboot NOTIFY changed)
    Q_PROPERTY(bool canSuspend READ canSuspend NOTIFY changed)
    Q_PROPERTY(bool canHibernate READ canHibernate NOTIFY changed)
    Q_PROPERTY(bool canLock READ canLock NOTIFY changed)

public:
    explicit PowerManager(QObject *parent = nullptr) : QObject(parent)
    {
        m_managerIface = new QDBusInterface(
            QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"),
            QDBusConnection::systemBus(),
            this);

        m_available = m_managerIface->isValid();
        if (!m_available) {
            qWarning() << "PowerManager: could not reach org.freedesktop.login1"
                       << "-- power menu actions will show as unavailable";
            return;
        }

        // Also need the session-locking capability, which lives on the
        // Session object for the session we're actually running in, not
        // on the Manager -- look that up once at startup rather than on
        // every open of the power menu.
        QDBusInterface sessionLookup(
            QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"),
            QDBusConnection::systemBus());
        QDBusReply<QDBusObjectPath> sessionPathReply =
            sessionLookup.call(QStringLiteral("GetSessionByPID"), (uint)QCoreApplication::applicationPid());
        if (sessionPathReply.isValid()) {
            m_sessionPath = sessionPathReply.value().path();
        }

        refreshCapabilities();
    }

    bool available() const { return m_available; }
    bool canPowerOff() const { return m_canPowerOff; }
    bool canReboot() const { return m_canReboot; }
    bool canSuspend() const { return m_canSuspend; }
    bool canHibernate() const { return m_canHibernate; }
    bool canLock() const { return !m_sessionPath.isEmpty(); }

public slots:
    // interactive=true (the "true" arguments below) lets logind/polkit
    // show its own auth prompt if the action isn't already allowed
    // outright for this session -- matches what every other desktop's
    // power menu passes, rather than silently failing for a user who'd
    // need to authenticate.
    Q_INVOKABLE void powerOff()
    {
        if (m_available) m_managerIface->call(QStringLiteral("PowerOff"), true);
    }
    Q_INVOKABLE void reboot()
    {
        if (m_available) m_managerIface->call(QStringLiteral("Reboot"), true);
    }
    Q_INVOKABLE void suspend()
    {
        if (m_available) m_managerIface->call(QStringLiteral("Suspend"), true);
    }
    Q_INVOKABLE void hibernate()
    {
        if (m_available) m_managerIface->call(QStringLiteral("Hibernate"), true);
    }
    Q_INVOKABLE void lockSession()
    {
        if (m_sessionPath.isEmpty()) {
            return;
        }
        QDBusInterface sessionIface(
            QStringLiteral("org.freedesktop.login1"),
            m_sessionPath,
            QStringLiteral("org.freedesktop.login1.Session"),
            QDBusConnection::systemBus());
        if (sessionIface.isValid()) {
            sessionIface.call(QStringLiteral("Lock"));
        }
    }
    // Ends the current graphical session without powering the machine
    // off -- logind's Session.Terminate is the correct call for "log
    // out" (as opposed to Manager.PowerOff/Reboot, which affect the
    // whole machine).
    Q_INVOKABLE void logOut()
    {
        if (m_sessionPath.isEmpty()) {
            return;
        }
        QDBusInterface sessionIface(
            QStringLiteral("org.freedesktop.login1"),
            m_sessionPath,
            QStringLiteral("org.freedesktop.login1.Session"),
            QDBusConnection::systemBus());
        if (sessionIface.isValid()) {
            sessionIface.call(QStringLiteral("Terminate"));
        }
    }

signals:
    void changed();

private:
    void refreshCapabilities()
    {
        bool changedAny = false;

        // Each Can* call replies with one of "yes" / "no" / "challenge"
        // (challenge = allowed after a polkit auth prompt) -- treat
        // "challenge" as available too, same as "yes", since the
        // interactive=true argument on the action calls above is
        // exactly what lets that prompt happen rather than the call
        // just failing outright.
        auto checkCan = [this](const QString &method) -> bool {
            QDBusReply<QString> reply = m_managerIface->call(method);
            if (!reply.isValid()) {
                return false;
            }
            const QString result = reply.value();
            return result == QLatin1String("yes") || result == QLatin1String("challenge");
        };

        const bool newPowerOff = checkCan(QStringLiteral("CanPowerOff"));
        const bool newReboot = checkCan(QStringLiteral("CanReboot"));
        const bool newSuspend = checkCan(QStringLiteral("CanSuspend"));
        const bool newHibernate = checkCan(QStringLiteral("CanHibernate"));

        if (newPowerOff != m_canPowerOff) { m_canPowerOff = newPowerOff; changedAny = true; }
        if (newReboot != m_canReboot) { m_canReboot = newReboot; changedAny = true; }
        if (newSuspend != m_canSuspend) { m_canSuspend = newSuspend; changedAny = true; }
        if (newHibernate != m_canHibernate) { m_canHibernate = newHibernate; changedAny = true; }

        if (changedAny) {
            emit changed();
        }
    }

    QDBusInterface *m_managerIface = nullptr;
    QString m_sessionPath;
    bool m_available = false;
    bool m_canPowerOff = false;
    bool m_canReboot = false;
    bool m_canSuspend = false;
    bool m_canHibernate = false;
};
