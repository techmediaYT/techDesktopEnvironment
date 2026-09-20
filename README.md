# techDesktopEnvironment (tDE)

**techDesktopEnvironment (tDE)** is a modern Linux desktop environment focused on a **clean user interface**, **intuitive workflows**, and a familiar experience for users transitioning from Windows.  
tDE is written entirely from scratch in **C++**, using **Qt 6** and **Qt Quick** for its interface, animations, and effects.

tDE ships **pre‑installed** on [techOperatingSystem (tOS)](https://github.com/techmediaYT/tOS), a Debian‑based Linux distribution designed around simplicity and performance.

---

## Features

- **Modern UI design** with acrylic blur and smooth animations  
- **Simple, intuitive layout** inspired by familiar desktop paradigms  
- **Modular architecture** (dock, top bar, wallpaper engine, daemon)  
- **Built with Qt 6** for fast, GPU‑accelerated rendering  
- **Wayland‑first** design using `libqt6waylandclient6` and LayerShellQt  
- **Lightweight core**, designed to stay responsive even on older hardware  

> ⚠️ *tDE is currently in active development. Components may be incomplete, unstable, or subject to change.*

---

## Hardware Requirements

- **GPU required** (integrated or dedicated)  
  - tDE uses heavy GPU‑accelerated effects (acrylic blur, shadows, animations)
- **Minimum RAM:** 2 GB  
- **Recommended:** 4 GB or more for a smooth experience

> ⚠️ **Do not run tDE with software rendering.**  
> Qt Quick’s software backend (`QT_QUICK_BACKEND=software`) is not supported and will cause severe graphical issues.

---

## Software Dependencies

tDE depends on the following libraries:

### Qt 6 Core & GUI
- `libqt6core6`
- `libqt6gui6`

### Qt Quick & QML
- `libqt6qml6`
- `libqt6quick6`
- `libqt6quickcontrols2-6`
- `libqt6quickeffects6`

### IPC & Wayland
- `libqt6dbus6`
- `libqt6waylandclient6`
- `liblayershellqt-interface6`
- `libwayland-client0`
- `dbus`

---

## Project Structure

tDE is composed of several core components:

- **tde-daemon**  
  Central background service responsible for IPC, settings, and global state.

- **tde-dock**  
  Application launcher and task manager.

- **tde-topbar**  
  System bar providing clock, indicators, and quick actions.

- **tde-wallpaper**  
  Wallpaper renderer and background engine.

- **tde-session** *(planned)*  
  Session manager responsible for starting all components and handling login/logout.

Each component is developed independently but will be unified under `tde-session` as the project matures.

---

## Project Status

tDE is currently in **early development**.  
Many components exist but are not yet fully integrated. Expect:

- Missing features  
- Visual inconsistencies  
- Crashes or incomplete modules  
- Rapid changes in architecture  

This repository is intended for contributors, testers, and early adopters.

---

## Building tDE (experimental)

tDE uses **CMake** (planned) or a simple build script depending on the component.

Example (placeholder):

```bash
mkdir tDE 
cd tDE
cmake ..
make -j$(nproc)
```
# Build platforms

<p align="left">
  <img src="https://www.qt.io/hubfs/built-with-Qt-badge-white.svg" alt="Built with Qt" width="512">
</p>


