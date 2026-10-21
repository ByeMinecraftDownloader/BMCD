import QtQuick
import BMCD.UI

// Three-station rail + settings at the bottom. 72 px wide: icon, label, active
// pill. The rail is the only navigator; the window never shows tabs.
Item {
    id: rail

    property int current: 0
    signal select(int index)

    implicitWidth: 76
    implicitHeight: 320

    readonly property var items: [
        { label: qsTr("版本"), icon: "versions" },
        { label: qsTr("队列"), icon: "queue" },
        { label: qsTr("导出"), icon: "serve" },
        { label: qsTr("设置"), icon: "settings" }
    ]

    Column {
        anchors.top: parent.top
        anchors.topMargin: Tokens.spacingLg
        width: parent.width
        spacing: Tokens.spacingSm

        Repeater {
            model: rail.items
            delegate: Item {
                id: navItem
                width: rail.width
                height: 58
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.selected: index === rail.current

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 4
                    radius: Tokens.radiusMd
                    color: hover.hovered && index !== rail.current ? Tokens.surfaceHigh : "transparent"
                }

                Rectangle {
                    id: pill
                    anchors.centerIn: parent
                    width: 44
                    height: 40
                    radius: Tokens.radiusLg
                    color: index === rail.current ? Tokens.primaryContainer : "transparent"
                    Behavior on color { ColorAnimation { duration: Motion.micro } }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 2
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: modelData.icon
                        size: 19
                        color: index === rail.current ? Tokens.primaryContainerInk : Tokens.textMuted
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label
                        color: index === rail.current ? Tokens.primaryContainerInk : Tokens.textMuted
                        font.pixelSize: 11
                        font.weight: index === rail.current ? Font.DemiBold : Font.Normal
                    }
                }

                HoverHandler { id: hover }
                TapHandler { cursorShape: Qt.PointingHandCursor; onTapped: rail.select(index) }
            }
        }
    }
}
