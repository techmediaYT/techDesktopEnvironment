import QtQuick
import QtQuick.Layouts
import TDEMoonlight.Theme

// One button in PowerMenu.qml's grid -- icon left of label, inside a
// wide rounded rectangle, matching the mockup exactly (unlike the
// icon-above-label style used elsewhere in this project, this menu's
// buttons put them side by side in a single row).
Item {
    id: button

    property string iconName: ""
    property string label: ""
    property color foregroundColor: "white"
    property bool isDarkBackground: true

    signal clicked()

    Layout.fillWidth: true
    Layout.preferredHeight: 64
    // Also set directly (not just via the Layout attached property)
    // since PowerMenu.qml's log-out-view Cancel button is positioned
    // with anchors rather than as a Layout child, where
    // Layout.preferredHeight has no effect.
    implicitHeight: 64

    Rectangle {
        anchors.fill: parent
        radius: Theme.cornerRadius
        color: buttonMouse.containsMouse
            ? (button.isDarkBackground ? Qt.rgba(255, 255, 255, 0.22) : Qt.rgba(0, 0, 0, 0.14))
            : (button.isDarkBackground ? Qt.rgba(255, 255, 255, 0.14) : Qt.rgba(0, 0, 0, 0.08))

        Behavior on color { ColorAnimation { duration: 100 } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 14

            Image {
                source: "image://icontheme/" + button.iconName + "?tint=" +
                        button.foregroundColor.toString().replace("#", "")
                sourceSize.width: 24
                sourceSize.height: 24
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: button.label
                color: button.foregroundColor
                font.family: "Inter"
                font.pixelSize: 20
            }
        }

        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: button.clicked()
        }
    }
}
