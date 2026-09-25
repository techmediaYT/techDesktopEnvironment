import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import TDEMoonlight.Dock
import TDEMoonlight.Theme

Window {
    id: menuRoot

    // This was the actual reason the menu never appeared: isOpen only
    // drove the inner Rectangle's opacity/position animation below --
    // nothing ever bound the *window's own* visible property, so the
    // OS-level window itself stayed hidden regardless of what the
    // animation was doing internally.
    visible: isOpen

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    property bool isOpen: false
    property bool isDarkBackground: false
    property color foregroundColor: isDarkBackground ? "white" : "#1a1a1a"

    // Runs while isOpen (and therefore visible) is still false, so this
    // happens before the window is ever mapped -- required ordering,
    // same as main.cpp's own layer-shell setup for the topbar/dock.
    // Without this the window is a plain xdg_toplevel: Sway decorates
    // it with a title bar and tiles it into the layout like any normal
    // app window, which is exactly the bug this fixes.
    Component.onCompleted: LayerShellHelper.configureOverlay(menuRoot, "tde-moonlight-appmenu")

    // Full-window scrim, behind menuCanvas. This is what makes the menu
    // dismissable by clicking anywhere outside the card -- the overlay
    // window spans the whole screen (see LayerShellHelper's anchors), so
    // without this there was no way to close the menu except Escape.
    //
    // onVisibleChanged (not Component.onCompleted/isOpen directly) is
    // what guards the click-through bug: the same physical click that
    // opens the menu (dock icon -> globalAppMenu.isOpen = true) also
    // lands, same event loop turn, on whatever is now underneath the
    // pointer -- which, the instant this window becomes visible, is this
    // scrim. Without ignoreNextRelease, that single click would open and
    // immediately close the menu, which reads as "the menu doesn't open
    // any more" even though isOpen did briefly flip true.
    MouseArea {
        id: scrim
        anchors.fill: parent
        enabled: menuRoot.isOpen
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
            menuRoot.isOpen = false
            searchInput.text = ""
        }
    }

    onVisibleChanged: {
        if (visible) {
            scrim.ignoreNextRelease = true
        }
    }

    Rectangle {
        id: menuCanvas
        anchors.verticalCenter: parent.verticalCenter
        width: 720
        height: 840
        // Corner radius comes from the shared Theme singleton (see
        // qml/theme/Theme.qml) -- applied consistently below to the
        // search bar, tiles, and popup too, not just this outer card.
        radius: Theme.cornerRadius
        color: menuRoot.isDarkBackground ? Qt.rgba(30, 30, 30, 0.65) : Qt.rgba(240, 240, 240, 0.75)
        border.color: menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.12) : Qt.rgba(0, 0, 0, 0.06)
        border.width: 1
        opacity: menuRoot.isOpen ? 1.0 : 0.0

        // Deliberately not using anchors.right here -- anchoring and
        // directly animating x on the same axis conflict (whichever is
        // assigned last silently wins over the other), which breaks the
        // slide animation. Computing x directly gets the same "flush to
        // the right edge, with a small margin" position without that.
        readonly property int rightMargin: 16
        x: menuRoot.isOpen ? parent.width - width - rightMargin
                            : parent.width - width - rightMargin + 24

        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        Behavior on x { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

        // Swallows clicks on empty card padding so they don't fall
        // through to the full-window scrim behind menuCanvas and close
        // the menu while the user is still clicking inside it.
        MouseArea {
            anchors.fill: parent
            onClicked: {} // consume, do nothing
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 32
            anchors.bottomMargin: 32
            anchors.leftMargin: 40
            anchors.rightMargin: 40
            spacing: 28

            Rectangle {
                id: searchBarContainer
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                radius: Theme.cornerRadius
                color: menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.08) : Qt.rgba(0, 0, 0, 0.05)
                border.color: menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.08) : Qt.rgba(0, 0, 0, 0.03)
                border.width: 1

                TextInput {
                    id: searchInput
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    verticalAlignment: TextInput.AlignVCenter
                    font.family: "Inter"
                    font.pixelSize: 16
                    color: menuRoot.foregroundColor
                    focus: menuRoot.isOpen

                    Text {
                        text: qsTr("Type to search")
                        color: menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.4) : Qt.rgba(0, 0, 0, 0.4)
                        font: parent.font
                        visible: parent.text === ""
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridView {
                    id: appGrid
                    anchors.fill: parent
                    anchors.rightMargin: 20
                    clip: true
                    model: AppSystemModel
                    cellWidth: 140
                    cellHeight: 145

                    // Correct way to attach a scrollbar to a Flickable-
                    // derived view (GridView included) is the
                    // ScrollBar.vertical attached property, not a
                    // standalone ScrollBar with a "target" property --
                    // that property doesn't exist on ScrollBar and was
                    // the actual cause of this component failing to load.
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle {
                            implicitWidth: 6
                            radius: 3
                            color: menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.4) : Qt.rgba(0, 0, 0, 0.25)
                        }
                        background: Rectangle { color: "transparent" }
                    }

                    delegate: Item {
                        width: appGrid.cellWidth
                        height: appGrid.cellHeight
                        visible: appName.toLowerCase().includes(searchInput.text.toLowerCase())

                        Rectangle {
                            id: tileContainer
                            width: 120
                            height: 125
                            anchors.centerIn: parent
                            radius: Theme.cornerRadius
                            color: tileMouse.containsMouse
                                ? (menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.10) : Qt.rgba(0, 0, 0, 0.06))
                                : "transparent"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                Item {
                                    id: iconWrapper
                                    Layout.preferredWidth: 64
                                    Layout.preferredHeight: 64
                                    Layout.alignment: Qt.AlignHCenter

                                    Image {
                                        anchors.fill: parent
                                        source: "image://icontheme/" + appIcon
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                    }

                                    // Options trigger -- opens the full action
                                    // menu (Pin, Open, More Info, Uninstall).
                                    Rectangle {
                                        id: optionsTrigger
                                        width: 22
                                        height: 22
                                        radius: 11
                                        anchors.top: parent.top
                                        anchors.right: parent.right
                                        anchors.topMargin: -4
                                        anchors.rightMargin: -4
                                        color: optionsTriggerMouse.containsMouse
                                            ? (menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.25) : Qt.rgba(0, 0, 0, 0.15))
                                            : (menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.12) : Qt.rgba(0, 0, 0, 0.06))
                                        border.color: menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.1) : Qt.rgba(0, 0, 0, 0.04)
                                        border.width: 1

                                        Text {
                                            text: "\u22ee"
                                            anchors.centerIn: parent
                                            color: menuRoot.foregroundColor
                                            font.pixelSize: 13
                                            font.weight: Font.Bold
                                            anchors.verticalCenterOffset: -1
                                        }

                                        MouseArea {
                                            id: optionsTriggerMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            propagateComposedEvents: false
                                            onClicked: (mouse) => {
                                                mouse.accepted = true
                                                optionsDropdown.popup(optionsTrigger, 0, optionsTrigger.height + 4)
                                            }
                                        }
                                    }
                                }

                                Text {
                                    text: appName
                                    color: menuRoot.foregroundColor
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    font.family: "Inter"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                }
                            }

                            MouseArea {
                                id: tileMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.LeftButton) {
                                        AppSystemModel.launchApplication(appId)
                                        menuRoot.isOpen = false
                                        searchInput.text = ""
                                    } else if (mouse.button === Qt.RightButton) {
                                        optionsDropdown.popup(tileContainer, mouse.x, mouse.y)
                                    }
                                }
                            }

                            Menu {
                                id: optionsDropdown

                                MenuItem {
                                    text: appPinned ? qsTr("Unpin") : qsTr("Pin")
                                    font.weight: Font.Medium
                                    onTriggered: AppSystemModel.togglePinState(appId)
                                }
                                MenuItem {
                                    text: qsTr("Open")
                                    onTriggered: {
                                        AppSystemModel.launchApplication(appId)
                                        menuRoot.isOpen = false
                                        searchInput.text = ""
                                    }
                                }
                                MenuItem {
                                    text: qsTr("More Info")
                                    onTriggered: infoPopup.open()
                                }
                                MenuSeparator {}
                                MenuItem {
                                    // Uninstall isn't implemented yet -- see
                                    // DesktopDaemon::triggerUninstall for why
                                    // (needs a safe, allowlisted mechanism,
                                    // not a bare shell-out with UI-supplied
                                    // input). This currently just shows a
                                    // console message rather than pretending
                                    // to work.
                                    contentItem: Text {
                                        text: qsTr("Uninstall")
                                        color: "#e63946"
                                        font.family: "Inter"
                                        font.pixelSize: 13
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    onTriggered: console.log("Uninstall not yet implemented for:", appId)
                                }
                            }

                            // "More Info" details popup -- shows the raw metadata
                            // read from this app's .desktop entry. Rendered via
                            // Popup's own overlay layer, so it isn't clipped by
                            // the GridView's clip:true like a plain child Item
                            // would be.
                            Popup {
                                id: infoPopup
                                modal: true
                                focus: true
                                anchors.centerIn: Overlay.overlay
                                width: 320
                                padding: 24

                                background: Rectangle {
                                    radius: Theme.cornerRadius
                                    color: menuRoot.isDarkBackground ? "#2b2b2b" : "#f5f5f5"
                                    border.color: menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.12) : Qt.rgba(0, 0, 0, 0.08)
                                    border.width: 1
                                }

                                contentItem: ColumnLayout {
                                    spacing: 14

                                    RowLayout {
                                        spacing: 12
                                        Image {
                                            Layout.preferredWidth: 40
                                            Layout.preferredHeight: 40
                                            source: "image://icontheme/" + appIcon
                                            fillMode: Image.PreserveAspectFit
                                        }
                                        Text {
                                            text: appName
                                            color: menuRoot.foregroundColor
                                            font.family: "Inter"
                                            font.pixelSize: 17
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 1
                                        color: menuRoot.isDarkBackground ? Qt.rgba(255, 255, 255, 0.1) : Qt.rgba(0, 0, 0, 0.08)
                                    }

                                    ColumnLayout {
                                        spacing: 6
                                        Layout.fillWidth: true

                                        Text {
                                            text: qsTr("Application ID: ") + appId
                                            color: menuRoot.foregroundColor
                                            font.family: "Inter"
                                            font.pixelSize: 13
                                            wrapMode: Text.Wrap
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: qsTr("Command: ") + appExec
                                            color: menuRoot.foregroundColor
                                            font.family: "Inter"
                                            font.pixelSize: 13
                                            wrapMode: Text.Wrap
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: qsTr("Pinned to dock: ") + (appPinned ? qsTr("Yes") : qsTr("No"))
                                            color: menuRoot.foregroundColor
                                            font.family: "Inter"
                                            font.pixelSize: 13
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

            }
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            menuRoot.isOpen = false
            searchInput.text = ""
        }
    }
}
