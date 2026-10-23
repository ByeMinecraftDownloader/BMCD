import QtQuick
import BMCD.UI

Row {
    id: row

    property string label: ""
    property string value: ""
    property int labelWidth: 92
    property bool danger: false

    width: parent ? parent.width : implicitWidth
    spacing: Tokens.spacingMd

    Text {
        id: key
        width: row.labelWidth
        text: row.label
        color: Tokens.textMuted
        font.pixelSize: Tokens.fontSizeCaption
    }
    Text {
        width: row.width - key.width - row.spacing
        text: row.value
        color: row.danger ? Tokens.danger : Tokens.text
        font.pixelSize: Tokens.fontSizeBody
        elide: Text.ElideMiddle
    }
}
