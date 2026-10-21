import QtQuick
import BMCD.UI

Rectangle {
    id: button

    property string text: ""
    property bool primary: false
    property bool danger: false
    property bool enabledByParent: true
    property int horizontalPadding: Tokens.spacingLg
    property int verticalPadding: Tokens.spacingSm
    signal clicked()

    implicitWidth: label.implicitWidth + horizontalPadding * 2
    implicitHeight: label.implicitHeight + verticalPadding * 2
    radius: Tokens.radiusSm
    opacity: enabledByParent ? 1 : 0.45
    color: danger ? (mouse.pressed ? Qt.darker(Tokens.danger, 1.15) : Tokens.danger)
         : primary ? (mouse.pressed ? Qt.darker(Tokens.primary, 1.15) : Tokens.primary)
         : (mouse.pressed ? Tokens.surfaceHigh : hover.hovered ? Tokens.surfaceHigh : Tokens.surfaceAlt)
    border.width: primary || danger ? 0 : 1
    border.color: Tokens.outline

    Behavior on color { ColorAnimation { duration: Motion.micro } }
    scale: mouse.pressed ? 0.98 : 1.0
    Behavior on scale { NumberAnimation { duration: Motion.micro; easing.type: Motion.outCubic } }

    Text {
        id: label
        anchors.centerIn: parent
        text: button.text
        color: button.primary ? Tokens.onPrimary : Tokens.text
        font.pixelSize: Tokens.fontSizeBody
        font.weight: button.primary ? Font.DemiBold : Font.Normal
    }

    HoverHandler { id: hover }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: button.enabledByParent
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
}
