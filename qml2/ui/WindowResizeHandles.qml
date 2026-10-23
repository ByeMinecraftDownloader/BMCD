import QtQuick

// Eight invisible resize grips on the window edges. They live at the window
// root, not inside the 40 px chrome, so the lower corners and the full-height
// sides stay draggable at any window size.
Item {
    id: handles

    property Window targetWindow: Window.window
    anchors.fill: parent
    z: 1000

    Repeater {
        model: ["left", "right", "top", "bottom", "topleft", "topright", "bottomleft", "bottomright"]
        delegate: MouseArea {
            readonly property bool corner: modelData.length > 6
            readonly property int edges: (modelData.indexOf("left") >= 0 ? Qt.LeftEdge
                                          : modelData.indexOf("right") >= 0 ? Qt.RightEdge : 0)
                                         | (modelData.indexOf("top") >= 0 ? Qt.TopEdge
                                            : modelData.indexOf("bottom") >= 0 ? Qt.BottomEdge : 0)

            visible: handles.targetWindow.visibility !== Window.Maximized
            width: corner ? 16 : (modelData === "left" || modelData === "right" ? 6 : handles.width)
            height: corner ? 16 : (modelData === "top" || modelData === "bottom" ? 6 : handles.height)
            x: modelData.indexOf("left") >= 0 ? 0
             : modelData.indexOf("right") >= 0 ? handles.width - width
             : 0
            y: modelData.indexOf("top") >= 0 ? 0
             : modelData.indexOf("bottom") >= 0 ? handles.height - height
             : 0
            cursorShape: (modelData === "left" || modelData === "right") ? Qt.SizeHorCursor
                       : (modelData === "top" || modelData === "bottom") ? Qt.SizeVerCursor
                       : modelData === "topleft" || modelData === "bottomright" ? Qt.SizeFDiagCursor
                       : Qt.SizeBDiagCursor
            onPressed: handles.targetWindow.startSystemResize(edges)
        }
    }
}
