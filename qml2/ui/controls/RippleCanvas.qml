import QtQuick
import BMCD.UI

// Serve screen's "成形" moment: rings converge on success (converge=true) or
// spread on export (converge=false). Pure Rectangles on purpose: this must not
// depend on a working shader pipeline (software rendering / remote desktops).
Item {
    id: ripple

    property bool running: false
    property bool converge: false
    property color rippleColor: Tokens.primary

    // Two rings, staggered; reduced motion collapses this to a short fade.
    Repeater {
        model: 2
        delegate: Rectangle {
            id: circle
            anchors.centerIn: parent
            width: ripple.width
            height: ripple.height
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: ripple.rippleColor
            opacity: 0
            visible: ripple.running

            readonly property int delay: index * 180
            readonly property real fromScale: ripple.converge ? 1.35 : 0.35
            readonly property real toScale: ripple.converge ? 0.35 : 1.35

            scale: ripple.converge ? fromScale : fromScale

            SequentialAnimation {
                running: ripple.running
                PauseAnimation { duration: circle.delay }
                ParallelAnimation {
                    NumberAnimation {
                        target: circle
                        property: "scale"
                        from: circle.fromScale
                        to: circle.toScale
                        duration: Motion.ripple
                        easing.type: Motion.inOutSine
                    }
                    SequentialAnimation {
                        NumberAnimation {
                            target: circle
                            property: "opacity"
                            from: 0
                            to: Motion.reduced ? 0.5 : 0.8
                            duration: Motion.reduced ? 60 : Motion.ripple * 0.25
                        }
                        NumberAnimation {
                            target: circle
                            property: "opacity"
                            to: 0
                            duration: Motion.reduced ? 60 : Motion.ripple * 0.75
                        }
                    }
                }
                ScriptAction {
                    script: if (index === 1 && ripple.converge)
                                ripple.finished()
                }
            }
        }
    }

    signal finished()

    function fire(converging) {
        converge = converging;
        running = false;
        running = true;
    }
}
