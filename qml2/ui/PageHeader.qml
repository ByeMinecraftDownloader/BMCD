import QtQuick
import BMCD.UI

// One heading pattern for all four views: a 22 px title, a 13 px explanation,
// then consistent space before the content panels.
Column {
    id: header

    property string title
    property string subtitle
    property string eyebrow
    default property alias actions: actionRow.data

    spacing: 4

    Text {
        visible: header.eyebrow !== ""
        text: header.eyebrow
        color: Tokens.textMuted
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
    Text {
        text: header.title
        color: Tokens.text
        font.pixelSize: 22
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        width: parent.width
    }
    Text {
        visible: header.subtitle !== ""
        text: header.subtitle
        color: Tokens.textMuted
        font.pixelSize: 13
        lineHeight: 1.25
        wrapMode: Text.WordWrap
        width: parent.width
    }
    Row {
        id: actionRow
        visible: children.length > 0
        spacing: Tokens.spacingSm
    }
}
