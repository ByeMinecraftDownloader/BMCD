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
        { label: qsTr("版本"), glyph: "▤" },
        { label: qsTr("队列"), glyph: "≡" },
        { label: qsTr("导出"), glyph: "↥" },
        { label: qsTr("设置"), glyph: "⚙" }
    ]

    Column {
        anchors.top: parent.top
        anchors.topMargin: Tokens.spacingLg
        width: parent.width
        spacing: Tokens.spacingSm

        Repeater {
            model: rail.items
            delegate: Item {
                width: rail.width
                height: 54

                Rectangle {
                    id: pill
                    anchors.centerIn: parent
                    width: 48
                    height: 30
                    radius: Tokens.radiusPill
                    color: index === rail.current ? Tokens.primaryContainer : "transparent"
                    Behavior on color { ColorAnimation { duration: Motion.micro } }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 2
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.glyph
                        color: index === rail.current ? Tokens.onPrimaryContainer : Tokens.textMuted
                        font.pixelSize: 15
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label
                        color: index === rail.current ? Tokens.onPrimaryContainer : Tokens.textMuted
                        font.pixelSize: Tokens.fontSizeCaption
                    }
                }

                HoverHandler { id: hover }
                Rectangle {
                    anchors.fill: parent
                    radius: Tokens.radiusMd
                    color: hover.hovered && index !== rail.current ? Tokens.surfaceHigh : "transparent"
                }

                TapHandler { onTapped: rail.select(index) }
            }
        }
    }
}
