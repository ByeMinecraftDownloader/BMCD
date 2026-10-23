import QtQuick
import QtQuick.Controls.Basic
import BMCD.UI

// Modal confirm sheet (licence confirmations, destructive redeploys).
Dialog {
    id: dialog

    property string text: ""
    property string acceptText: "确定"
    property string rejectText: "取消"
    property bool destructive: false
    signal confirmed()
    signal cancelled()

    modal: true
    anchors.centerIn: parent
    padding: Tokens.spacingXl
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        color: Tokens.surfaceHigh
        radius: Tokens.radiusLg
        border.width: 1
        border.color: Tokens.outline
    }

    contentItem: Column {
        spacing: Tokens.spacingLg
        Text {
            width: Math.min(dialog.text.length * 9 + 24, 460)
            text: dialog.text
            color: Tokens.text
            wrapMode: Text.WordWrap
            font.pixelSize: Tokens.fontSizeBody
        }
    }

    footer: Row {
        spacing: Tokens.spacingMd
        anchors.right: parent.right
        AppButton {
            text: dialog.rejectText
            onClicked: { dialog.cancelled(); dialog.close(); }
        }
        AppButton {
            text: dialog.acceptText
            primary: !dialog.destructive
            danger: dialog.destructive
            onClicked: { dialog.confirmed(); dialog.close(); }
        }
    }
}
