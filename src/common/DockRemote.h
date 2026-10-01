#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDebug>

// Topbar-side counterpart to DockController.h -- calls the same D-Bus
// method a compositor keybinding would (org.tde.Moonlight.Dock ->
// toggleAppMenu), so the topbar's own overview/app-switcher icon can
// open the dock's app menu even though topbar and dock are separate
// processes with no shared QML tree.
//
// There is currently no separate "window switcher" feature anywhere in
// this codebase (no Alt-Tab overlay, no per-window list) -- the app
// menu (search + grid of installed apps) is the only overview-style
// surface that exists, so that's what this icon opens. If a real
// window switcher gets built later, this is the file to point at it
// instead.
//
// Registered as a QML singleton (see topbar/main.cpp).
class DockRemote : public QObject
{
    Q_OBJECT

public:
    explicit DockRemote(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    Q_INVOKABLE void toggleAppMenu()
    {
        // Constructed fresh per call rather than held as a member --
        // the dock process might not be running yet when topbar starts
        // (or might restart later), and QDBusInterface doesn't need a
        // live peer to construct successfully, only to actually call
        // successfully, so there's nothing to gain from caching it.
        QDBusInterface dockIface(
            QStringLiteral("org.tde.Moonlight.Dock"),
            QStringLiteral("/org/tde/Moonlight/Dock"),
            QStringLiteral("org.tde.Moonlight.Dock"),
            QDBusConnection::sessionBus());

        if (!dockIface.isValid()) {
            qWarning() << "DockRemote: tde-dock isn't running or hasn't claimed"
                       << "org.tde.Moonlight.Dock yet -- app menu can't be opened remotely";
            return;
        }
        dockIface.call(QDBus::NoBlock, QStringLiteral("toggleAppMenu"));
    }
};
