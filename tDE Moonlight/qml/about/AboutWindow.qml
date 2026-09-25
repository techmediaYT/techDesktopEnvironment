import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import TDEMoonlight.Theme
import TDEMoonlight.Decoration

// Matches tDE-Moonlight-Interface-AppBar.png: dark window, standard tDE
// titlebar ("About tDE...", minimize/maximize/close), and a purple-to-
// magenta gradient body with the product name and developer credit
// centered on it.
Window {
    id: aboutRoot
    width: 900
    height: 560
    minimumWidth: 480
    minimumHeight: 320
    color: "#000000"
    title: qsTr("About tDE...")
    flags: Qt.Window | Qt.FramelessWindowHint

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            id: titleBar
            Layout.fillWidth: true
            title: aboutRoot.title
            targetWindow: aboutRoot
            backgroundColor: aboutRoot.color
        }

        // Gradient body -- colors sampled from the mockup (purple on
        // the left grading to magenta/pink on the right).
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            radius: Theme.cornerRadius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#9b1fd6" }
                GradientStop { position: 0.55; color: "#c92be0" }
                GradientStop { position: 1.0; color: "#e055e8" }
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("techDesktopEnvironment")
                    color: "#1a1a1a"
                    font.family: "Inter"
                    font.pixelSize: 42
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Developed by tCorporation")
                    color: "#1a1a1a"
                    opacity: 0.75
                    font.family: "Inter"
                    font.pixelSize: 20
                }

                // Real build metadata, from CMake-injected compile
                // definitions (see src/about-tde/main.cpp) -- not shown
                // in the original mockup, added below the credit line
                // since "version info, build release, etc" was
                // explicitly asked for.
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 24
                    text: qsTr("Version %1").arg(tdeVersionString)
                    color: "#1a1a1a"
                    opacity: 0.6
                    font.family: "Inter"
                    font.pixelSize: 14
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Build %1").arg(tdeBuildInfo)
                    color: "#1a1a1a"
                    opacity: 0.6
                    font.family: "Inter"
                    font.pixelSize: 14
                }
            }
        }
    }
}
