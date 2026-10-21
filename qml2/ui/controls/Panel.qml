import QtQuick
import BMCD.UI

// A rounded panel. `interactive` adds the hover/press feedback used by list
// rows, but keeps the "stamp" scale out of it: that belongs to list items.
Rectangle {
    id: panel

    property bool interactive: false
    property bool selected: false
    property int padding: Tokens.spacingLg
    default property alias content: contentItem.data

    radius: Tokens.radiusMd
    color: selected ? Tokens.primaryContainer
         : interactive && hover.hovered ? Tokens.surfaceHigh
         : Tokens.surface
    border.width: selected ? 1 : 0
    border.color: Tokens.primary

    Behavior on color { ColorAnimation { duration: Motion.micro } }

    HoverHandler { id: hover; enabled: panel.interactive }

    Item {
        id: contentItem
        anchors.fill: parent

        anchors.margins: panel.padding
    }
}
