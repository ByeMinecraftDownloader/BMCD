import QtQuick
import BMCD.UI

// Transient message at the bottom of the window; one at a time is enough.
Rectangle {
    id: toast

    property string text: ""
    property bool shown: false

    width: Math.min(parent ? parent.width - Tokens.spacingXxl * 2 : 400, label.implicitWidth + Tokens.spacingXl * 2)
    height: 40
    radius: Tokens.radiusPill
    color: Tokens.surfaceHigh
    border.width: 1
    border.color: Tokens.outline
    opacity: shown ? 1 : 0
    visible: opacity > 0
    y: parent ? parent.height - height - Tokens.spacingXl : 0

    Behavior on opacity { NumberAnimation { duration: Motion.standard; easing.type: Motion.outCubic } }

    Text {
        id: label
        anchors.centerIn: parent
        text: toast.text
        color: Tokens.text
        font.pixelSize: Tokens.fontSizeBody
        elide: Text.ElideRight
    }

    Timer {
        id: hideTimer
        interval: 3200
        onTriggered: toast.shown = false
    }

    function show(message) {
        text = message;
        shown = true;
        hideTimer.restart();
    }
}
