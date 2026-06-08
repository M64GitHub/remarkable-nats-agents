import QtQuick
import AgentChat

// First screen: connection status + the agent roster. Tapping a row emits
// agentChosen(row); Main routes to the chat view.
Item {
    id: root
    signal agentChosen(int row)

    // ── Server address + connect ───────────────────────────────────────────────
    // Editable NATS endpoint. Type a host / host:port / nats://host:port and tap
    // Connect (or press Enter). The address is persisted by AppController, so it
    // sticks across restarts. On the bare device (no system keyboard yet, M3) this
    // field is typed with a BT/USB keyboard, or preset via config/$AGENT_CHAT_CONFIG.
    Item {
        id: status
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.touch * 2 + Theme.gap * 3

        Item {
            id: serverRow
            anchors.top: parent.top
            anchors.topMargin: Theme.gap
            anchors.left: parent.left
            anchors.leftMargin: Theme.pad
            anchors.right: parent.right
            anchors.rightMargin: Theme.pad
            height: Theme.touch

            FlatButton {
                id: connectBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: App.connectionState === "connected" ? "Reconnect" : "Connect"
                onClicked: root.connect()
            }

            // NATS context picker — taps cycle through ~/.config/nats/context/*.json,
            // applying each one's server URL + creds. Hidden when none are configured.
            FlatButton {
                id: ctxBtn
                anchors.right: connectBtn.left
                anchors.rightMargin: Theme.gap
                anchors.verticalCenter: parent.verticalCenter
                visible: App.natsContexts.length > 0
                text: App.selectedContext.length ? App.selectedContext : "context"
                onClicked: {
                    var list = App.natsContexts
                    if (list.length === 0)
                        return
                    var i = list.indexOf(App.selectedContext)
                    App.useContext(list[(i + 1) % list.length])
                    serverField.text = App.serverUrl
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: ctxBtn.visible ? ctxBtn.left : connectBtn.left
                anchors.rightMargin: Theme.gap
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.touch
                color: Theme.bg
                border.color: Theme.hairline
                border.width: Theme.border

                TextInput {
                    id: serverField
                    anchors.fill: parent
                    anchors.margins: Theme.gap
                    verticalAlignment: TextInput.AlignVCenter
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fontS
                    color: Theme.fg
                    clip: true
                    focus: true
                    selectByMouse: true
                    Component.onCompleted: text = App.serverUrl
                    Keys.onReturnPressed: root.connect()
                    Keys.onEnterPressed: root.connect()
                }

                Text {   // placeholder
                    anchors.fill: parent
                    anchors.margins: Theme.gap
                    verticalAlignment: Text.AlignVCenter
                    visible: serverField.text.length === 0
                    text: "nats://host:4222"
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fontS
                    color: Theme.mute
                }
            }
        }

        Item {
            id: stateRow
            anchors.top: serverRow.bottom
            anchors.topMargin: Theme.gap
            anchors.left: parent.left
            anchors.leftMargin: Theme.pad
            anchors.right: parent.right
            anchors.rightMargin: Theme.pad
            height: Theme.touch

            FlatButton {
                id: refreshBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: App.connectionState === "connected"
                text: "Refresh"
                onClicked: App.refresh()
            }

            Text {
                id: stateText
                anchors.left: parent.left
                anchors.right: refreshBtn.visible ? refreshBtn.left : parent.right
                anchors.rightMargin: refreshBtn.visible ? Theme.gap : 0
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontS
                color: Theme.fg
                text: {
                    if (App.connectionState === "connected") return "● connected · " + App.serverUrl
                    if (App.connectionState === "connecting") return "… connecting · " + App.serverUrl
                    return "○ offline · " + App.serverUrl
                }
            }
        }

        // Keep the field in sync if the address changes elsewhere, unless the user
        // is mid-edit.
        Connections {
            target: App
            function onServerUrlChanged() {
                if (!serverField.activeFocus)
                    serverField.text = App.serverUrl
            }
        }
    }

    function connect() {
        App.setServerUrl(serverField.text)
        App.connectToServer()
    }

    Rectangle {
        id: statusLine
        anchors.top: status.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.border
        color: Theme.hairline
    }

    // ── Roster grid (3 across) ──────────────────────────────────────────────────
    GridView {
        id: grid
        anchors.top: statusLine.bottom
        anchors.topMargin: Theme.gap / 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pad - Theme.gap / 2
        anchors.rightMargin: Theme.pad - Theme.gap / 2
        clip: true
        model: App.agents
        boundsBehavior: Flickable.StopAtBounds
        cellWidth: Math.floor(width / 3)
        cellHeight: Theme.touch * 2 + Theme.gap * 4

        delegate: AgentDelegate {
            width: grid.cellWidth
            height: grid.cellHeight
            title: model.title
            subtitle: model.subtitle
            description: model.description
            online: model.online
            onClicked: root.agentChosen(index)
        }
    }

    Text {
        anchors.centerIn: grid
        visible: grid.count === 0
        text: "No agents in roster"
        font.family: Theme.uiFont
        font.pixelSize: Theme.fontM
        color: Theme.mute
    }
}
