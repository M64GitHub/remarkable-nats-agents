pragma Singleton
import QtQuick

// Central e-paper style tokens. High contrast (black on white), large type, big
// touch targets, no colour beyond grayscale. Sizes are deliberately generous —
// the device panel is high-DPI and refreshes slowly, so we favour few large
// elements over many small ones. Tune against real geometry on first deploy.
QtObject {
    readonly property color bg: "white"
    readonly property color fg: "black"
    readonly property color mute: "#444444"      // secondary text
    readonly property color hairline: "#000000"  // borders are full-contrast on e-paper
    readonly property color fill: "#000000"       // pressed/selected fill (inverts to white text)

    readonly property string uiFont: "Noto Sans"
    readonly property string monoFont: "Noto Mono"

    readonly property int fontXL: 44
    readonly property int fontL: 34
    readonly property int fontM: 28
    readonly property int fontS: 22

    readonly property int pad: 24       // screen-edge padding
    readonly property int gap: 16       // inter-element spacing
    readonly property int touch: 96     // minimum touch-target height
    readonly property int border: 3     // line weight
}
