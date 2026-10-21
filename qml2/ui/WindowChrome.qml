import QtQuick
import BMCD.UI

// Frameless window chrome: drag, double-click maximise, min/max/close and the
// eight resize edges. The rest of the UI never touches window internals.
Item {
    id: chrome

    property Window targetWindow: Window.window
    property string title: "BMCD"
    property string subtitle: ""

    implicitHeight: 40

    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: buttons.width
        onPressed: chrome.targetWindow.startSystemMove()
        onDoubleClicked: chrome.targetWindow.visibility = chrome.targetWindow.visibility === Window.Maximized
                                                    ? Window.Windowed : Window.Maximized
    }

    Row {
        id: titleRow
        anchors.left: parent.left
        anchors.leftMargin: Tokens.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Tokens.spacingSm

        Text {
            text: chrome.title
            color: Tokens.text
            font.pixelSize: Tokens.fontSizeBody
            font.weight: Font.DemiBold
        }
        Text {
            text: chrome.subtitle
            color: Tokens.textMuted
            font.pixelSize: Tokens.fontSizeCaption
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height

        Repeater {
            model: [
                { label: "—", action: "min" },
                { label: "▢", action: "max" },
                { label: "✕", action: "close" }
            ]
            delegate: Item {
                width: 46
                height: buttons.height
                Rectangle {
                    anchors.fill: parent
                    color: ma.hovered ? (modelData.action === "close" ? Tokens.danger : Tokens.surfaceHigh) : "transparent"
                    Behavior on color { ColorAnimation { duration: Motion.micro } }
                }
                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    color: Tokens.textMuted
                    font.pixelSize: 13
                }
                HoverHandler { id: ma }
                TapHandler {
                    onTapped: {
                        if (modelData.action === "min")
                            chrome.targetWindow.visibility = Window.Minimized;
                        else if (modelData.action === "max")
                            chrome.targetWindow.visibility =
                                chrome.targetWindow.visibility === Window.Maximized
                                    ? Window.Windowed : Window.Maximized;
                        else
                            chrome.targetWindow.close();
                    }
                }
            }
        }
    }

    // Resize edges. 6 px wide, hidden when maximised.
    Repeater {
        model: ["left", "right", "top", "bottom", "topleft", "topright", "bottomleft", "bottomright"]
        delegate: MouseArea {
            readonly property var edges: ({
                left: Qt.LeftEdge, right: Qt.RightEdge, top: Qt.TopEdge, bottom: Qt.BottomEdge
            })
            readonly property bool isCorner: modelData.length > 6
            readonly property var pair: isCorner
                ? [modelData.indexOf("left") >= 0 ? Qt.LeftEdge : Qt.RightEdge,
                   modelData.indexOf("top") >= 0 ? Qt.TopEdge : Qt.BottomEdge]
                : [edges[modelData]]

            visible: chrome.targetWindow.visibility !== Window.Maximized
            z: 10
            width: isCorner ? 16 : (modelData === "left" || modelData === "right" ? 6 : chrome.width)
            height: isCorner ? 16 : (modelData === "top" || modelData === "bottom" ? 6 : chrome.height)
            x: modelData.indexOf("left") >= 0 ? 0
             : modelData.indexOf("right") >= 0 ? chrome.width - width
             : 0
            y: modelData.indexOf("top") >= 0 ? 0
             : modelData.indexOf("bottom") >= 0 ? chrome.height - height
             : 0
            cursorShape: (modelData === "left" || modelData === "right") ? Qt.SizeHorCursor
                       : (modelData === "top" || modelData === "bottom") ? Qt.SizeVerCursor
                       : modelData === "topleft" || modelData === "bottomright" ? Qt.SizeFDiagCursor
                       : Qt.SizeBDiagCursor
            onPressed: chrome.targetWindow.startSystemResize(pair[0] | (pair[1] === undefined ? 0 : pair[1]))
        }
    }
}
