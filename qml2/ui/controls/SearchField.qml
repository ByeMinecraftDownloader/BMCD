import QtQuick
import QtQuick.Controls.Basic
import BMCD.UI

// Search / text input with the focus ring the keyboard flow relies on.
Rectangle {
    id: field

    property alias text: input.text
    property alias placeholder: placeholder.text
    property string label: ""
    property int focusRing: activeFocus ? 1 : 0
    signal accepted()

    implicitHeight: 34
    radius: Tokens.radiusSm
    color: Tokens.surface
    border.width: 1
    border.color: input.activeFocus ? Tokens.primary : Tokens.outline

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: field.radius + 3
        color: "transparent"
        border.width: field.focusRing
        border.color: Qt.rgba(Tokens.primary.r, Tokens.primary.g, Tokens.primary.b, 0.35)
        visible: field.focusRing > 0
    }

    Text {
        id: placeholder
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Tokens.spacingMd
        text: field.label
        color: Tokens.textMuted
        font.pixelSize: Tokens.fontSizeBody
        visible: input.text.length === 0
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Tokens.spacingMd
        anchors.rightMargin: Tokens.spacingMd
        verticalAlignment: TextInput.AlignVCenter
        color: Tokens.text
        selectionColor: Tokens.primary
        selectedTextColor: Tokens.primaryInk
        font.pixelSize: Tokens.fontSizeBody
        onAccepted: field.accepted()
    }
}
