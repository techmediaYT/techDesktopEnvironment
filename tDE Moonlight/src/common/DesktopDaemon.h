#pragma once

#include <QObject>
#include <QStringList>
#include <QDir>

// Long-running background service (separate binary, main_daemon.cpp)
// that owns state shared across the dock, app menu, and any future
// components -- currently just the pinned-apps list, persisted to a
// JSON file so it survives restarts. Exposed over the session D-Bus so
// multiple UI processes can read/write the same state without needing
// a shared database or IPC of their own.
class DesktopDaemon : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.tde.Moonlight.Daemon")

public:
    explicit DesktopDaemon(QObject *parent = nullptr);

public slots:
    QStringList getPinnedApplications();
    void pinApplication(const QString &appId);
    void unpinApplication(const QString &appId);

    // Deliberately NOT implemented as an actual uninstall action yet.
    // The earlier draft of this called `pkexec apt-get remove -y
    // <appId>` directly with the app id coming straight from UI state
    // with no validation -- that's a real command-injection-adjacent
    // risk (pkexec runs as root after auth) and also hardcodes
    // apt/Debian as the package manager, which isn't guaranteed for
    // tOS. A real implementation needs: an allowlisted set of package
    // IDs (not arbitrary strings from the UI), a proper privileged
    // helper with a narrow D-Bus policy (PolicyKit action, not a blank
    // pkexec call), and package-manager abstraction if tOS isn't
    // strictly apt-based. Until that exists, this just emits a signal
    // so the UI can show "not yet supported" rather than silently
    // running a privileged command.
    void triggerUninstall(const QString &appId);

    void triggerSystemPowerAction(int actionCode);

signals:
    void dockConfigurationChanged();
    void systemNotificationBroadcast(const QString &title, const QString &message);
    void uninstallNotSupported(const QString &appId);

private:
    const QString m_configPath = QDir::homePath() + QStringLiteral("/.config/tde/dock.json");
    void ensureConfigExists();
    void savePinnedList(const QStringList &list);
};
