import QtQuick
import QtQuick.Controls
import CymtX

// Time through the track, with the track's moments as ticks and, while a
// blend is coming, the stretch where the next song fades in.
Item {
    id: bar
    property real position: 0
    property real duration: 0
    property var moments: []
    property real blendFrom: -1
    property bool compact: false

    implicitHeight: Style.target
    readonly property real fraction: duration > 0 ? Math.min(1, position / duration) : 0
    property real dragFraction: -1

    Accessible.role: Accessible.Slider
    Accessible.name: qsTr("Position")
    Accessible.description: Style.clock(position) + " / " + Style.clock(duration)

    readonly property real shown: dragFraction >= 0 ? dragFraction : fraction
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown

    Item {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: mouse.containsMouse || mouse.pressed ? 8 : 6
        Behavior on height { NumberAnimation { duration: 80 } }

        // Square cells (see shaders/pixelbar.frag): played, blend, rest.
        ShaderEffect {
            anchors.fill: parent
            visible: bar.supported
            property real fraction: bar.shown
            property real blendFrom: bar.blendFrom >= 0 && bar.duration > 0 ? Math.min(1, bar.blendFrom / bar.duration) : -1
            property real cell: 4
            property real gap: 2
            property size size: Qt.size(width, height)
            property color played: Style.accent
            property color rest: Style.line
            property color blend: Qt.tint(Style.line, Qt.alpha(Style.accent, 0.45))
            fragmentShader: "qrc:/shaders/pixelbar.frag.qsb"
        }
        // The same without shaders (Qt's software renderer).
        Rectangle {
            anchors.fill: parent
            visible: !bar.supported
            color: Style.line
            Rectangle { width: parent.width * bar.shown; height: parent.height; color: Style.accent }
        }

        Repeater {
            model: bar.moments
            Rectangle {
                required property var modelData
                x: Math.round(track.width * Math.min(1, modelData / Math.max(1, bar.duration)) - 1)
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 14
                color: Style.text
            }
        }

        // The playhead: a terminal's block cursor, blinking while paused.
        Rectangle {
            id: head
            readonly property bool big: mouse.containsMouse || mouse.pressed
            x: Math.round(track.width * bar.shown - width / 2)
            anchors.verticalCenter: parent.verticalCenter
            width: big ? 8 : 4
            height: big ? 18 : 12
            color: big ? Style.text : Style.accentText
            visible: bar.duration > 0
            opacity: !player.playing && !big && Style.motion && blink.on ? 0.15 : 1
            Timer {
                id: blink
                property bool on: false
                interval: 530
                repeat: true
                running: !player.playing && bar.duration > 0 && Style.motion && bar.visible
                onTriggered: on = !on
                onRunningChanged: if (!running) on = false
            }
        }
    }

    // The time under the pointer.
    Rectangle {
        visible: mouse.containsMouse && bar.duration > 0
        x: Math.max(0, Math.min(bar.width - width, mouse.mouseX - width / 2))
        y: -height + 4
        width: hoverTime.implicitWidth + 12
        height: hoverTime.implicitHeight + 6
        color: Style.raised
        border.color: Style.accent
        Text {
            textFormat: Text.PlainText
            id: hoverTime
            anchors.centerIn: parent
            text: Style.clock(bar.duration * Math.max(0, Math.min(1, mouse.mouseX / bar.width)))
            color: Style.text
            font.pixelSize: Style.fontSmall
            font.family: Style.mono
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        function at(x) { return Math.max(0, Math.min(1, x / width)) }
        onPressed: e => bar.dragFraction = at(e.x)
        onPositionChanged: e => { if (pressed) bar.dragFraction = at(e.x) }
        onReleased: e => {
            if (bar.duration > 0)
                player.seek(at(e.x) * bar.duration)
            bar.dragFraction = -1
        }
        onCanceled: bar.dragFraction = -1
    }
}
