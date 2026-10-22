import QtQuick
import BMCD
import BMCD.UI

// Thin horizontal meter used for per-category download progress.
Item {
    id: root
    property real value: 0
    property color fillColor: Tokens.primary
    implicitHeight: 6

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Tokens.surfaceAlt

        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, root.value))
            height: parent.height
            radius: parent.radius
            color: root.fillColor
        }
    }
}
