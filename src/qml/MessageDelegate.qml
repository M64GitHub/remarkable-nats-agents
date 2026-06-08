import QtQuick
import AgentChat

// One chat message as a bubble. User messages sit right, agent messages left;
// each carries a small role + timestamp header. Pure black/white with a rounded
// border (no fills/gradients) so it stays on the fast e-paper waveform.
Item {
    id: root
    property string text: ""
    property bool isUser: false
    property string status: "done"   // pending | streaming | done | error
    property string time: ""

    readonly property real maxW: width * (isUser ? 0.74 : 0.88)
    readonly property real pad: Theme.gap
    readonly property string bodyText:
        (!isUser && status === "pending") ? "…" : text

    implicitHeight: bubble.height + Theme.gap

    Rectangle {
        id: bubble
        anchors.right: root.isUser ? parent.right : undefined
        anchors.left: root.isUser ? undefined : parent.left
        anchors.rightMargin: Theme.pad
        anchors.leftMargin: Theme.pad
        width: Math.min(root.maxW,
                        Math.max(body.implicitWidth, header.implicitWidth) + root.pad * 2)
        height: col.implicitHeight + root.pad * 2
        radius: Theme.gap
        color: Theme.bg
        border.color: Theme.hairline
        border.width: status === "error" ? Theme.border * 2 : Theme.border

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: root.pad
            spacing: Theme.gap / 3

            Item {
                id: header
                width: parent.width
                height: roleLabel.implicitHeight
                implicitWidth: roleLabel.implicitWidth + timeLabel.implicitWidth + Theme.gap * 2

                Text {
                    id: roleLabel
                    anchors.left: parent.left
                    text: root.isUser ? "You" : "Agent"
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontS
                    font.bold: true
                    color: Theme.mute
                }
                Text {
                    id: timeLabel
                    anchors.right: parent.right
                    text: root.time
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontS
                    color: Theme.mute
                }
            }

            Text {
                id: body
                width: parent.width
                text: root.bodyText
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                font.family: root.isUser ? Theme.uiFont : Theme.monoFont
                font.pixelSize: Theme.fontM
                color: Theme.fg
            }
        }
    }
}
