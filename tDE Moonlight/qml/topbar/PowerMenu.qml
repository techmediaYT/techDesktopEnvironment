import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import TDEMoonlight.TopBar
import TDEMoonlight.Theme

// Power menu opened by clicking the shutdown icon in the topbar.
// Matches the tDE-Moonlight-Interface-ShutdownMenu.png /
// tDE-Moonlight-Interface-SwitchUser.png mockups: a centered card
// ("What do you want to do?") over a dimmed, blurred whole screen,
// with a 2-column grid of icon-over-label buttons. Two views:
//   - main grid: Shutdown / Restart / Suspend / Hibernate / Log out / Cancel
//   - log-out sub-menu (opened by the main grid's "Log out" button):
//     Log out / Switch users / Cancel, where Cancel returns to the main
//     grid rather than closing the whole thing -- the mockups show this
//     as a fade-and-move-to-center transition between the two, not a
//     confirmation dialog stacked on top.
//
// Backed by PowerManager (org.freedesktop.login1 / systemd-logind, see
// src/common/PowerManager.h). Each action button only appears when
// PowerManager reports it's actually possible on this system (e.g. no
// Suspend button on hardware/config that can't suspend) -- same "don't
// show a control for something that can't work" reasoning as the
// Ethernet tile in ControlPanel.qml.
//
// "Switch users" has no backend at all yet -- there's no multi-seat/
// greeter integration anywhere in this codebase (logind sessions exist
// per-user, but actually switching to another user's session needs a
// display manager or greeter to hand off to, which tDE Moonlight
// doesn't have). Its button is present (matching the mockup) but wired
// to the same honest "not implemented yet" path SettingsLauncher uses,
// rather than silently doing nothing.
Window {
    id: powerMenuRoot

    visible: isOpen
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    property bool isOpen: false
    property bool isDarkBackground: true
    property color foregroundColor: isDarkBackground ? "white" : "#1a1a1a"

    // false = main grid, true = the Log out / Switch users / Cancel
    // sub-menu. Separate from isOpen so the sub-menu's own transition
    // can be driven by this flipping, not by the whole window opening.
    property bool showingLogoutMenu: false

    Component.onCompleted: LayerShellHelper.configureOverlay(powerMenuRoot, "tde-moonlight-powermenu")

    function resetAndClose() {
        powerMenuRoot.isOpen = false
        powerMenuRoot.showingLogoutMenu = false
    }

    function runAction(action) {
        switch (action) {
        case "poweroff": PowerManager.powerOff(); break
        case "reboot": PowerManager.reboot(); break
        case "suspend": PowerManager.suspend(); break
        case "hibernate": PowerManager.hibernate(); break
        case "logout": PowerManager.logOut(); break
        }
        resetAndClose()
    }

    // Full-window scrim -- unlike AppMenu.qml/ControlPanel.qml's
    // invisible click-catcher-only scrim, this one is also the visible
    // dim the mockups show behind the card (the actual blur comes from
    // the compositor via LayerShellHelper.configureOverlay above,
    // applied to this whole surface; this rectangle is the darkening
    // layered on top of that blur, which the mockups clearly show as
    // well). Same same-frame click-through guard as the other two
    // overlays -- see AppMenu.qml's comment for the detailed reasoning.
    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.35)
        opacity: powerMenuRoot.isOpen ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }

        MouseArea {
            id: scrimMouse
            anchors.fill: parent
            enabled: powerMenuRoot.isOpen
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
                resetAndClose()
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            scrimMouse.ignoreNextRelease = true
        } else {
            showingLogoutMenu = false
        }
    }

    // Centered card -- both the main grid and the log-out sub-menu live
    // here, cross-faded via the fixed-height Item below (not two
    // ColumnLayout siblings, which would reserve space for both at
    // once and overlap mid-transition), so the card itself can smoothly
    // resize between the grid's wider layout and the sub-menu's
    // narrower one (matching the mockups' different card widths)
    // instead of one abruptly replacing the other.
    Rectangle {
        id: menuCard
        anchors.centerIn: parent
        radius: Theme.cornerRadius
        color: powerMenuRoot.isDarkBackground ? Qt.rgba(60, 60, 60, 0.82) : Qt.rgba(235, 235, 235, 0.9)
        border.color: powerMenuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.1) : Qt.rgba(0, 0, 0, 0.06)
        border.width: 1

        opacity: powerMenuRoot.isOpen ? 1 : 0
        scale: powerMenuRoot.isOpen ? 1 : 0.94
        width: powerMenuRoot.showingLogoutMenu ? 420 : 560
        height: cardStack.implicitHeight + 48

        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }
        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        // Swallows clicks on card padding -- same reasoning as
        // menuCanvas/settingsCard's equivalent MouseArea in the other
        // two overlays.
        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        ColumnLayout {
            id: cardStack
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 20

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("What do you want to do?")
                color: powerMenuRoot.foregroundColor
                font.family: "Inter"
                font.pixelSize: 26
            }

            // Fixed-height container holding both views stacked on top
            // of each other (anchors.fill, not ColumnLayout siblings) --
            // this is what makes the crossfade clean: ColumnLayout would
            // reserve space for both simultaneously and they'd visibly
            // overlap mid-transition, since both are still "in the
            // layout" during the opacity animation, not swapped
            // instantly. Height is driven by whichever view is
            // currently the target (see menuCard.height above using
            // cardStack.implicitHeight, which now reflects this
            // Item's own implicitHeight instead).
            Item {
                Layout.fillWidth: true
                implicitHeight: powerMenuRoot.showingLogoutMenu
                    ? (logoutRow.implicitHeight + 16 + logoutCancelButton.implicitHeight)
                    : mainGrid.implicitHeight
                Behavior on implicitHeight { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

                // --- Main grid: Shutdown / Restart / Suspend / Hibernate /
                //     Log out / Cancel -----------------------------------
                GridLayout {
                    id: mainGrid
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    visible: opacity > 0
                    opacity: powerMenuRoot.showingLogoutMenu ? 0 : 1
                    Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutQuad } }

                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 16

                    PowerMenuButton {
                        visible: PowerManager.canPowerOff
                        iconName: "system-shutdown-symbolic"
                        label: qsTr("Shutdown")
                        foregroundColor: powerMenuRoot.foregroundColor
                        isDarkBackground: powerMenuRoot.isDarkBackground
                        onClicked: runAction("poweroff")
                    }
                    PowerMenuButton {
                        visible: PowerManager.canReboot
                        iconName: "system-reboot-symbolic"
                        label: qsTr("Restart")
                        foregroundColor: powerMenuRoot.foregroundColor
                        isDarkBackground: powerMenuRoot.isDarkBackground
                        onClicked: runAction("reboot")
                    }
                    PowerMenuButton {
                        visible: PowerManager.canSuspend
                        iconName: "system-suspend-symbolic"
                        label: qsTr("Suspend")
                        foregroundColor: powerMenuRoot.foregroundColor
                        isDarkBackground: powerMenuRoot.isDarkBackground
                        onClicked: runAction("suspend")
                    }
                    PowerMenuButton {
                        visible: PowerManager.canHibernate
                        iconName: "system-hibernate-symbolic"
                        label: qsTr("Hibernate")
                        foregroundColor: powerMenuRoot.foregroundColor
                        isDarkBackground: powerMenuRoot.isDarkBackground
                        onClicked: runAction("hibernate")
                    }
                    PowerMenuButton {
                        // Always shown -- Session.Terminate doesn't
                        // depend on the same polkit/hardware capability
                        // checks as the actions above (see
                        // PowerManager.h).
                        iconName: "system-log-out-symbolic"
                        label: qsTr("Log out")
                        foregroundColor: powerMenuRoot.foregroundColor
                        isDarkBackground: powerMenuRoot.isDarkBackground
                        // Opens the sub-menu rather than logging out
                        // directly -- matches the 2nd mockup, reached
                        // from this exact button.
                        onClicked: powerMenuRoot.showingLogoutMenu = true
                    }
                    PowerMenuButton {
                        iconName: "process-stop-symbolic"
                        label: qsTr("Cancel")
                        foregroundColor: powerMenuRoot.foregroundColor
                        isDarkBackground: powerMenuRoot.isDarkBackground
                        onClicked: resetAndClose()
                    }

                    Text {
                        visible: !PowerManager.available
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Power actions unavailable \u2014 could not reach systemd-logind.")
                        color: powerMenuRoot.foregroundColor
                        opacity: 0.5
                        font.family: "Inter"
                        font.pixelSize: 11
                    }
                }

                // --- Log-out sub-menu: Log out / Switch users / Cancel --
                // Cancel here returns to the main grid
                // (showingLogoutMenu = false) rather than closing the
                // window -- per the request, matching the 2nd mockup's
                // relationship to the 1st.
                RowLayout {
                    id: logoutRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    visible: opacity > 0
                    opacity: powerMenuRoot.showingLogoutMenu ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutQuad } }

                    spacing: 16

                    PowerMenuButton {
                        Layout.fillWidth: true
                        iconName: "system-log-out-symbolic"
                        label: qsTr("Log out")
                        foregroundColor: powerMenuRoot.foregroundColor
                        isDarkBackground: powerMenuRoot.isDarkBackground
                        onClicked: runAction("logout")
                    }
                    PowerMenuButton {
                        Layout.fillWidth: true
                        iconName: "system-switch-user-symbolic"
                        label: qsTr("Switch users")
                        foregroundColor: powerMenuRoot.foregroundColor
                        isDarkBackground: powerMenuRoot.isDarkBackground
                        // No multi-seat/greeter backend exists to
                        // actually switch users (see file header
                        // comment) -- routes through the same
                        // coming-soon path ControlPanel.qml's Project
                        // row uses, rather than silently doing nothing.
                        onClicked: SettingsLauncher.openSettings("switch-user")
                    }
                }

                // Cancel for the log-out sub-menu sits on its own row
                // below the other two buttons, matching the mockup's
                // layout (Log out + Switch users side by side, Cancel
                // full-width beneath).
                PowerMenuButton {
                    id: logoutCancelButton
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: logoutRow.bottom
                    anchors.topMargin: 16
                    visible: opacity > 0
                    opacity: powerMenuRoot.showingLogoutMenu ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutQuad } }

                    iconName: "process-stop-symbolic"
                    label: qsTr("Cancel")
                    foregroundColor: powerMenuRoot.foregroundColor
                    isDarkBackground: powerMenuRoot.isDarkBackground
                    onClicked: powerMenuRoot.showingLogoutMenu = false
                }
            }
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (powerMenuRoot.showingLogoutMenu) {
                powerMenuRoot.showingLogoutMenu = false
            } else {
                resetAndClose()
            }
        }
    }
}
