pragma Singleton
import QtQuick
import BMCD as Bmcd
import BMCD.Settings 1.0

// One rhythm for the whole app. Durations live here and nowhere else; when the
// system asks for reduced motion every one of them collapses to a short fade.
QtObject {
    id: motion

    readonly property bool reduced: AppSettings.reduceMotion

    readonly property int micro: reduced ? 90 : 120      // hover, press, tiny shifts
    readonly property int standard: reduced ? 110 : 180  // card entry, badge bump
    readonly property int screen: reduced ? 120 : 260    // view switch, drawer
    readonly property int celebrate: reduced ? 120 : 1000
    readonly property int settle: reduced ? 120 : 700
    readonly property int ripple: celebrate

    readonly property int outCubic: Easing.OutCubic
    readonly property int inOutCubic: Easing.InOutCubic
    readonly property int inOutSine: Easing.InOutSine
    readonly property int outBack: Easing.OutBack

    // Springs are reserved for the "stamp" moment: placing an item in the list.
    readonly property int stampSpring: 220

    function ease(token) {
        return token === "standard" ? outCubic
             : token === "screen" ? inOutCubic
             : token === "celebrate" ? inOutSine
             : token === "settle" ? outCubic
             : outCubic;
    }
}
