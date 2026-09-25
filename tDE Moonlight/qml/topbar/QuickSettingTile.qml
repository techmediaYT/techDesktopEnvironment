import QtQuick
import QtQuick.Layouts
import TDEMoonlight.Theme

// One toggle-style tile in the Control Panel (Wi-Fi, Bluetooth,
// Ethernet) -- icon, title, status subtitle, and a "..." button for
// per-toggle options. Clicking the main body of the tile (anywhere
// except the "..." button) calls onToggleRequested, which
// ControlPanel.qml wires to the real NetworkStatus/BluetoothStatus
// backends. "active" reflects real hardware/connection state and drives
// the tile's own highlighted-when-on look, independent of hover.
Item {
    id: tile

    property string iconName: ""
    property string title: ""
    property string subtitle: ""
    property bool isDarkBackground: true
    property color foregroundColor: "white"
    // Whether this toggle is currently "on" (radio powered, cable
    // plugged in, etc) -- distinct from hover. Ethernet has no concept
    // of user-toggled on/off (see ControlPanel.qml), so it just reflects
    // whether a cable happens to be connected right now.
    property bool active: false
    // Set to false for tiles with no real toggle action (Ethernet --
    // there's nothing to switch, only a cable to plug in) so clicking
    // the body doesn't fire a no-op toggle request.
    property bool toggleable: true

    signal toggleRequested()
    signal optionsRequested()

    Layout.preferredHeight: 76

    Rectangle {
        id: background
        anchors.fill: parent
        radius: Theme.cornerRadius
        color: {
            const onTint = tile.isDarkBackground ? Qt.rgba(1, 1, 1, 0.28) : Qt.rgba(0, 0, 0, 0.16)
            const hoverTint = tile.isDarkBackground ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.08)
            const idleTint = tile.isDarkBackground ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.04)
            if (tile.active) return onTint
            return tileMouse.containsMouse ? hoverTint : idleTint
        }

        Behavior on color { ColorAnimation { duration: 120 } }

        // Whole-tile click target. Declared first (and thus painted/
        // hit-tested below) the RowLayout below it, so the smaller
        // "..." button's own MouseArea -- being on top -- still gets
        // priority for clicks inside its own bounds.
        MouseArea {
            id: tileMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                if (tile.toggleable) {
                    tile.toggleRequested()
                }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            Image {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                source: "image://icontheme/" + tile.iconName + "?tint=" +
                        tile.foregroundColor.toString().replace("#", "")
                sourceSize.width: 26
                sourceSize.height: 26
                fillMode: Image.PreserveAspectFit
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: tile.title
                    color: tile.foregroundColor
                    font.family: "Inter"
                    font.pixelSize: 16
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: tile.subtitle
                    color: tile.foregroundColor
                    opacity: 0.65
                    font.family: "Inter"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: Theme.cornerRadius
                color: optionsMouse.containsMouse
                    ? (tile.isDarkBackground ? Qt.rgba(255, 255, 255, 0.2) : Qt.rgba(0, 0, 0, 0.12))
                    : (tile.isDarkBackground ? Qt.rgba(255, 255, 255, 0.1) : Qt.rgba(0, 0, 0, 0.06))

                Text {
                    anchors.centerIn: parent
                    text: "\u2026" // "..."
                    color: tile.foregroundColor
                    font.pixelSize: 14
                    font.bold: true
                }

                MouseArea {
                    id: optionsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    // Declared after (so painted/hit-tested above) the
                    // whole-tile MouseArea, and accepts the event
                    // outright, so clicking "..." opens per-toggle
                    // options (routed to tSettings, once it has real
                    // Wi-Fi/Bluetooth pages) without also firing the
                    // tile's own toggleRequested().
                    onClicked: (mouse) => {
                        mouse.accepted = true
                        tile.optionsRequested()
                    }
                }
            }
        }
    }
}
