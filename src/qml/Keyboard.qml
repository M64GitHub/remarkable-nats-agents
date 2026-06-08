import QtQuick
import AgentChat

// Self-contained on-screen keyboard for the bare device (the probe confirmed no
// system/virtual keyboard). Hand-rolled flat keys that invert while pressed — a
// discrete repaint, no animation, e-paper friendly. Emits text/backspace/enter;
// the chat view applies them to its TextEdit. Hardware keyboards (desktop / BT)
// keep working in parallel, so this is purely additive.
Item {
    id: kb

    signal keyText(string s)
    signal backspace()
    signal enter()

    property bool shifted: false
    property string keyLayer: "letters"     // "letters" | "symbols" (Item.layer is taken)
    property real spacing: Theme.gap

    readonly property var lettersRows: [
        ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
        ["a", "s", "d", "f", "g", "h", "j", "k", "l"],
        ["Shift", "z", "x", "c", "v", "b", "n", "m", "Del"],
        ["123", "Space", "Send"]
    ]
    readonly property var symbolsRows: [
        ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
        ["@", "#", "$", "%", "&", "-", "+", "(", ")", "/"],
        [".", ",", "?", "!", "'", "\"", ":", ";", "Del"],
        ["ABC", "Space", "Send"]
    ]
    readonly property var rows: keyLayer === "letters" ? lettersRows : symbolsRows
    readonly property int rowCount: rows.length
    readonly property real rowHeight: (height - spacing * (rowCount - 1)) / rowCount
    readonly property real unit: (width - spacing * 9) / 10   // 10-column base grid

    function isSpecial(k) {
        return k === "Shift" || k === "Del" || k === "Send"
            || k === "123" || k === "ABC" || k === "Space"
    }
    function keySpan(k) {
        if (k === "Space")
            return 7
        if (isSpecial(k))
            return 1.5
        return 1
    }
    function keyLabel(k) {
        if (k === "Space")
            return "space"
        if (isSpecial(k))
            return k
        return kb.shifted ? k.toUpperCase() : k
    }
    function press(k) {
        switch (k) {
        case "Shift": kb.shifted = !kb.shifted; return
        case "Del":   kb.backspace(); return
        case "Send":  kb.enter(); return
        case "123":   kb.keyLayer = "symbols"; return
        case "ABC":   kb.keyLayer = "letters"; return
        case "Space": kb.keyText(" "); return
        default:
            kb.keyText(kb.shifted ? k.toUpperCase() : k)
            if (kb.shifted)
                kb.shifted = false    // one-shot shift, like a phone keyboard
        }
    }

    Repeater {
        model: kb.rows
        delegate: Item {
            id: rowItem
            required property int index
            required property var modelData
            readonly property var rowKeys: modelData
            width: kb.width
            height: kb.rowHeight
            y: index * (kb.rowHeight + kb.spacing)

            Row {
                anchors.centerIn: parent
                spacing: kb.spacing

                Repeater {
                    model: rowItem.rowKeys
                    delegate: Rectangle {
                        id: key
                        required property var modelData
                        readonly property string keyVal: modelData
                        readonly property bool active:
                            keyMouse.pressed || (keyVal === "Shift" && kb.shifted)
                        width: kb.keySpan(keyVal) * kb.unit + (kb.keySpan(keyVal) - 1) * kb.spacing
                        height: kb.rowHeight
                        color: active ? Theme.fill : Theme.bg
                        border.color: Theme.hairline
                        border.width: Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: kb.keyLabel(key.keyVal)
                            font.family: Theme.uiFont
                            font.pixelSize: kb.isSpecial(key.keyVal) ? Theme.fontS : Theme.fontM
                            color: key.active ? Theme.bg : Theme.fg
                        }

                        MouseArea {
                            id: keyMouse
                            anchors.fill: parent
                            onClicked: kb.press(key.keyVal)
                        }
                    }
                }
            }
        }
    }
}
