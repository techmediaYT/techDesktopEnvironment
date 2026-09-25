import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import TDEMoonlight.TopBar
import TDEMoonlight.Theme

// Quick-settings panel opened by clicking the status pill in the
// topbar. Wi-Fi and Bluetooth tiles are backed by real state
// (NetworkStatus/BluetoothStatus, see src/common/) and their whole-tile
// click actually powers the radio on/off. Ethernet has no on/off
// concept -- it just reports whether a cable is plugged in. Project
// (screen sharing) and the media widget still have no backend
// (no display-portal integration, no MPRIS) -- those stay honest empty/
// placeholder states rather than fake toggles, same reasoning as
// before, just no longer true of Wi-Fi/Bluetooth/Ethernet.
Window {
    id: panelRoot

    visible: isOpen
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    property bool isOpen: false
    property bool isDarkBackground: true
    property color foregroundColor: isDarkBackground ? "white" : "#1a1a1a"

    // See AppMenu.qml for why this has to run before the window is
    // ever shown -- same layer-shell ordering constraint.
    Component.onCompleted: LayerShellHelper.configureOverlay(panelRoot, "tde-moonlight-controlpanel")

    // Full-window scrim behind panelStack -- same click-outside-to-
    // dismiss fix, and same same-frame click-through guard, as
    // AppMenu.qml's scrim. Without this the only way to close the
    // Control Panel was Escape, which is exactly the "can't get it off
    // the screen after I open it" bug.
    MouseArea {
        id: scrim
        anchors.fill: parent
        enabled: panelRoot.isOpen
        propagateComposedEvents: false

        property bool ignoreNextRelease: false

        onPressed: (mouse) => {
            if (ignoreNextRelease) {
                mouse.accepted = true
            }
        }
        onClicked: (mouse) => {
            if (ignoreNextRelease) {
                ignoreNextRelease = false
                return
            }
            panelRoot.isOpen = false
        }
    }

    onVisibleChanged: {
        if (visible) {
            scrim.ignoreNextRelease = true
        }
    }

    // Brief "tSettings -- coming soon" confirmation, shown in place of
    // the "All settings.." row's own label for a couple seconds after
    // SettingsLauncher reports it couldn't launch a real tsettings
    // binary. Beats a dead click with literally no feedback.
    Connections {
        target: SettingsLauncher
        function onNotImplementedYet() {
            comingSoonTimer.restart()
        }
    }
    Timer {
        id: comingSoonTimer
        interval: 2200
    }

    ColumnLayout {
        id: panelStack
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 64
        anchors.rightMargin: 16
        spacing: 12
        opacity: panelRoot.isOpen ? 1.0 : 0.0
        y: panelRoot.isOpen ? 0 : -12

        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        // --- Quick toggles card ---------------------------------------
        Rectangle {
            id: settingsCard
            Layout.preferredWidth: 420
            Layout.preferredHeight: settingsColumn.implicitHeight + 40
            radius: Theme.cornerRadius
            color: panelRoot.isDarkBackground ? Qt.rgba(45, 45, 45, 0.85) : Qt.rgba(235, 235, 235, 0.9)
            border.color: panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.1) : Qt.rgba(0, 0, 0, 0.06)
            border.width: 1

            // Swallows clicks on empty card padding -- same reasoning
            // as menuCanvas's equivalent MouseArea in AppMenu.qml, so
            // clicking inside the card doesn't fall through to the
            // full-window scrim and close the panel.
            MouseArea {
                anchors.fill: parent
                onClicked: {}
            }

            ColumnLayout {
                id: settingsColumn
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                GridLayout {
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 14
                    Layout.fillWidth: true

                    QuickSettingTile {
                        Layout.fillWidth: true
                        iconName: {
                            if (!NetworkStatus.available) return "network-wireless-symbolic"
                            if (!NetworkStatus.wifiHardwareEnabled) return "network-wireless-disabled-symbolic"
                            return NetworkStatus.wifiConnected
                                ? "network-wireless-signal-good-symbolic"
                                : "network-wireless-no-route-symbolic"
                        }
                        title: qsTr("Wi-Fi")
                        subtitle: {
                            if (!NetworkStatus.available) return qsTr("Unavailable")
                            if (!NetworkStatus.wifiHardwareEnabled) return qsTr("Off")
                            if (NetworkStatus.wifiConnected) return NetworkStatus.wifiSsid.length > 0
                                ? NetworkStatus.wifiSsid : qsTr("Connected")
                            return qsTr("Not connected")
                        }
                        active: NetworkStatus.available && NetworkStatus.wifiHardwareEnabled
                        toggleable: NetworkStatus.available
                        isDarkBackground: panelRoot.isDarkBackground
                        foregroundColor: panelRoot.foregroundColor
                        onToggleRequested: NetworkStatus.toggleWifi()
                        onOptionsRequested: SettingsLauncher.openSettings("network")
                    }
                    QuickSettingTile {
                        Layout.fillWidth: true
                        iconName: {
                            if (!BluetoothStatus.available) return "bluetooth-active-symbolic"
                            if (!BluetoothStatus.powered) return "bluetooth-disabled-symbolic"
                            return BluetoothStatus.deviceConnected ? "bluetooth-active-symbolic" : "bluetooth-symbolic"
                        }
                        title: qsTr("Bluetooth")
                        subtitle: {
                            if (!BluetoothStatus.available) return qsTr("Unavailable")
                            if (!BluetoothStatus.powered) return qsTr("Off")
                            return BluetoothStatus.deviceConnected
                                ? BluetoothStatus.connectedDeviceName
                                : qsTr("Not connected")
                        }
                        active: BluetoothStatus.available && BluetoothStatus.powered
                        toggleable: BluetoothStatus.available
                        isDarkBackground: panelRoot.isDarkBackground
                        foregroundColor: panelRoot.foregroundColor
                        onToggleRequested: BluetoothStatus.togglePowered()
                        onOptionsRequested: SettingsLauncher.openSettings("bluetooth")
                    }
                    QuickSettingTile {
                        Layout.fillWidth: true
                        iconName: "network-wired-symbolic"
                        title: qsTr("Ethernet")
                        // No on/off switch for a physical cable -- this
                        // tile just reports NetworkManager's Ethernet
                        // device state. toggleable: false below means
                        // clicking the tile body is a no-op, matching
                        // that there's nothing here for the user to
                        // turn on or off.
                        subtitle: NetworkStatus.available
                            ? (NetworkStatus.ethernetConnected ? qsTr("Connected") : qsTr("Cable unplugged"))
                            : qsTr("Unavailable")
                        active: NetworkStatus.available && NetworkStatus.ethernetConnected
                        toggleable: false
                        isDarkBackground: panelRoot.isDarkBackground
                        foregroundColor: panelRoot.foregroundColor
                        onOptionsRequested: SettingsLauncher.openSettings("network")
                    }
                    // "Project" doesn't have a settings-toggle shape like
                    // the other three (no "..." button, no on/off state) --
                    // it's a one-shot action (opens a display-sharing
                    // picker), so it gets the chevron treatment instead.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 76
                        radius: Theme.cornerRadius
                        color: projectMouse.containsMouse
                            ? (panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.14) : Qt.rgba(0, 0, 0, 0.08))
                            : (panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.08) : Qt.rgba(0, 0, 0, 0.04))

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 4

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: qsTr("Project")
                                    color: panelRoot.foregroundColor
                                    font.family: "Inter"
                                    font.pixelSize: 17
                                }
                                Text {
                                    text: qsTr("Share your screen wirelessly")
                                    color: panelRoot.foregroundColor
                                    opacity: 0.65
                                    font.family: "Inter"
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }

                            Text {
                                text: "\u203a" // chevron -- screen-share picker isn't implemented yet
                                color: panelRoot.foregroundColor
                                font.pixelSize: 22
                                font.weight: Font.Bold
                            }
                        }

                        MouseArea {
                            id: projectMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            // No display-portal/screen-share backend
                            // exists yet (see header comment) -- routes
                            // through the same SettingsLauncher
                            // coming-soon path as "All settings.." rather
                            // than a silent console.log, so clicking it
                            // gives the same honest feedback in the UI.
                            onClicked: SettingsLauncher.openSettings("display-sharing")
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    radius: Theme.cornerRadius
                    color: allSettingsMouse.containsMouse
                        ? (panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.18) : Qt.rgba(0, 0, 0, 0.1))
                        : (panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.12) : Qt.rgba(0, 0, 0, 0.06))

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        // Swaps to a brief confirmation after a click
                        // that couldn't actually launch tsettings (see
                        // the Connections/Timer above) -- otherwise
                        // clicking this looks identical whether it
                        // worked or not.
                        text: comingSoonTimer.running
                            ? qsTr("tSettings \u2014 coming soon")
                            : qsTr("All settings..")
                        color: panelRoot.foregroundColor
                        font.family: "Inter"
                        font.pixelSize: 18
                    }

                    MouseArea {
                        id: allSettingsMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: SettingsLauncher.openSettings()
                    }
                }
            }
        }

        // --- Media widget ------------------------------------------------
        // Backed by MediaPlayerModel (MPRIS over D-Bus, see
        // src/common/MediaPlayerModel.h). Shows the same honest
        // "No app selected for media play" / "No song playing" empty
        // state when MediaPlayer.available is false (no MPRIS player
        // currently running), and real title/artist plus working
        // play/pause/skip buttons once one is.
        Rectangle {
            Layout.preferredWidth: 420
            Layout.preferredHeight: 140
            radius: Theme.cornerRadius
            color: panelRoot.isDarkBackground ? Qt.rgba(45, 45, 45, 0.85) : Qt.rgba(235, 235, 235, 0.9)
            border.color: panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.1) : Qt.rgba(0, 0, 0, 0.06)
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 8

                Text {
                    text: MediaPlayer.available
                        ? MediaPlayer.playerName
                        : qsTr("No app selected for media play")
                    color: panelRoot.foregroundColor
                    opacity: 0.6
                    font.family: "Inter"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 14

                    Rectangle {
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 64
                        Layout.alignment: Qt.AlignVCenter
                        radius: Theme.cornerRadius
                        color: panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.08) : Qt.rgba(0, 0, 0, 0.06)

                        Image {
                            anchors.centerIn: parent
                            source: "image://icontheme/" + "audio-x-generic-symbolic?tint=" +
                                    panelRoot.foregroundColor.toString().replace("#", "")
                            sourceSize.width: 24
                            sourceSize.height: 24
                            opacity: 0.5
                        }
                    }

                    // Title/artist -- takes over the space the old
                    // static "No song playing" label used, but
                    // left-aligned and two lines once there's real
                    // metadata to show, since a single centered line
                    // stops making sense once artist is added.
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: MediaPlayer.available && MediaPlayer.title.length > 0
                                ? MediaPlayer.title
                                : qsTr("No song playing")
                            color: panelRoot.foregroundColor
                            font.family: "Inter"
                            font.pixelSize: 16
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: MediaPlayer.available && MediaPlayer.artist.length > 0
                            Layout.fillWidth: true
                            text: MediaPlayer.artist
                            color: panelRoot.foregroundColor
                            opacity: 0.6
                            font.family: "Inter"
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }

                    // Transport controls -- only meaningful (and only
                    // shown) once a real MPRIS player is actually
                    // present; there's nothing to play/pause/skip in
                    // the empty state, and showing dead buttons there
                    // would be the same kind of fake-affordance this
                    // whole pass is trying to get rid of.
                    RowLayout {
                        visible: MediaPlayer.available
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 4

                        Rectangle {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: Theme.cornerRadius
                            color: prevMouse.containsMouse
                                ? (panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.14) : Qt.rgba(0, 0, 0, 0.08))
                                : "transparent"
                            opacity: MediaPlayer.canGoPrevious ? 1.0 : 0.35

                            Image {
                                anchors.centerIn: parent
                                source: "image://icontheme/media-skip-backward-symbolic?tint=" +
                                        panelRoot.foregroundColor.toString().replace("#", "")
                                sourceSize.width: 16
                                sourceSize.height: 16
                            }
                            MouseArea {
                                id: prevMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: MediaPlayer.canGoPrevious
                                onClicked: MediaPlayer.previous()
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: Theme.cornerRadius
                            color: panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.18) : Qt.rgba(0, 0, 0, 0.12)

                            Image {
                                anchors.centerIn: parent
                                source: "image://icontheme/" +
                                        (MediaPlayer.playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic") +
                                        "?tint=" + panelRoot.foregroundColor.toString().replace("#", "")
                                sourceSize.width: 18
                                sourceSize.height: 18
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: MediaPlayer.playPause()
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: Theme.cornerRadius
                            color: nextMouse.containsMouse
                                ? (panelRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.14) : Qt.rgba(0, 0, 0, 0.08))
                                : "transparent"
                            opacity: MediaPlayer.canGoNext ? 1.0 : 0.35

                            Image {
                                anchors.centerIn: parent
                                source: "image://icontheme/media-skip-forward-symbolic?tint=" +
                                        panelRoot.foregroundColor.toString().replace("#", "")
                                sourceSize.width: 16
                                sourceSize.height: 16
                            }
                            MouseArea {
                                id: nextMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: MediaPlayer.canGoNext
                                onClicked: MediaPlayer.next()
                            }
                        }
                    }
                }
            }
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: panelRoot.isOpen = false
    }
}
