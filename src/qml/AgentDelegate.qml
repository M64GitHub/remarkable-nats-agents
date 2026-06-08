import QtQuick
import AgentChat

// One agent as a card (laid out in a grid, 3 across). Status dot + name, the
// agent/owner identity, and a short description. The whole card is the touch
// target and inverts while pressed. Pure B/W, rounded border — e-paper friendly.
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property string description: ""
    property bool online: true
    signal clicked()

    Rectangle {
        id: card
        anchors.fill: parent
        anchors.margins: Theme.gap / 2
        radius: Theme.gap
        color: mouse.pressed ? Theme.fill : Theme.bg
        border.color: Theme.hairline
        border.width: Theme.border

        readonly property color ink: mouse.pressed ? Theme.bg : Theme.fg
        readonly property color sub: mouse.pressed ? Theme.bg : Theme.mute

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.gap
            spacing: Theme.gap / 2

            Item {
                width: parent.width
                height: nameText.implicitHeight

                Rectangle {   // drawn status dot: filled = online, ring = offline
                    id: dot
                    width: Theme.fontS * 0.7
                    height: width
                    radius: width / 2
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.online ? card.ink : "transparent"
                    border.color: card.ink
                    border.width: Theme.border
                }
                Text {
                    id: nameText
                    anchors.left: dot.right
                    anchors.leftMargin: Theme.gap / 2
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.title
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontL
                    font.bold: true
                    color: card.ink
                }
            }

            Text {
                width: parent.width
                text: root.subtitle
                elide: Text.ElideRight
                font.family: Theme.monoFont
                font.pixelSize: Theme.fontS
                color: card.sub
            }

            Text {
                width: parent.width
                visible: root.description.length > 0
                text: root.description
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontS
                color: card.sub
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: root.clicked()
    }
}
