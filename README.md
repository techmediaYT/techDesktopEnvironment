# tDE Moonlight

Four separate binaries make up the desktop environment so far, matching
how real DEs are structured (one process crashing shouldn't take the
whole desktop down):

- **`tde-topbar`** — the top status bar (clock pill, notif pill, status
  icons). Wayland layer-shell panel anchored to the top edge.
- **`tde-dock`** — bottom-anchored dock with pinned app icons and an
  app-launcher button that opens the app menu.
- **`tde-wallpaper`** — background-layer surface covering the whole
  screen. Currently just a flat fill color; real wallpaper image
  support isn't implemented yet.
- **`tde-daemon`** — headless background service (no window). Owns the
  pinned-apps list, persisted to `~/.config/tde/dock.json`, exposed
  over the session D-Bus (`org.tde.Moonlight.Daemon`) so the dock and
  app menu can read/write shared state without their own IPC.

## Stack

- **C++17 / Qt6** (Core, Gui, Qml, Quick, QuickControls2, QuickEffects, DBus)
- **LayerShellQt** — KDE's Qt bindings for the `wlr-layer-shell` Wayland
  protocol. Anchors panels to a screen edge, reserves space
  (`exclusiveZone`) so windows don't overlap them, no title bar.

## Build dependencies

```bash
sudo apt install build-essential cmake pkg-config \
    qt6-base-dev qt6-declarative-dev \
    qt6-wayland-dev \
    libwayland-dev \
    liblayershellqtinterface-dev \
    qml6-module-org-kde-layershell
```

Package names vary by distro.

## Build

```bash
cmake -B tDE -DCMAKE_BUILD_TYPE=Release
cmake --build tDE
```

## Running

The daemon needs to be running **before** the dock/app menu, since
they fetch pin state from it over D-Bus at startup (if it's not
running, they just silently start with nothing pinned rather than
failing). Easiest is the supervisor script, which also handles crash
detection/fallback (see below):

```bash
./scripts/tde-session
```

Or run components individually for debugging:

```bash
./tDE/tde-daemon &
./tDE/tde-topbar &
./tDE/tde-dock &
./tDE/tde-wallpaper &
```

You need a **Wayland session with a wlr-layer-shell-capable
compositor** to see real panel/dock behavior (KWin, Sway, Hyprland).
**GNOME/Mutter does not implement wlr-layer-shell** — panels won't
anchor correctly there.

### Known VM workaround (VirtualBox)

If you hit `importing the supplied dmabufs failed`, force software
rendering:

```bash
QT_QUICK_BACKEND=software QT_QPA_PLATFORM=wayland ./tDE/tde-topbar
```

## What's real vs. stubbed

**Real:**
- Layer-shell panel/dock behavior (anchoring, exclusive zone, no
  keyboard focus grab) — confirmed working on Sway
- Live clock, real battery via `/sys/class/power_supply` (scans for
  any battery device, not hardcoded to `BAT0`)
- Real system icons via `QIcon::fromTheme()`, pinned to Adwaita, with
  symbolic status icons recolored via `MultiEffect` to match the
  panel's adaptive light/dark tint
- Real `.desktop` file parsing for the app list
- Pinned-apps persistence via the daemon + D-Bus, survives restarts
- **GNOME cross-compatibility**: on first run (no existing tDE config
  yet), the daemon imports GNOME's `favorite-apps` gsettings key so
  your existing GNOME-pinned apps show up in the tDE dock immediately,
  rather than starting empty. This is a one-time import, not an
  ongoing sync — after first run, tDE's own config is authoritative.
- **Real compositor blur**, targeting the modern `ext-background-effect-v1`
  Wayland protocol (protocol XML bundled under `protocols/`, since the
  canonical upstream host isn't reachable from where this was built —
  reconstructed from the published spec at
  wayland.app/protocols/ext-background-effect-v1; diff against upstream
  if something doesn't match). **Important:** per that protocol's own
  published compositor-support table, only KWin (6.7+), GNOME's Mutter
  (51+), and niri currently implement it. **Sway (1.11, what you're
  testing against) does not** — the code will compile and run fine,
  and silently do nothing visually, on Sway specifically. This isn't a
  bug to chase; it's a real gap in Sway's protocol support as of now.
  If wayland-scanner or the linker complains about this code, the
  bundled protocol XML (reconstructed rather than fetched from
  upstream) is the first thing to double check.
- **Crash fallback**: `scripts/tde-session` supervises the daemon/
  topbar/dock/wallpaper. If topbar or dock crashes, it saves a local
  crash log to `~/.local/share/tde/crash-reports/` and offers
  (via zenity/kdialog, or a terminal prompt if neither's installed) to
  fall back to a GNOME session. Two honest caveats: "send report"
  currently only saves the log locally — there's no backend to
  actually submit it anywhere. And the GNOME fallback just execs
  `gnome-session` directly; whether that produces a fully working
  desktop depends on your login manager/session setup, not something
  this script can fully guarantee in every configuration.

**Stubbed / not yet implemented:**
- `isDarkBackground` is a hardcoded property per-window, not driven by
  actual wallpaper luminance.
- Wallpaper images — `tde-wallpaper` is a flat color fill only.
- Uninstall — deliberately not implemented (see comments in
  `DesktopDaemon.h`/`AppMenu.qml`) due to the security problems in an
  earlier draft that shelled out to `pkexec apt-get remove` with
  unvalidated input.
- Notification center — notif pill has a click hook, no backend.
- App list doesn't live-update after startup (no filesystem watcher).
- Volume/wifi/bluetooth status icons are placeholder states, not bound
  to real PipeWire/NetworkManager/bluez state yet.
- Blur doesn't re-apply on window resize (fine for topbar/dock, which
  don't resize after initial setup).
