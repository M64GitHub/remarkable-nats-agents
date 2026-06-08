import QtQuick
import AgentChat

// Hand-rolled flat button: a bordered box that inverts to solid-black-with-white-
// text while pressed. No animation — the press state is a discrete repaint, which
// is exactly what e-paper wants. Used for every tappable control in the app.
Item {
    id: root
    property string text: ""
    property bool enabled: true
    signal clicked()

    implicitWidth: Math.max(Theme.touch, label.implicitWidth + Theme.pad * 2)
    implicitHeight: Theme.touch
    opacity: enabled ? 1.0 : 0.35

    Rectangle {
        anchors.fill: parent
        color: mouse.pressed && root.enabled ? Theme.fill : Theme.bg
        border.color: Theme.hairline
        border.width: Theme.border
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.family: Theme.uiFont
        font.pixelSize: Theme.fontM
        color: mouse.pressed && root.enabled ? Theme.bg : Theme.fg
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
