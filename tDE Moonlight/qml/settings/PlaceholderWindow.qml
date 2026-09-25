import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import TDEMoonlight.Decoration

// Real placeholder window for the tsettings binary -- shown when
// something (currently only ControlPanel.qml's "All settings.." row,
// via SettingsLauncher) successfully launches tsettings. Most systems
// won't have this binary installed yet, in which case SettingsLauncher
// never gets this far and shows its own in-panel "coming soon" state
// instead -- this window is what appears on the rare system that *does*
// have tsettings built and on PATH.
Window {
    id: root
    width: 720
    height: 480
    minimumWidth: 480
    minimumHeight: 360
    title: qsTr("Settings")
    color: "#1e1e1e"
    flags: Qt.Window | Qt.FramelessWindowHint

    // Passed in via QQmlApplicationEngine::setInitialProperties from
    // main.cpp's --page argument. Empty string means "no specific page
    // requested" -- shown as-is below since no pages exist yet to route
    // to.
    property string requestedPage: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            Layout.fillWidth: true
            title: root.title
            targetWindow: root
            backgroundColor: root.color
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: true
            spacing: 16
            width: Math.min(480, root.width - 80)

        Text {
            text: qsTr("tSettings")
            color: "white"
            font.family: "Inter"
            font.pixelSize: 28
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            text: qsTr("Coming soon.")
            color: Qt.rgba(1, 1, 1, 0.7)
            font.family: "Inter"
            font.pixelSize: 16
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            visible: root.requestedPage.length > 0
            text: qsTr("Requested page: ") + root.requestedPage
            color: Qt.rgba(1, 1, 1, 0.45)
            font.family: "Inter"
            font.pixelSize: 12
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            text: qsTr("Wi-Fi, Bluetooth, displays, sound, and the rest of tDE Moonlight's settings panels aren't built yet. This window exists so \u201cAll settings..\u201d has something real to open, rather than a dead click.")
            color: Qt.rgba(1, 1, 1, 0.55)
            font.family: "Inter"
            font.pixelSize: 13
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Layout.topMargin: 8
        }
        }
    }
}
