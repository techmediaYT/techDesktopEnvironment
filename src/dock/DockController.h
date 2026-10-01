#pragma once

#include <QObject>

// Bridges a global "open the app menu" request into the dock's QML.
//
// Wayland deliberately doesn't let an ordinary client grab a truly
// global hotkey -- only the compositor/WM owns the keyboard, and the
// dock's own layer-shell surface is set to KeyboardInteractivityNone
// (see main.cpp), so an in-process QML Shortcut item would never fire
// for Super+A even if we added one. The correct place for a global
// keybinding to live is the compositor's own config, which then calls
// into the running app via IPC -- D-Bus is the standard mechanism for
// that on Linux.
//
// This class exports exactly one D-Bus method for that purpose. Under
// Sway (used for dev/testing per the project setup), bind it like:
//
//   bindsym $mod+a exec dbus-send --session --type=method_call \
//       --dest=org.tde.Moonlight.Dock /org/tde/Moonlight/Dock \
//       org.tde.Moonlight.Dock.toggleAppMenu
//
// tOS's own compositor, once it exists, can call this the same way --
// or this can be swapped for the newer xdg-desktop-portal
// GlobalShortcuts portal later without changing anything on the QML
// side, since both would just end up emitting toggleAppMenuRequested().
class DockController : public QObject
{
    Q_OBJECT

public:
    explicit DockController(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    Q_SCRIPTABLE void toggleAppMenu() { emit toggleAppMenuRequested(); }

signals:
    void toggleAppMenuRequested();
};
