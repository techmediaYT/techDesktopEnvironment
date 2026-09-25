import QtQuick
import QtQuick.Controls
import TDEMoonlight.Theme

// One pinned-app icon in the dock. App icons (unlike the symbolic
// status-bar icons in StatusIcon.qml) are normally full-color to begin
// with, so no recoloring/MultiEffect tinting is applied here -- just
// rendered as-is.
Item {
    id: dockItem

    property string iconSource: ""
    property string appName: ""
    // See AppModel.h's m_runningPids comment -- this reflects "we
    // launched it and its process is still alive", not real
    // window-open tracking.
    property bool appRunning: false
    // Set by Dock.qml to the capsule's actual height so icons scale
    // with the dock instead of staying a fixed 48px regardless of it
    // (previously hardcoded, so icons didn't fill the dock -- per the
    // request, icons should match the dock's height minus a small
    // margin, not sit at an arbitrary fixed size).
    property real dockHeight: 76
    // How much smaller than the full dock height the icon sits --
    // "leave a couple of pixels" per the request, not flush edge to
    // edge (which would look cramped against the capsule's own
    // padding and the running-indicator dot below it).
    readonly property real margin: 12

    signal clicked()

    implicitWidth: dockHeight - margin
    implicitHeight: dockHeight - margin

    Button {
        anchors.fill: parent
        flat: true
        hoverEnabled: true

        background: Rectangle {
            color: parent.hovered ? Qt.rgba(255, 255, 255, 0.10) : "transparent"
            radius: Theme.cornerRadius
        }

        contentItem: Image {
            source: dockItem.iconSource
            // Sized to the icon's own bounds (fillMode.PreserveAspectFit
            // below keeps it square/undistorted) rather than a fixed
            // 32x32 -- so the icon actually grows/shrinks with the dock
            // instead of floating small inside a bigger button hit area.
            sourceSize.width: dockItem.width
            sourceSize.height: dockItem.height
            fillMode: Image.PreserveAspectFit
            anchors.centerIn: parent
            asynchronous: true
        }

        onClicked: dockItem.clicked()

        ToolTip.visible: hovered
        ToolTip.text: dockItem.appName
        ToolTip.delay: 500
    }

    // Small running-app indicator, positioned for a bottom-anchored
    // horizontal dock (a dot beneath the icon, matching where
    // dash-to-dock-style docks put it for a bottom bar -- previously
    // this sat on the icon's left edge, correct only for the old
    // left-edge vertical strip layout).
    Rectangle {
        visible: dockItem.appRunning
        width: 5
        height: 5
        radius: 2.5
        color: "white"
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: -4
    }
}
