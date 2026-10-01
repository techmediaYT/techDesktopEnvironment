#include "AppModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QProcess>
#include <QStandardPaths>
#include <QSet>
#include <QRegularExpression>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>

namespace {

// Directories where .desktop files live, per the XDG Desktop Entry
// spec: system-wide locations first, then user-local overrides. Later
// entries with the same filename take precedence over earlier ones
// (user overrides win), matching standard XDG behavior.
QStringList applicationDirectories()
{
    QStringList dirs = {
        QStringLiteral("/usr/share/applications"),
        QStringLiteral("/usr/local/share/applications"),
    };
    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (!userDir.isEmpty()) {
        dirs << userDir;
    }
    return dirs;
}

// Minimal manual parser for the "[Desktop Entry]" group of a .desktop
// file. Not using QSettings(..., QSettings::IniFormat) here because
// .desktop files allow characters in values (e.g. "%" field codes,
// embedded "=" in Exec arguments) that QSettings' INI parsing doesn't
// always handle predictably -- a small dedicated parser is more
// reliable for exactly the handful of keys we actually need.
struct ParsedEntry {
    QString name;
    QString icon;
    QString exec;
    bool noDisplay = false;
    bool hidden = false;
    bool isApplication = true;
    bool valid = false;
};

ParsedEntry parseDesktopFile(const QString &path)
{
    ParsedEntry entry;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entry;
    }

    QTextStream in(&file);
    bool inDesktopEntryGroup = false;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();

        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        if (line.startsWith(QLatin1Char('['))) {
            inDesktopEntryGroup = (line == QStringLiteral("[Desktop Entry]"));
            continue;
        }

        if (!inDesktopEntryGroup) {
            continue;
        }

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }

        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        // Only the unlocalized keys are read -- localized variants like
        // "Name[de]=" are skipped, so displayed names stay in whatever
        // language the .desktop file's default Name= was authored in.
        // Proper locale-matched names are a reasonable follow-up.
        if (key == QStringLiteral("Name")) {
            entry.name = value;
        } else if (key == QStringLiteral("Icon")) {
            entry.icon = value;
        } else if (key == QStringLiteral("Exec")) {
            entry.exec = value;
        } else if (key == QStringLiteral("NoDisplay")) {
            entry.noDisplay = (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
        } else if (key == QStringLiteral("Hidden")) {
            entry.hidden = (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
        } else if (key == QStringLiteral("Type")) {
            entry.isApplication = (value == QStringLiteral("Application"));
        }
    }

    entry.valid = !entry.name.isEmpty() && entry.isApplication
                  && !entry.noDisplay && !entry.hidden;
    return entry;
}

// Strips XDG field codes (%f, %F, %u, %U, %i, %c, %k, %%, etc) from an
// Exec= value -- these are meant to be substituted with filenames/URLs
// by whatever launched the app with an argument, which doesn't apply
// when we're just launching it bare from the dock/app menu.
QString stripFieldCodes(const QString &exec)
{
    static const QRegularExpression fieldCodePattern(QStringLiteral("%[fFuUick%]"));
    QString result = exec;
    result.replace(fieldCodePattern, QString());
    return result.trimmed();
}

} // namespace

AppModel::AppModel(QObject *parent) : QAbstractListModel(parent)
{
    loadDesktopEntries();
    refreshPinnedStateFromDaemon();

    // Polling /proc rather than reacting to a signal because the
    // processes we're tracking are intentionally detached (see
    // launchApplication() for why) -- there's no QProcess::finished()
    // to connect to for a detached process. 2s is frequent enough for
    // a dock indicator to feel responsive without meaningfully
    // touching the CPU budget.
    m_livenessTimer.setInterval(2000);
    connect(&m_livenessTimer, &QTimer::timeout, this, &AppModel::checkRunningProcesses);
    m_livenessTimer.start();
}

void AppModel::loadDesktopEntries()
{
    m_apps.clear();

    // Track filenames already added so user-local entries can override
    // system ones without producing duplicate dock entries for the same
    // app id.
    QSet<QString> seenIds;

    for (const QString &dirPath : applicationDirectories()) {
        QDir dir(dirPath);
        if (!dir.exists()) {
            continue;
        }

        const auto files = dir.entryList(QStringList{QStringLiteral("*.desktop")}, QDir::Files);
        for (const QString &fileName : files) {
            const QString id = QFileInfo(fileName).completeBaseName();
            if (seenIds.contains(id)) {
                continue;
            }

            const ParsedEntry parsed = parseDesktopFile(dir.filePath(fileName));
            if (!parsed.valid) {
                continue;
            }

            AppItem item;
            item.id = id;
            item.name = parsed.name;
            // Fall back to Adwaita's generic executable icon when the
            // .desktop entry has no Icon= key (or names a theme icon
            // that doesn't exist) -- otherwise the tile just renders
            // blank instead of a recognizable placeholder.
            item.iconName = parsed.icon.isEmpty()
                ? QStringLiteral("application-x-executable-symbolic")
                : parsed.icon;
            item.execPath = stripFieldCodes(parsed.exec);
            item.isPinned = false; // set below, from the daemon

            m_apps.append(item);
            seenIds.insert(id);
        }
    }
}

void AppModel::refreshPinnedStateFromDaemon()
{
    QDBusInterface daemonInterface(
        QStringLiteral("org.tde.Moonlight.Daemon"),
        QStringLiteral("/org/tde/Moonlight/Daemon"),
        QStringLiteral("org.tde.Moonlight.Daemon"),
        QDBusConnection::sessionBus());

    if (!daemonInterface.isValid()) {
        // Daemon isn't running (e.g. testing the dock standalone) --
        // fall back to nothing pinned rather than failing to start.
        return;
    }

    QDBusReply<QStringList> reply = daemonInterface.call(QStringLiteral("getPinnedApplications"));
    if (!reply.isValid()) {
        return;
    }

    const QStringList pinned = reply.value();
    for (auto &item : m_apps) {
        item.isPinned = pinned.contains(item.id);
    }
}

int AppModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_apps.count();
}

QVariant AppModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_apps.count()) {
        return QVariant();
    }
    const auto &item = m_apps[index.row()];

    switch (role) {
    case IdRole: return item.id;
    case NameRole: return item.name;
    case IconNameRole: return item.iconName;
    case ExecRole: return item.execPath;
    case PinnedRole: return item.isPinned;
    case RunningRole: return item.isRunning;
    }
    return QVariant();
}

QHash<int, QByteArray> AppModel::roleNames() const
{
    return {
        {IdRole, "appId"},
        {NameRole, "appName"},
        {IconNameRole, "appIcon"},
        {ExecRole, "appExec"},
        {PinnedRole, "appPinned"},
        {RunningRole, "appRunning"},
    };
}

void AppModel::togglePinState(const QString &appId)
{
    QDBusInterface daemonInterface(
        QStringLiteral("org.tde.Moonlight.Daemon"),
        QStringLiteral("/org/tde/Moonlight/Daemon"),
        QStringLiteral("org.tde.Moonlight.Daemon"),
        QDBusConnection::sessionBus());

    if (!daemonInterface.isValid()) {
        return;
    }

    bool currentlyPinned = false;
    for (const auto &item : m_apps) {
        if (item.id == appId) {
            currentlyPinned = item.isPinned;
            break;
        }
    }

    if (currentlyPinned) {
        daemonInterface.call(QStringLiteral("unpinApplication"), appId);
    } else {
        daemonInterface.call(QStringLiteral("pinApplication"), appId);
    }

    QDBusReply<QStringList> reply = daemonInterface.call(QStringLiteral("getPinnedApplications"));
    if (reply.isValid()) {
        const QStringList updatedPins = reply.value();
        for (int i = 0; i < m_apps.count(); ++i) {
            const bool newState = updatedPins.contains(m_apps[i].id);
            if (m_apps[i].isPinned != newState) {
                m_apps[i].isPinned = newState;
                emit dataChanged(index(i), index(i), {PinnedRole});
            }
        }
    }
}

void AppModel::launchApplication(const QString &appId)
{
    for (int row = 0; row < m_apps.size(); ++row) {
        AppItem &item = m_apps[row];
        if (item.id != appId) {
            continue;
        }

        // Exec= values can be a full command line with arguments
        // (e.g. "code --new-window") -- QProcess::startDetached
        // with a single command string doesn't split that itself
        // on all platforms, so split it explicitly.
        const QStringList parts = item.execPath.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            return;
        }

        // Detached (not owned QProcess*) deliberately: the launched
        // app must keep running even if tde-dock itself crashes or
        // restarts -- an owned QProcess would terminate it on
        // destruction. The tradeoff is we can only poll for liveness
        // (checkRunningProcesses()) rather than get a finished()
        // signal; see the m_runningPids comment in AppModel.h.
        qint64 pid = 0;
        if (QProcess::startDetached(parts.first(), parts.mid(1), QString(), &pid)) {
            m_runningPids[appId].append(pid);
            if (!item.isRunning) {
                item.isRunning = true;
                const QModelIndex idx = index(row);
                emit dataChanged(idx, idx, {RunningRole});
            }
        }
        return;
    }
}

void AppModel::checkRunningProcesses()
{
    for (auto it = m_runningPids.begin(); it != m_runningPids.end();) {
        const QString appId = it.key();

        // /proc/<pid> existing is the standard cheap Linux liveness
        // check -- no signal needs to be sent, unlike kill(pid, 0).
        // Fine for a Linux-only desktop environment; would need a
        // different check (or QProcess::finished(), which needs an
        // owned process -- see launchApplication()) on any other OS.
        QList<qint64> &pids = it.value();
        pids.removeIf([](qint64 pid) {
            return !QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid));
        });

        if (pids.isEmpty()) {
            it = m_runningPids.erase(it);
            for (int row = 0; row < m_apps.size(); ++row) {
                if (m_apps[row].id == appId && m_apps[row].isRunning) {
                    m_apps[row].isRunning = false;
                    const QModelIndex idx = index(row);
                    emit dataChanged(idx, idx, {RunningRole});
                    break;
                }
            }
        } else {
            ++it;
        }
    }
}

void AppModel::refresh()
{
    beginResetModel();
    loadDesktopEntries();
    refreshPinnedStateFromDaemon();
    endResetModel();
}
