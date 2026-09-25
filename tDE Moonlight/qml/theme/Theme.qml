pragma Singleton
import QtQuick

// Shared visual design tokens used across every surface (topbar, dock,
// app menu, control panel, power menu). A single QML singleton so a
// value like the corner radius lives in exactly one place instead of
// being copy-pasted (and drifting -- previously 10/11/12/14/16/18/20/
// 24/28/38/height-2 all appeared as separate hardcoded radii across
// different files) across a dozen QML files.
//
// cornerRadius: per the reference screenshot (Affinity Photo/Designer's
// own "9%" corner-radius readout on the mockup's pill shapes, which in
// that tool's units comes out to ~9px for shapes at this scale) -- this
// is a flat pixel value, not a percentage of each shape's own size. A
// literal "9% of width/height" computed per-Rectangle was tried first
// and rejected: it reproduces a fully-round stadium shape on small
// elements (exactly the rounder-than-intended look this change moves
// away from) and a barely-visible corner on large cards -- neither
// matches the mockups, which show one consistent, fairly square corner
// everywhere regardless of the element's size.
QtObject {
    readonly property real cornerRadius: 9

    // A handful of elements are deliberately fully round regardless of
    // this token -- small status dots/indicators (a 4-8px circle reads
    // as a dot either way) and the running-app indicator. Those keep
    // their own radius (height/2 or a small fixed value) rather than
    // referencing cornerRadius, since applying a flat 9px corner to
    // something 5px tall would just clip to a rectangle, not round it.
}
