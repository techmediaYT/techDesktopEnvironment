import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import TDEMoonlight.TopBar
import TDEMoonlight.Theme

// Root window. Actual size/position/layer-shell anchoring is set from
// main.cpp once this window exists -- don't hardcode geometry here.
Window {
    id: root
    // Deliberately NOT visible:true here. If Qt shows the window before
    // our C++ code (main.cpp) has converted it into a layer-shell
    // surface, it gets assigned a normal xdg_toplevel role first --
    // then the later layer-shell conversion conflicts with that role
    // and Wayland kills the connection with a protocol error. main.cpp
    // calls window->show() itself, after layer-shell setup is done.

    // Must be transparent, or the acrylic effect (and the rounded pill
    // shapes sitting on top of it) will show square black edges.
    color: "transparent"
    flags: Qt.FramelessWindowHint

    // --- Panel background ---------------------------------------------
    // No full-width bar background anymore -- the panel is fully
    // transparent, with the clock and notif pills as independently
    // shaped floating elements. isDarkBackground/foregroundColor are
    // still kept: they drive the pills' and status icons' text/icon
    // color so everything still adapts to a light or dark desktop.
    // Defaulting to dark since the desktop has no wallpaper set yet.
    // Real background sampling when available (see
    // src/common/BackgroundSampler.h) -- BackgroundState is always
    // registered regardless of build configuration, and simply keeps
    // its constructed default (true/dark) forever on a compositor that
    // doesn't implement ext-image-copy-capture-v1, or when
    // TDE_ENABLE_BACKGROUND_SAMPLING was off at build time -- there is
    // no separate "active" check needed here since the state object
    // itself is the single source of truth either way.
    property bool isDarkBackground: BackgroundState.isDarkBackground
    property color foregroundColor: isDarkBackground ? "white" : "#1a1a1a"

    ControlPanel {
        id: controlPanel
        isDarkBackground: root.isDarkBackground
    }

    PowerMenu {
        id: powerMenu
        isDarkBackground: root.isDarkBackground
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        // --- Left cluster: clock pill + notif pill -----------------------
        RowLayout {
            spacing: 20
            Layout.alignment: Qt.AlignVCenter

            NotifPill {
                label: Qt.formatTime(clock.currentTime, "h:mm")
                pillRadius: 28
                fontPixelSize: 18
                textColor: root.foregroundColor
            }

            NotifPill {
                // Placeholder label until the notification center backend
                // exists; this will bind to an actual unread-count/summary
                // model later.
                label: "No new notifications"
                pillRadius: 38
                textColor: root.foregroundColor
            }
        }

        Item { Layout.fillWidth: true } // pushes status cluster to the right

        // --- Right cluster: status pill + separate power button -----------
        // Wifi/bluetooth/battery/volume/overview live inside their own
        // translucent rounded pill, matching the same frosted treatment
        // as the clock and notif pills on the left -- the panel window
        // itself already gets real compositor blur where supported (see
        // PanelBlur.h / TDE_ENABLE_BLUR), this pill's translucent fill
        // just sits on top of that. The power button is deliberately its
        // own separate element, off to the side of the pill, not inside it.
        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 14

            Rectangle {
                id: statusPill
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: statusRow.implicitWidth + 28
                implicitHeight: 40
                radius: Theme.cornerRadius
                color: "#3a3a3a"
                opacity: 0.85

                // Clicking anywhere on the pill OTHER than the volume
                // and overview icons' own hit areas opens the Control
                // Panel, matching how every other desktop's quick-
                // settings pill/tray behaves. Declared BEFORE statusRow
                // (not after) so it sits underneath the icons in paint/
                // hit-test order -- otherwise, as a later sibling on
                // top of the whole pill, it would intercept every click
                // before the volume/overview icons' own inner
                // MouseAreas ever saw them, since Qt Quick delivers
                // events to the topmost item first.
                MouseArea {
                    anchors.fill: parent
                    onClicked: controlPanel.isOpen = !controlPanel.isOpen
                }

                RowLayout {
                    id: statusRow
                    anchors.centerIn: parent
                    spacing: 14

                    // --- Network icon: one slot, three possible states ---
                    // Ethernet takes priority over Wi-Fi when both are
                    // connected -- a wired connection is the "real" one
                    // the user cares about, and showing both a wifi and
                    // an eth glyph at once is exactly the kind of status
                    // clutter a single quick-settings pill shouldn't have.
                    // "Off"/no-icon-available states resolve through
                    // StatusIcon's own IconChecker.hasIcon() guard -- if
                    // the installed theme has no dedicated disabled
                    // variant for whichever glyph applies, this shows no
                    // icon at all rather than a misleading generic one.
                    StatusIcon {
                        iconName: {
                            if (NetworkStatus.ethernetConnected) {
                                return "network-wired-symbolic"
                            }
                            if (!NetworkStatus.available) {
                                return "network-wireless-symbolic"
                            }
                            if (!NetworkStatus.wifiHardwareEnabled) {
                                return "network-wireless-disabled-symbolic"
                            }
                            if (NetworkStatus.wifiConnected) {
                                return "network-wireless-signal-good-symbolic"
                            }
                            return "network-wireless-no-route-symbolic"
                        }
                        textColor: root.foregroundColor
                    }

                    StatusIcon {
                        iconName: {
                            if (!BluetoothStatus.available) {
                                return "bluetooth-active-symbolic"
                            }
                            if (!BluetoothStatus.powered) {
                                return "bluetooth-disabled-symbolic"
                            }
                            return BluetoothStatus.deviceConnected
                                ? "bluetooth-active-symbolic"
                                : "bluetooth-symbolic"
                        }
                        textColor: root.foregroundColor
                    }
                    StatusIcon {
                        visible: BatteryMonitor.present
                        iconName: BatteryMonitor.iconName
                        label: BatteryMonitor.percentage + "%"
                        textColor: root.foregroundColor
                    }

                    // Volume icon reflects real mute/level state via
                    // VolumeModel (wpctl/PipeWire, see
                    // src/common/VolumeModel.h). Icon threshold names
                    // follow Adwaita's own convention for this exact
                    // control (audio-volume-{muted,low,medium,high}) --
                    // StatusIcon's own IconChecker guard means if a
                    // theme is somehow missing one of these (very
                    // unlikely for Adwaita, all four ship together),
                    // it simply won't show rather than showing the
                    // wrong level.
                    StatusIcon {
                        iconName: {
                            if (!Volume.available) return "audio-volume-high-symbolic"
                            if (Volume.muted || Volume.volumePercent === 0) return "audio-volume-muted-symbolic"
                            if (Volume.volumePercent < 34) return "audio-volume-low-symbolic"
                            if (Volume.volumePercent < 67) return "audio-volume-medium-symbolic"
                            return "audio-volume-high-symbolic"
                        }
                        textColor: root.foregroundColor

                        MouseArea {
                            anchors.fill: parent
                            enabled: Volume.available
                            acceptedButtons: Qt.LeftButton
                            // Click toggles mute -- the single most
                            // common quick action on a status-bar volume
                            // icon across every desktop. Fine-grained
                            // level adjustment (a slider) belongs in the
                            // Control Panel, not a one-click status icon.
                            onClicked: Volume.toggleMute()
                            // Scroll to step volume up/down without
                            // opening anything -- same interaction every
                            // major desktop's volume tray icon supports.
                            onWheel: (wheel) => {
                                if (wheel.angleDelta.y > 0) {
                                    Volume.increaseVolume()
                                } else if (wheel.angleDelta.y < 0) {
                                    Volume.decreaseVolume()
                                }
                            }
                        }
                    }

                    // Overview/app-switcher icon. No standalone window-
                    // switcher feature exists in this codebase (see
                    // DockRemote.h) -- this opens the same app menu the
                    // dock's own grid-icon button opens, via the
                    // cross-process D-Bus call DockController already
                    // exposes for compositor keybindings.
                    StatusIcon {
                        iconName: "view-app-grid-symbolic"
                        textColor: root.foregroundColor

                        MouseArea {
                            anchors.fill: parent
                            onClicked: DockRemote.toggleAppMenu()
                        }
                    }
                }

            }

            StatusIcon {
                Layout.alignment: Qt.AlignVCenter
                iconName: "system-shutdown-symbolic"
                textColor: root.foregroundColor

                MouseArea {
                    anchors.fill: parent
                    onClicked: powerMenu.isOpen = !powerMenu.isOpen
                }
            }
        }
    }

    // Simple ticking clock. Swap for a proper Date-change-driven timer if
    // second-level precision or timezone changes need to be handled later.
    Timer {
        id: clock
        property date currentTime: new Date()
        interval: 1000
        running: true
        repeat: true
        onTriggered: currentTime = new Date()
    }
}
