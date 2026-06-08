import QtQuick
import AgentChat

// Compact -[n]+ stepper (hand-rolled, flat, press-to-invert). Used for the page
// range in the note browser. ASCII +/- so it renders on the device's fonts.
Row {
    id: sb
    property int value: 1
    property int minValue: 1
    property int maxValue: 1
    spacing: Theme.gap / 2

    readonly property real btn: Theme.touch * 0.72

    Rectangle {
        width: sb.btn
        height: sb.btn
        anchors.verticalCenter: parent.verticalCenter
        color: minus.pressed ? Theme.fill : Theme.bg
        border.color: Theme.hairline
        border.width: Theme.border
        Text {
            anchors.centerIn: parent
            text: "-"
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontL
            color: minus.pressed ? Theme.bg : Theme.fg
        }
        MouseArea {
            id: minus
            anchors.fill: parent
            onClicked: if (sb.value > sb.minValue) sb.value--
        }
    }

    Text {
        width: sb.btn
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignHCenter
        text: sb.value
        font.family: Theme.uiFont
        font.pixelSize: Theme.fontL
        color: Theme.fg
    }

    Rectangle {
        width: sb.btn
        height: sb.btn
        anchors.verticalCenter: parent.verticalCenter
        color: plus.pressed ? Theme.fill : Theme.bg
        border.color: Theme.hairline
        border.width: Theme.border
        Text {
            anchors.centerIn: parent
            text: "+"
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontL
            color: plus.pressed ? Theme.bg : Theme.fg
        }
        MouseArea {
            id: plus
            anchors.fill: parent
            onClicked: if (sb.value < sb.maxValue) sb.value++
        }
    }
}
