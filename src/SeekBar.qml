import QtQuick
import QtQuick.Controls
import Halo

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

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: mouse.containsMouse || mouse.pressed ? 6 : 4
        radius: height / 2
        color: Style.line
        Behavior on height { NumberAnimation { duration: 80 } }

        // Where the next song blends in.
        Rectangle {
            visible: bar.blendFrom >= 0 && bar.duration > 0
            x: parent.width * Math.min(1, bar.blendFrom / Math.max(1, bar.duration))
            width: parent.width - x
            height: parent.height
            radius: parent.radius
            color: Qt.alpha(Style.accent, 0.28)
        }

        Rectangle {
            width: parent.width * (bar.dragFraction >= 0 ? bar.dragFraction : bar.fraction)
            height: parent.height
            radius: parent.radius
            color: Style.accent
        }

        Repeater {
            model: bar.moments
            Rectangle {
                required property var modelData
                x: track.width * Math.min(1, modelData / Math.max(1, bar.duration)) - 1
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 14
                radius: 1
                color: Style.text
            }
        }

        Rectangle {
            visible: mouse.containsMouse || mouse.pressed
            x: track.width * (bar.dragFraction >= 0 ? bar.dragFraction : bar.fraction) - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            radius: 7
            color: Style.text
        }
    }

    // The time under the pointer.
    Rectangle {
        visible: mouse.containsMouse && bar.duration > 0
        x: Math.max(0, Math.min(bar.width - width, mouse.mouseX - width / 2))
        y: -height + 4
        width: hoverTime.implicitWidth + 12
        height: hoverTime.implicitHeight + 6
        radius: Style.small
        color: Style.raised
        border.color: Style.line
        Text {
            id: hoverTime
            anchors.centerIn: parent
            text: Style.clock(bar.duration * Math.max(0, Math.min(1, mouse.mouseX / bar.width)))
            color: Style.text
            font.pixelSize: Style.fontSmall
            font.features: { "tnum": 1 }
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
