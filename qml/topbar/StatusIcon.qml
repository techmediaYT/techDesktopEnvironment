import QtQuick
import TDEMoonlight.TopBar

// One icon in the right-hand status cluster (wifi, bluetooth, battery,
// volume, etc). Icons are sourced from the system icon theme
// (Adwaita/Breeze, whatever's installed) at runtime via the
// "icontheme" image provider registered in main.cpp, which wraps
// QIcon::fromTheme() -- see src/common/IconThemeProvider.h.
Item {
    id: root

    // Plain icon name, e.g. "network-wireless-symbolic" -- NOT a full
    // "image://..." URL. The actual Image.source below appends the
    // "?tint=" query itself so the recoloring always matches textColor;
    // if this were a full URL instead, changing textColor later
    // wouldn't be reflected since the tint is baked in at request time.
    property string iconName: ""
    property string label: ""
    property color textColor: "white"

    // Per this project's explicit requirement: if the current icon
    // theme has no dedicated icon for this exact name (e.g. no
    // "bluetooth-disabled-symbolic" in a theme that only ships the
    // active glyph), show nothing at all rather than falling back to
    // IconThemeProvider's generic "unknown app" icon, which would read
    // as "Bluetooth is on" when it's actually off. IconChecker.hasIcon
    // wraps QIcon::hasThemeIcon() -- see IconThemeProvider.h.
    readonly property bool resolvable: root.iconName.length > 0 && IconChecker.hasIcon(root.iconName)

    visible: root.resolvable
    implicitWidth: root.resolvable ? row.implicitWidth : 0
    implicitHeight: root.resolvable ? 20 : 0

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Image {
            id: img
            width: 16
            height: 16
            // Image providers get (0,0) as the requested size unless
            // sourceSize is set explicitly -- without this the provider
            // falls back to its own default and the icon can end up
            // blurry/mis-scaled.
            sourceSize.width: 16
            sourceSize.height: 16
            fillMode: Image.PreserveAspectFit
            anchors.verticalCenter: parent.verticalCenter
            // Symbolic Adwaita icons are silhouettes with a fixed dark
            // fill baked into the SVG (GTK recolors them via CSS at
            // paint time; plain QIcon::fromTheme() doesn't). The
            // "?tint=" param tells IconThemeProvider to recolor the
            // pixmap itself (plain CPU compositing) so it always
            // matches the panel's current foreground color -- this
            // used to be done via MultiEffect, a GPU shader effect that
            // doesn't work under QT_QUICK_BACKEND=software and forces
            // a GL/EGL context to exist even when it does, which is
            // what caused "importing the supplied dmabufs failed" under
            // Sway in a VM with no working GPU passthrough.
            source: root.resolvable
                ? "image://icontheme/" + root.iconName + "?tint=" + root.textColor.toString().replace("#", "")
                : ""
        }

        Text {
            visible: root.label.length > 0
            text: root.label
            color: root.textColor
            font.pixelSize: 12
            font.family: "Inter"
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
