import QtQuick
import CymtX

// Mouse-wheel scrolling that glides instead of jumping a notch at a time.
// Put one inside a ListView, GridView or Flickable: its handler sits on the
// view itself, takes the wheel before the view's own scrolling does, and
// eases contentY to where the notches add up. Touchpads (which report pixels) move 1:1 as
// before, and calm (C) scrolls without the glide.
Item {
    id: wheel
    required property Flickable flickable
    property real notch: 120   // pixels a notch moves the view
    property real goal: 0

    function clampY(y) {
        const f = flickable
        const top = f.originY - f.topMargin
        const bottom = Math.max(top, f.originY + f.contentHeight + f.bottomMargin - f.height)
        return Math.max(top, Math.min(bottom, y))
    }

    WheelHandler {
        parent: wheel.flickable
        acceptedModifiers: Qt.NoModifier
        onWheel: event => {
            const f = wheel.flickable
            if (!f.interactive && !f.visible)
                return
            if (event.pixelDelta.y !== 0) {
                glide.stop()
                f.contentY = wheel.clampY(f.contentY - event.pixelDelta.y)
                return
            }
            if (!glide.running)
                wheel.goal = f.contentY
            wheel.goal = wheel.clampY(wheel.goal - event.angleDelta.y / 120 * wheel.notch)
            if (!Style.motion) {
                f.contentY = wheel.goal
                return
            }
            glide.to = wheel.goal
            glide.restart()
        }
    }
    NumberAnimation {
        id: glide
        target: wheel.flickable
        property: "contentY"
        duration: 340
        easing.type: Easing.OutCubic
    }
    // A drag or the scroll bar takes over at once.
    Connections {
        target: wheel.flickable
        function onMovementStarted() { glide.stop() }
    }
}
