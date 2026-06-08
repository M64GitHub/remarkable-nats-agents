import QtQuick
import AgentChat

// Second screen: the conversation, a prompt input row, and the on-screen keyboard.
// Input works three ways, all driving the same TextEdit: the on-screen keyboard
// (bare device), and a hardware keyboard (desktop / BT) where Enter sends and
// Shift+Enter inserts a newline.
Item {
    id: root
    signal attachRequested()   // keyboard's Attach key — Main opens the note browser

    ListView {
        id: list
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: attachBar.top
        clip: true
        model: App.messages
        boundsBehavior: Flickable.StopAtBounds
        topMargin: Theme.gap
        bottomMargin: Theme.gap
        cacheBuffer: 0

        delegate: MessageDelegate {
            width: ListView.view.width
            text: model.text
            isUser: model.isUser
            status: model.status
            time: model.time
        }

        onCountChanged: positionViewAtEnd()
        Component.onCompleted: positionViewAtEnd()
    }

    Text {
        anchors.centerIn: list
        visible: list.count === 0
        text: "Send a prompt to start"
        font.family: Theme.uiFont
        font.pixelSize: Theme.fontM
        color: Theme.mute
    }

    // Staged attachment chip — tap to remove. Collapses to nothing when empty.
    Item {
        id: attachBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: inputRow.top
        height: App.attachmentLabel.length > 0 ? Theme.touch * 0.8 : 0
        visible: height > 0
        clip: true

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: Theme.pad
            anchors.right: parent.right
            anchors.rightMargin: Theme.pad
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.touch * 0.62
            radius: Theme.gap
            color: chipMouse.pressed ? Theme.fill : Theme.bg
            border.color: Theme.hairline
            border.width: Theme.border

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.gap
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideMiddle
                text: "attached: " + App.attachmentLabel + "   (tap to remove)"
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontS
                color: chipMouse.pressed ? Theme.bg : Theme.fg
            }
            MouseArea {
                id: chipMouse
                anchors.fill: parent
                onClicked: App.clearAttachment()
            }
        }
    }

    Rectangle {   // separator above the input row
        anchors.bottom: inputRow.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.border
        color: Theme.hairline
    }

    // ── Input row ──────────────────────────────────────────────────────────────
    Item {
        id: inputRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: keyboard.top
        anchors.bottomMargin: Theme.gap
        height: Math.max(Theme.touch + Theme.gap, field.implicitHeight + Theme.gap * 2)

        Rectangle {
            id: fieldBox
            anchors.left: parent.left
            anchors.leftMargin: Theme.pad
            anchors.right: clearButton.left
            anchors.rightMargin: Theme.gap
            anchors.verticalCenter: parent.verticalCenter
            height: Math.max(Theme.touch, field.implicitHeight + Theme.gap)
            color: Theme.bg
            border.color: Theme.hairline
            border.width: Theme.border

            TextEdit {
                id: field
                anchors.fill: parent
                anchors.margins: Theme.gap
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontM
                color: Theme.fg
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                focus: true
                selectByMouse: true

                Keys.onReturnPressed: function(event) {
                    if (event.modifiers & Qt.ShiftModifier) {
                        event.accepted = false   // Shift+Enter: newline
                    } else {
                        root.send()
                        event.accepted = true
                    }
                }
                Keys.onEnterPressed: function(event) {
                    if (event.modifiers & Qt.ShiftModifier) {
                        event.accepted = false
                    } else {
                        root.send()
                        event.accepted = true
                    }
                }
            }

            Text {   // placeholder
                anchors.fill: field
                visible: field.text.length === 0
                text: "Type a prompt…"
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontM
                color: Theme.mute
            }
        }

        FlatButton {
            id: clearButton
            anchors.right: parent.right
            anchors.rightMargin: Theme.pad
            anchors.verticalCenter: parent.verticalCenter
            text: "Clear"
            enabled: field.text.length > 0
            onClicked: field.clear()
        }
    }

    // ── On-screen keyboard ──────────────────────────────────────────────────────
    Keyboard {
        id: keyboard
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pad
        anchors.rightMargin: Theme.pad
        anchors.bottomMargin: Theme.gap
        height: Theme.touch * 4 + Theme.gap * 3

        onKeyText: function(s) { field.insert(field.cursorPosition, s) }
        onBackspace: {
            if (field.cursorPosition > 0)
                field.remove(field.cursorPosition - 1, field.cursorPosition)
        }
        onEnter: root.send()
        onAttach: root.attachRequested()
    }

    function send() {
        var t = field.text
        if (t.trim().length === 0)
            return
        App.sendPrompt(t)
        field.clear()
    }
}
