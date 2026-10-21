pragma Singleton
import QtQuick
import BMCD

// Semantic layer over Palette: views ask for "background" or "dangerSurface",
// never for a raw colour. Spacing and radii live here too so the four views
// share one grid.
QtObject {
    readonly property color background: Palette.background
    readonly property color surface: Palette.surface
    readonly property color surfaceAlt: Palette.surfaceAlt
    readonly property color surfaceHigh: Palette.surfaceContainerHigh
    readonly property color surfaceContainer: Palette.surfaceContainer
    readonly property color text: Palette.onSurface
    readonly property color textMuted: Palette.onSurfaceMuted
    readonly property color textVariant: Palette.onSurfaceVariant
    readonly property color primary: Palette.primary
    readonly property color onPrimary: Palette.onPrimary
    readonly property color primaryContainer: Palette.primaryContainer
    readonly property color onPrimaryContainer: Palette.onPrimaryContainer
    readonly property color outline: Palette.outline
    readonly property color danger: Palette.error
    readonly property color warning: Palette.warning
    readonly property color success: Palette.success
    readonly property color link: Palette.link

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24
    readonly property int spacingXxl: 32

    readonly property int radiusSm: 6
    readonly property int radiusMd: 10
    readonly property int radiusLg: 14
    readonly property int radiusPill: 999

    readonly property int fontSizeCaption: 11
    readonly property int fontSizeBody: 13
    readonly property int fontSizeTitle: 16
    readonly property int fontSizeHeading: 22
    readonly property int fontSizeDisplay: 30
}
