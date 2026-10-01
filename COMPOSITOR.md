# tDE Compositor -- scope notes (not implemented)

This document exists because a request came in to "implement the
compositor" and to make tDE's topbar "global... so it even overrides
Sway's topbar, by hiding and replacing it." Neither of those is
something that could be honestly delivered as an incremental addition
to this codebase, so this file records what's actually true instead of
letting the idea quietly disappear or get misrepresented as done.

## What tDE Moonlight currently is

Every binary in this repo (tde-topbar, tde-dock, tde-wallpaper,
tsettings, about-tde) is a Wayland client. They use LayerShellQt / the
wlr-layer-shell protocol to ask whatever compositor is already running
(Sway, in the scenario described) for a strip of screen space -- that's
what "layer shell" means. A client cannot compel the compositor it's
connected to hide its own panel or hand over that responsibility; the
compositor is in charge of what gets drawn and where. There's no code
path in this project that could override Sway's own bar (waybar,
swaybar, whatever the user has configured) -- doing so isn't a matter
of writing more QML, it's outside what a client is able to do to its
compositor at all.

## What "implementing the compositor" would actually require

A Wayland compositor is the process that:
- Owns the actual display output (DRM/KMS on bare hardware, or a nested
  window when running inside another compositor for development)
- Accepts and manages client connections (every app that wants to draw
  anything talks to it)
- Composites all client surfaces into final frames
- Implements the core Wayland protocol plus the extension protocols
  real apps expect (xdg-shell for window management, xdg-decoration
  for who draws titlebars, wlr-layer-shell -- the protocol tDE's own
  panels already use, input-method, presentation-time, etc.)
- Routes keyboard/pointer/touch input to the right client
- Handles window placement, focus, workspaces/tiling if wanted

In practice nobody writes this from raw Wayland protocol code anymore;
real projects build on wlroots (C, what Sway itself is built on) or
Smithay (Rust). Either is a multi-thousand-line integration even when
reusing one of those libraries for the hard parts. This is comparable
in scope to Sway, KWin, or Mutter's compositor core -- not a feature to
bolt onto a set of layer-shell panels.

## What this repo has instead, and why it's the right scope

Given the above, the actual goal behind the request -- tDE's own apps
having a single consistent, on-brand titlebar -- is achievable a
different way: client-side decoration (CSD), the same approach
GNOME/GTK apps use. qml/decoration/TitleBar.qml is a QML component any
tDE app can put at the top of its own window to get the exact titlebar
from the mockups (icon, title, minimize/maximize/close, drag-to-move).
about-tde and tsettings both use it now.

The real limitation this doesn't solve: CSD only works for windows
whose own application code includes the component. It cannot appear on
a terminal emulator, a browser, or any other app that isn't tDE-aware,
and it cannot replace or suppress another compositor's own bar. Full
control over every window's decoration -- including replacing Sway's
topbar -- genuinely needs the compositor described above.

## If this gets picked up later

Realistic path, in order:
1. Prototype as a nested compositor first (runs in a window inside your
   existing session, via wlroots' built-in Wayland/X11 backends) -- far
   faster iteration than fighting real DRM output.
2. Start from wlroots' own example compositors (tinywl is the canonical
   minimal starting point) rather than from protocol bindings directly.
3. Layer-shell support (topbar/dock positioning), xdg-shell for normal
   app windows, then decide the actual titlebar policy (SSD vs CSD, and
   whether tDE's compositor forces SSD off for its own apps since they
   already draw TitleBar.qml themselves).
4. Only after that works standalone does "replacing Sway" become
   coherent at all -- and at that point it's not replacing Sway within
   a Sway session, it's tDE's own compositor being what the user logs
   into instead of Sway.
