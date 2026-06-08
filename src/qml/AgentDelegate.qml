import QtQuick
import AgentChat

// One roster row: instance name, identity line, description. Whole row is the
// touch target; it inverts while pressed (discrete, no animation).
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property string description: ""
    property bool online: true
    signal clicked()

    implicitHeight: col.implicitHeight + Theme.pad * 2

    Rectangle {
        anchors.fill: parent
        color: mouse.pressed ? Theme.fill : Theme.bg
    }

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.pad
        anchors.rightMargin: Theme.pad
        spacing: Theme.gap / 2

        Text {
            text: (root.online ? "● " : "○ ") + root.title
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontL
            font.bold: true
            color: mouse.pressed ? Theme.bg : Theme.fg
        }
        Text {
            text: root.subtitle
            font.family: Theme.monoFont
            font.pixelSize: Theme.fontS
            color: mouse.pressed ? Theme.bg : Theme.mute
        }
        Text {
            width: col.width
            visible: root.description.length > 0
            text: root.description
            wrapMode: Text.WordWrap
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontS
            color: mouse.pressed ? Theme.bg : Theme.mute
        }
    }

    Rectangle {   // row separator
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.border
        color: Theme.hairline
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: root.clicked()
    }
}
