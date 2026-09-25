import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import TDEMoonlight.Dock
import TDEMoonlight.Theme

// Root window. Actual size/position/layer-shell anchoring is set from
// main.cpp once this window exists -- don't hardcode geometry here.
// The window itself is transparent and sits at the bottom-left corner
// of the screen (Left+Bottom layer-shell anchors); the actual visible
// dock is the floating horizontal capsule below.
Window {
    id: dockRoot

    color: "transparent"
    flags: Qt.FramelessWindowHint

    // Same adaptive-tint pattern as TopBar.qml -- see that file for
    // the fuller explanation. Currently hardcoded; wiring both to a
    // shared source (real wallpaper luminance) is a follow-up once
    // that exists.
    property bool isDarkBackground: true
    property color foregroundColor: isDarkBackground ? "white" : "#1a1a1a"

    AppMenu {
        id: globalAppMenu
        isDarkBackground: dockRoot.isDarkBackground
        // No width/height binding here anymore -- dockRoot is now a
        // small bottom-left corner window sized to the dock capsule's
        // content, not full-screen, so binding to its size would be
        // wrong. AppMenu's own layer-shell setup (LayerShellHelper,
        // anchored to all 4 edges) makes the compositor size it to the
        // full screen regardless of whatever width/height it's given
        // here.
    }

    // Fires when something calls DockController's D-Bus method
    // (org.tde.Moonlight.Dock.toggleAppMenu) -- see DockController.h for
    // why Super+A has to come in this way instead of a QML Shortcut.
    Connections {
        target: DockController
        function onToggleAppMenuRequested() {
            globalAppMenu.isOpen = !globalAppMenu.isOpen
        }
    }

    // Horizontal capsule anchored to the bottom-left corner (matching
    // the mockups and dock/main.cpp's Left+Bottom layer-shell anchors),
    // replacing the previous full-height left-edge vertical strip.
    Rectangle {
        id: dockCapsule
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 16
        anchors.bottomMargin: 16

        height: 76
        radius: Theme.cornerRadius
        color: dockRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.14) : Qt.rgba(40, 40, 40, 0.22)
        border.color: dockRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.12) : Qt.rgba(0, 0, 0, 0.08)
        border.width: 1
        implicitWidth: layoutRow.implicitWidth + 48

        RowLayout {
            id: layoutRow
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 16

            // Pinned apps always show; unpinned apps show only while
            // they have a live process we launched (see AppModel's
            // isRunning/m_runningPids) -- this is the "active apps"
            // behavior, same idea as macOS's Dock or GNOME's
            // dash-to-dock: pinned + currently-open.
            Repeater {
                model: AppSystemModel
                delegate: Loader {
                    active: appPinned === true || appRunning === true
                    sourceComponent: Component {
                        DockItem {
                            iconSource: "image://icontheme/" + appIcon
                            appName: appName
                            appRunning: appRunning
                            dockHeight: dockCapsule.height
                            onClicked: AppSystemModel.launchApplication(appId)
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Layout.alignment: Qt.AlignVCenter
                radius: Theme.cornerRadius
                color: appLauncherMouse.containsMouse ? Qt.rgba(255, 255, 255, 0.15) : Qt.rgba(255, 255, 255, 0.06)

                Grid {
                    anchors.centerIn: parent
                    columns: 3
                    rows: 3
                    spacing: 4
                    Repeater {
                        model: 9
                        Rectangle { width: 4; height: 4; radius: 2; color: dockRoot.foregroundColor }
                    }
                }

                MouseArea {
                    id: appLauncherMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: globalAppMenu.isOpen = !globalAppMenu.isOpen
                }
            }
        }
    }
}
