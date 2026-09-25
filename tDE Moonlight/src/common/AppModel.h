#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QMap>
#include <QTimer>

struct AppItem {
    QString id;         // .desktop filename without extension, e.g. "firefox"
    QString name;        // Name= from the desktop entry
    QString iconName;    // Icon= from the desktop entry (theme icon name or path)
    QString execPath;    // Exec= with field codes (%f, %u, etc) stripped
    bool isPinned = false;
    bool isRunning = false; // see checkRunningProcesses() below for what this actually tracks
};

// Real application list sourced from XDG .desktop files under the
// standard system/user application directories -- not a hardcoded
// placeholder list. Pin state is fetched from DesktopDaemon over D-Bus
// so it persists across dock restarts and stays in sync if multiple
// components (dock, app menu) change it.
class AppModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum AppRoles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        IconNameRole,
        ExecRole,
        PinnedRole,
        RunningRole
    };

    explicit AppModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void togglePinState(const QString &appId);
    Q_INVOKABLE void launchApplication(const QString &appId);

    // Re-scan the .desktop directories. Apps installed/removed after
    // startup won't appear until this (or a restart) runs -- there's no
    // filesystem watcher wired up yet; that's a reasonable follow-up if
    // live install/uninstall needs to reflect immediately.
    Q_INVOKABLE void refresh();

private:
    void loadDesktopEntries();
    void refreshPinnedStateFromDaemon();
    void checkRunningProcesses();

    QList<AppItem> m_apps;

    // appId -> PIDs of processes we personally launched via
    // launchApplication() that (as of the last poll) are still alive.
    // This is a heuristic, not real window tracking: it only knows
    // about apps launched through the dock/app menu themselves, and
    // it can't tell "has an open window" from "process alive but
    // still starting up" or "running with no window at all" (a tray
    // app, say). Real active-window tracking would need the
    // wlr-foreign-toplevel-management protocol (same kind of
    // client-extension binding as PanelBlur.h uses for blur) -- a
    // reasonable follow-up, but a materially bigger lift than this.
    QMap<QString, QList<qint64>> m_runningPids;
    QTimer m_livenessTimer;
};
