import QtQuick
import BMCD.UI

// Rendered by IconImageProvider (src/theme/IconImageProvider.cpp) as SVG:
// sharp at any size, and tinted with the active palette colour.
Item {
    id: icon

    property string name
    property color color: Tokens.text
    property real size: 18

    implicitWidth: size
    implicitHeight: size


    Image {
        anchors.fill: parent
        sourceSize: Qt.size(Math.round(icon.size), Math.round(icon.size))
        smooth: true
        source: icon.name.length > 0
                ? "image://bmcdicon/" + icon.name + "/" + icon.color.toString().substring(1)
                : ""
    }
}
