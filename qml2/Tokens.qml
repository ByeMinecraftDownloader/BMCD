pragma Singleton
import QtQuick
import BMCD as Bmcd

// Semantic layer over Palette: views ask for "background" or "dangerSurface",
// never for a raw colour. Spacing and radii live here too so the four views
// share one grid.
QtObject {
    readonly property color background: Bmcd.Palette.background
    readonly property color surface: Bmcd.Palette.surface
    readonly property color surfaceAlt: Bmcd.Palette.surfaceAlt
    readonly property color surfaceHigh: Bmcd.Palette.surfaceContainerHigh
    readonly property color surfaceContainer: Bmcd.Palette.surfaceContainer
    readonly property color text: Bmcd.Palette.onSurface
    readonly property color textMuted: Bmcd.Palette.onSurfaceMuted
    readonly property color textVariant: Bmcd.Palette.onSurfaceVariant
    readonly property color primary: Bmcd.Palette.primary
    readonly property color onPrimary: Bmcd.Palette.onPrimary
    readonly property color primaryContainer: Bmcd.Palette.primaryContainer
    readonly property color onPrimaryContainer: Bmcd.Palette.onPrimaryContainer
    readonly property color outline: Bmcd.Palette.outline
    readonly property color danger: Bmcd.Palette.error
    readonly property color warning: Bmcd.Palette.warning
    readonly property color success: Bmcd.Palette.success
    readonly property color link: Bmcd.Palette.link

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
