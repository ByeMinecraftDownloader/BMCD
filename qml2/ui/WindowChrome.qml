import QtQuick
import BMCD.UI

// Frameless window chrome: drag, double-click maximise, min/max/close. Resize
// grips live in WindowResizeHandles at the window root.
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
            id: appTitle
            text: chrome.title
            color: Tokens.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Text {
            text: chrome.subtitle
            color: Tokens.textMuted
            font.pixelSize: 11
            anchors.baseline: appTitle.baseline
        }
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height

        Repeater {
            model: [
                { icon: "minimize", action: "min", tip: qsTr("最小化") },
                { icon: "maximize", action: "max", tip: qsTr("最大化") },
                { icon: "close", action: "close", tip: qsTr("关闭") }
            ]
            delegate: Item {
                width: 46
                height: buttons.height
                Rectangle {
                    anchors.fill: parent
                    color: ma.hovered ? (modelData.action === "close" ? Tokens.danger : Tokens.surfaceHigh) : "transparent"
                    Behavior on color { ColorAnimation { duration: Motion.micro } }
                }
                Icon {
                    anchors.centerIn: parent
                    name: modelData.icon
                    size: 12
                    color: ma.hovered && modelData.action === "close" ? Tokens.background : Tokens.textMuted
                }
                HoverHandler { id: ma }
                Accessible.role: Accessible.Button
                Accessible.name: modelData.tip
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

}
