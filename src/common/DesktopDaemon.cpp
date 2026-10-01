#include "DesktopDaemon.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QtDBus/QDBusConnection>

namespace {

// GNOME stores pinned dock/taskbar apps as a gsettings string-array key,
// e.g. "['firefox.desktop', 'org.gnome.Nautilus.desktop']". tOS is
// meant to be cross-compatible with an existing GNOME setup, so on
// first run (no tDE config yet) we import this instead of starting
// with an empty dock -- a one-time migration, not an ongoing sync;
// after this, tDE's own config is the source of truth.
QStringList importGnomeFavoriteApps()
{
    QProcess process;
    process.start(QStringLiteral("gsettings"),
                   {QStringLiteral("get"), QStringLiteral("org.gnome.shell"), QStringLiteral("favorite-apps")});
    if (!process.waitForFinished(3000) || process.exitCode() != 0) {
        // gsettings not installed, GNOME never configured on this
        // system, or the key doesn't exist -- any of these just mean
        // "nothing to import", not an error worth surfacing.
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();

    // Output looks like: ['firefox.desktop', 'org.gnome.Nautilus.desktop']
    // Extract the quoted entries and strip ".desktop" so they match our
    // own appId convention (desktop filename without extension).
    QStringList result;
    static const QRegularExpression entryPattern(QStringLiteral("'([^']+)'"));
    auto it = entryPattern.globalMatch(output);
    while (it.hasNext()) {
        QString id = it.next().captured(1);
        if (id.endsWith(QStringLiteral(".desktop"))) {
            id.chop(8);
        }
        result.append(id);
    }
    return result;
}

} // namespace

DesktopDaemon::DesktopDaemon(QObject *parent) : QObject(parent)
{
    ensureConfigExists();

    QDBusConnection connection = QDBusConnection::sessionBus();
    connection.registerObject(
        QStringLiteral("/org/tde/Moonlight/Daemon"), this,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
    connection.registerService(QStringLiteral("org.tde.Moonlight.Daemon"));
}

void DesktopDaemon::ensureConfigExists()
{
    QDir dir(QDir::homePath() + QStringLiteral("/.config/tde"));
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    QFile file(m_configPath);
    if (!file.exists() && file.open(QIODevice::WriteOnly)) {
        QJsonArray pinnedArray;
        for (const QString &id : importGnomeFavoriteApps()) {
            pinnedArray.append(id);
        }
        const QJsonDocument doc(QJsonObject{{QStringLiteral("pinned"), pinnedArray}});
        file.write(doc.toJson());
        file.close();
    }
}

QStringList DesktopDaemon::getPinnedApplications()
{
    QStringList pinnedList;
    QFile file(m_configPath);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonArray arr = doc.object().value(QStringLiteral("pinned")).toArray();
        for (const auto &val : arr) {
            pinnedList.append(val.toString());
        }
        file.close();
    }
    return pinnedList;
}

void DesktopDaemon::pinApplication(const QString &appId)
{
    QStringList current = getPinnedApplications();
    if (!current.contains(appId)) {
        current.append(appId);
        savePinnedList(current);
        emit dockConfigurationChanged();
    }
}

void DesktopDaemon::unpinApplication(const QString &appId)
{
    QStringList current = getPinnedApplications();
    if (current.contains(appId)) {
        current.removeAll(appId);
        savePinnedList(current);
        emit dockConfigurationChanged();
    }
}

void DesktopDaemon::savePinnedList(const QStringList &list)
{
    QFile file(m_configPath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonArray arr;
        for (const auto &str : list) {
            arr.append(str);
        }
        const QJsonDocument doc(QJsonObject{{QStringLiteral("pinned"), arr}});
        file.write(doc.toJson());
        file.close();
    }
}

void DesktopDaemon::triggerUninstall(const QString &appId)
{
    // See the header comment: not wired to an actual package removal
    // yet, deliberately. Emitting a signal here rather than silently
    // no-op-ing lets the UI tell the user why nothing happened.
    emit uninstallNotSupported(appId);
}

void DesktopDaemon::triggerSystemPowerAction(int actionCode)
{
    // 1 = power off, 2 = reboot. loginctl is the standard systemd-based
    // way to request this as an unprivileged user (policy-gated via
    // polkit rules rather than needing pkexec/root here).
    if (actionCode == 1) {
        QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("poweroff")});
    } else if (actionCode == 2) {
        QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("reboot")});
    }
}
