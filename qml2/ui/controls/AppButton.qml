import QtQuick
import BMCD.UI

Rectangle {
    id: button

    property string text: ""
    property string iconName: ""
    property bool primary: false
    property bool danger: false
    property bool enabledByParent: true
    property int horizontalPadding: Tokens.spacingLg
    property int verticalPadding: Tokens.spacingSm
    signal clicked()

    implicitWidth: content.implicitWidth + horizontalPadding * 2
    implicitHeight: Math.max(34, content.implicitHeight + verticalPadding * 2)
    radius: Tokens.radiusSm
    // Disabled is a state, not a faded copy: keep the text legible and drop the
    // accent fill so it never reads as "broken but clickable".
    color: !enabledByParent ? Tokens.surfaceAlt
         : danger ? (mouse.pressed ? Qt.darker(Tokens.danger, 1.15) : Tokens.danger)
         : primary ? (mouse.pressed ? Qt.darker(Tokens.primary, 1.15) : Tokens.primary)
         : (mouse.pressed ? Tokens.surfaceHigh : hover.hovered ? Tokens.surfaceHigh : Tokens.surfaceAlt)
    border.width: !enabledByParent || (!primary && !danger) ? 1 : 0
    border.color: !enabledByParent ? Tokens.hairline : Tokens.outline

    Behavior on color { ColorAnimation { duration: Motion.micro } }
    scale: enabledByParent && mouse.pressed ? 0.98 : 1.0
    Behavior on scale { NumberAnimation { duration: Motion.micro; easing.type: Motion.outCubic } }

    Accessible.role: Accessible.Button
    Accessible.name: button.text

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 6

        Icon {
            visible: button.iconName !== ""
            name: button.iconName
            size: 15
            color: !button.enabledByParent ? Tokens.textMuted
                 : button.danger ? Tokens.background
                 : button.primary ? Tokens.primaryInk : Tokens.text
        }
        Text {
            id: label
            text: button.text
            color: !button.enabledByParent ? Tokens.textMuted
                 : button.danger ? Tokens.background
                 : button.primary ? Tokens.primaryInk : Tokens.text
            font.pixelSize: Tokens.fontSizeBody
            font.weight: button.primary ? Font.DemiBold : Font.Normal
        }
    }

    HoverHandler { id: hover }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: button.enabledByParent
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: button.clicked()
    }
}
