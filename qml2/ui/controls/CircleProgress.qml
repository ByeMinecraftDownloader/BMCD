import QtQuick
import BMCD.UI

// Deterministic progress ring: `value` in 0..1, optional indeterminate sweep.
Item {
    id: ring

    property real value: 0
    property bool indeterminate: false
    property color ringColor: Tokens.primary
    property color trackColor: Tokens.surfaceHigh
    property int thickness: 3
    property string label: ""

    implicitWidth: 44
    implicitHeight: 44

    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const center = width / 2;
            const radius = Math.min(width, height) / 2 - ring.thickness;
            ctx.lineWidth = ring.thickness;
            ctx.strokeStyle = ring.trackColor;
            ctx.beginPath();
            ctx.arc(center, center, radius, 0, Math.PI * 2);
            ctx.stroke();

            if (ring.value > 0) {
                ctx.strokeStyle = ring.ringColor;
                ctx.lineCap = "round";
                ctx.beginPath();
                ctx.arc(center, center, radius, -Math.PI / 2,
                        -Math.PI / 2 + Math.PI * 2 * Math.min(1, ring.value));
                ctx.stroke();
            }
        }

        Connections {
            target: ring
            function onValueChanged() { canvas.requestPaint(); }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    Text {
        anchors.centerIn: parent
        text: ring.label
        color: Tokens.text
        font.pixelSize: Tokens.fontSizeCaption
        font.weight: Font.DemiBold
    }
}
