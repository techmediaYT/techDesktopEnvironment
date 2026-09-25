import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import TDEMoonlight.Theme

// Client-side titlebar (icon + title + minimize/maximize/close) for
// tDE-aware apps, matching tDE-Moonlight-Interface-AppBar.png.
//
// IMPORTANT SCOPE NOTE: this is client-side decoration (CSD) -- a
// titlebar the *app itself* draws as part of its own window content,
// the same approach GNOME/GTK apps use. It is NOT a compositor-drawn
// server-side decoration, and it cannot appear on windows belonging to
// other applications (a terminal, a browser, etc) unless that specific
// application is rewritten to include this component. There is no
// Wayland compositor anywhere in this codebase (tDE Moonlight is a set
// of layer-shell panel clients -- topbar/dock/app-menu -- that run
// under whatever compositor the user has, e.g. Sway); building a real
// compositor that could draw this titlebar over arbitrary third-party
// windows, or that could take over/replace a compositor like Sway's
// own decoration handling, is a multi-thousand-line undertaking (full
// Wayland protocol implementation, surface/buffer management, input
// routing -- typically built on wlroots or Smithay) well beyond an
// incremental addition to this project. See COMPOSITOR.md at the repo
// root for what that would actually take.
//
// What THIS component gives you now: drop it into the top of any
// QQuickWindow-based tDE app (see qml/settings/PlaceholderWindow.qml
// for the pattern once wired) to get the exact titlebar from the
// mockup, with working minimize/maximize/close against that window.
Rectangle {
    id: titleBar

    property alias title: titleText.text
    // Path to an icon image (e.g. "qrc:/icons/tsettings.png") or an
    // icon-theme name resolvable via "image://icontheme/<name>" --
    // whichever the embedding app already has. Falls back to a plain
    // gradient swatch (matching the mockup's own icon treatment) when
    // empty, rather than showing a broken-image icon.
    property string iconSource: ""
    // The window this titlebar controls. Must be set by whatever
    // embeds this component (there's no way to discover "my own
    // window" generically from inside a plain Item).
    property Window targetWindow: null

    // Titlebar backgrounds vary per app (the mockup uses a plain black/
    // dark window body) -- when the app sets a dark backgroundColor,
    // per the request, text/icons switch to white so they stay
    // readable; otherwise dark text on the assumed lighter default.
    property color backgroundColor: "#000000"
    readonly property bool isDarkBackground: {
        // Standard perceptual luminance check -- matches how
        // TopBar.qml's own isDarkBackground-driven foregroundColor
        // logic works elsewhere in this project, so a titlebar sitting
        // on a black window (like the mockup) and the shell's own
        // topbar arrive at "use white" via the same reasoning rather
        // than two different heuristics.
        const luminance = 0.299 * backgroundColor.r + 0.587 * backgroundColor.g + 0.114 * backgroundColor.b
        return luminance < 0.5
    }
    readonly property color foregroundColor: isDarkBackground ? "white" : "#1a1a1a"

    signal closeRequested()
    signal minimizeRequested()
    signal maximizeRequested()

    implicitHeight: 44
    color: "transparent"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        // App icon -- small swatch matching the mockup's rounded-
        // square gradient icon when no real icon is supplied.
        Rectangle {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            Layout.alignment: Qt.AlignVCenter
            radius: Theme.cornerRadius
            visible: titleBar.iconSource.length === 0
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: "#8b7fd6" }
                GradientStop { position: 1.0; color: "#5a4fc2" }
            }
        }
        Image {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            Layout.alignment: Qt.AlignVCenter
            visible: titleBar.iconSource.length > 0
            source: titleBar.iconSource
            sourceSize.width: 22
            sourceSize.height: 22
        }

        Text {
            id: titleText
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            color: titleBar.foregroundColor
            font.family: "Inter"
            font.pixelSize: 16
            elide: Text.ElideRight
        }

        // Window controls: minimize / maximize / close. Icons are
        // drawn directly (not from an icon theme) since these three
        // glyphs are simple enough to not need a themed asset, and
        // this keeps the component fully self-contained.
        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 18

            Rectangle {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 2
                Layout.alignment: Qt.AlignVCenter
                color: titleBar.foregroundColor
                MouseArea {
                    anchors.margins: -8
                    anchors.fill: parent
                    onClicked: {
                        titleBar.minimizeRequested()
                        if (titleBar.targetWindow) {
                            titleBar.targetWindow.showMinimized()
                        }
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                Layout.alignment: Qt.AlignVCenter
                color: "transparent"
                border.color: titleBar.foregroundColor
                border.width: 2
                radius: 2
                MouseArea {
                    anchors.margins: -6
                    anchors.fill: parent
                    onClicked: {
                        titleBar.maximizeRequested()
                        if (titleBar.targetWindow) {
                            titleBar.targetWindow.visibility = titleBar.targetWindow.visibility === Window.Maximized
                                ? Window.Windowed : Window.Maximized
                        }
                    }
                }
            }
            Text {
                Layout.alignment: Qt.AlignVCenter
                text: "\u2715"
                color: titleBar.foregroundColor
                font.pixelSize: 16
                MouseArea {
                    anchors.margins: -8
                    anchors.fill: parent
                    onClicked: {
                        titleBar.closeRequested()
                        if (titleBar.targetWindow) {
                            titleBar.targetWindow.close()
                        }
                    }
                }
            }
        }
    }

    // Drag-to-move: clicking and dragging empty titlebar space moves
    // the window, standard titlebar behavior. Declared last (topmost
    // in paint/hit-test order among titleBar's own children) but the
    // three control MouseAreas above are on separate child Items with
    // their own smaller hit areas, so this doesn't steal their clicks
    // -- it only ever sees drags that start on genuinely empty
    // titlebar space.
    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: 100 // stay clear of the control cluster
        property point pressPos
        onPressed: (mouse) => { pressPos = Qt.point(mouse.x, mouse.y) }
        onPositionChanged: (mouse) => {
            if (titleBar.targetWindow && pressed) {
                titleBar.targetWindow.x += mouse.x - pressPos.x
                titleBar.targetWindow.y += mouse.y - pressPos.y
            }
        }
        onDoubleClicked: {
            if (titleBar.targetWindow) {
                titleBar.targetWindow.visibility = titleBar.targetWindow.visibility === Window.Maximized
                    ? Window.Windowed : Window.Maximized
            }
        }
    }
}
