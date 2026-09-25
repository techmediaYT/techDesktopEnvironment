import QtQuick
import TDEMoonlight.Theme

// Generic rounded pill, used for both the clock and the notification
// pill in the top bar. Despite the "pill" name/history, per the actual
// mockup (Affinity's own "9%" corner-radius readout on these shapes)
// this is a modest, mostly-square corner -- not the fully-rounded
// stadium shape the name and the old default (height/2) implied.
Rectangle {
    id: pill

    property alias label: pillText.text
    property color textColor: "white"
    property color pillColor: "#3a3a3a"
    property real pillRadius: Theme.cornerRadius
    property real fontPixelSize: 15

    readonly property int horizontalPadding: 18

    implicitWidth: pillText.implicitWidth + horizontalPadding * 2
    implicitHeight: 40
    radius: pillRadius
    color: pillColor
    opacity: 0.85

    Text {
        id: pillText
        anchors.centerIn: parent
        color: pill.textColor
        font.pixelSize: pill.fontPixelSize
        font.weight: Font.Medium
        font.family: "Inter"
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            // Will open the notification center popup once that surface
            // exists. Left as a no-op hook for now.
        }
    }
}
