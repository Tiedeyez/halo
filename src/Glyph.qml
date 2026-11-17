import QtQuick
import QtQuick.Shapes

// Icons as vector paths on a 24 px grid (Material Design shapes, Apache
// 2.0), drawn in any colour at any size with no icon font to depend on.
Item {
    id: glyph
    property string name
    property color color: "white"
    implicitWidth: 22
    implicitHeight: 22

    readonly property var paths: ({
        "play": ["M8 5v14l11-7z", false],
        "pause": ["M6 19h4V5H6v14zm8-14v14h4V5h-4z", false],
        "next": ["M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z", false],
        "previous": ["M6 6h2v12H6zm3.5 6l8.5 6V6z", false],
        "shuffle": ["M10.59 9.17L5.41 4 4 5.41l5.17 5.17 1.42-1.41zM14.5 4l2.04 2.04L4 18.59 5.41 20 17.96 7.46 20 9.5V4h-5.5zm.33 9.41l-1.41 1.41 3.13 3.13L14.5 20H20v-5.5l-2.04 2.04-3.13-3.13z", false],
        "repeat": ["M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4z", false],
        "repeatOne": ["M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4zm-4-2V9h-1l-2 1v1h1.5v4H13z", false],
        "queue": ["M15 6H3v2h12V6zm0 4H3v2h12v-2zM3 16h8v-2H3v2zM17 6v8.18c-.31-.11-.65-.18-1-.18-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3V8h3V6h-5z", false],
        "album": ["M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 14.5c-2.49 0-4.5-2.01-4.5-4.5S9.51 7.5 12 7.5s4.5 2.01 4.5 4.5-2.01 4.5-4.5 4.5zm0-5.5c-.55 0-1 .45-1 1s.45 1 1 1 1-.45 1-1-.45-1-1-1z", false],
        "song": ["M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z", false],
        "moment": ["M17 3H7c-1.1 0-1.99.9-1.99 2L5 21l7-3 7 3V5c0-1.1-.9-2-2-2z", false],
        "eq": ["M10 20h4V4h-4v16zm-6 0h4v-8H4v8zM16 9v11h4V9h-4z", false],
        "sleep": ["M12.34 2.02C6.59 1.82 2 6.42 2 12c0 5.52 4.48 10 10 10 3.71 0 6.93-2.02 8.66-5.02-7.51-.25-12.09-8.43-8.32-14.96z", false],
        "volume": ["M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02zM14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4.01-.91 7-4.49 7-8.77s-2.99-7.86-7-8.77z", false],
        "mute": ["M16.5 12c0-1.77-1.02-3.29-2.5-4.03v2.21l2.45 2.45c.03-.2.05-.41.05-.63zm2.5 0c0 .94-.2 1.82-.54 2.64l1.51 1.51C20.63 14.91 21 13.5 21 12c0-4.28-2.99-7.86-7-8.77v2.06c2.89.86 5 3.54 5 6.71zM4.27 3L3 4.27 7.73 9H3v6h4l5 5v-6.73l4.25 4.25c-.67.52-1.42.93-2.25 1.18v2.06c1.38-.31 2.63-.95 3.69-1.81L19.73 21 21 19.73l-9-9L4.27 3zM12 4L9.91 6.09 12 8.18V4z", false],
        "search": ["M15.5 14h-.79l-.28-.27C15.41 12.59 16 11.11 16 9.5 16 5.91 13.09 3 9.5 3S3 5.91 3 9.5 5.91 16 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z", false],
        "close": ["M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z", false],
        "expand": ["M7 14H5v5h5v-2H7v-3zm-2-4h2V7h3V5H5v5zm12 7h-3v2h5v-5h-2v3zM14 5v2h3v3h2V5h-5z", false],
        "top": ["M16 6l2.29 2.29-4.88 4.88-4-4L2 16.59 3.41 18l6-6 4 4 6.3-6.29L22 12V6z", false],
        "history": ["M13 3a9 9 0 0 0-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42A8.954 8.954 0 0 0 13 21a9 9 0 0 0 0-18zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z", false],
        "add": ["M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z", false],
        "playNext": ["M19 9H2v2h17V9zm0-4H2v2h17V5zM2 15h13v-2H2v2zm15-2v6l5-3-5-3z", false],
        "delete": ["M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z", false],
        "back": ["M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z", false],
        "more": ["M12 8c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm0 2c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm0 6c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z", false],
        "blend": ["M9 6.5a5.5 5.5 0 1 0 0 11a5.5 5.5 0 1 0 0-11M15 6.5a5.5 5.5 0 1 0 0 11a5.5 5.5 0 1 0 0-11", true],
        "drift": ["M2 9c2.5-3 4.5-3 7 0s4.5 3 7 0 4.5-3 6-1M2 15c2.5-3 4.5-3 7 0s4.5 3 7 0 4.5-3 6-1", true],
        "lyrics": ["M6 17h3l2-4V7H5v6h3zm8 0h3l2-4V7h-6v6h3z", false],
        "halo": ["M12 4a8 8 0 1 0 0 16a8 8 0 1 0 0-16M12 8.5a3.5 3.5 0 1 0 0 7a3.5 3.5 0 1 0 0-7", true]
    })

    readonly property var entry: paths[name] || paths["song"]

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: Math.min(glyph.width, glyph.height) / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: glyph.entry[1] ? "transparent" : glyph.color
            strokeColor: glyph.entry[1] ? glyph.color : "transparent"
            strokeWidth: glyph.entry[1] ? 2 : 0
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: glyph.entry[0] }
        }
    }
}
