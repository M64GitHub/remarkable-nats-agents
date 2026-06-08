import QtQuick
import AgentChat

// One chat message. User messages are right-aligned with a thin border; agent
// messages are left-aligned, full width, mono-ish for readability. A pending
// agent reply shows a "…" placeholder; an errored one is marked.
Item {
    id: root
    property string text: ""
    property bool isUser: false
    property string status: "done"   // pending | streaming | done | error

    implicitHeight: bubble.implicitHeight + Theme.gap

    Column {
        id: bubble
        anchors.right: root.isUser ? parent.right : undefined
        anchors.left: root.isUser ? undefined : parent.left
        anchors.rightMargin: Theme.pad
        anchors.leftMargin: Theme.pad
        width: parent.width - Theme.pad * 2 - (root.isUser ? parent.width * 0.12 : 0)
        spacing: Theme.gap / 3

        Text {
            text: root.isUser ? "You" : "Agent"
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontS
            font.bold: true
            color: Theme.mute
            horizontalAlignment: root.isUser ? Text.AlignRight : Text.AlignLeft
            width: parent.width
        }

        Rectangle {
            width: parent.width
            implicitHeight: body.implicitHeight + Theme.gap
            height: implicitHeight
            color: Theme.bg
            border.color: root.isUser ? Theme.hairline : Theme.bg
            border.width: root.isUser ? Theme.border : 0

            Text {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: root.isUser ? Theme.gap : 0
                anchors.rightMargin: root.isUser ? Theme.gap : 0
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                font.family: root.isUser ? Theme.uiFont : Theme.monoFont
                font.pixelSize: Theme.fontM
                color: root.status === "error" ? Theme.fg : Theme.fg
                horizontalAlignment: root.isUser ? Text.AlignRight : Text.AlignLeft
                text: {
                    if (!root.isUser && root.status === "pending")
                        return "…"
                    return root.text
                }
            }
        }
    }
}
