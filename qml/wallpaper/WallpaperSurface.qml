import QtQuick

// Background-layer surface, covers the whole screen. Currently just a
// flat fill -- real wallpaper support (loading a user-chosen image,
// scaling modes, per-monitor wallpapers) is a separate follow-up
// feature, not implemented here yet. This exists mainly so
// isDarkBackground-driven acrylic tinting elsewhere in the DE has an
// actual background layer to sit on top of.
Rectangle {
    anchors.fill: parent
    color: "#101010"
}
