import QtQuick
import QtQuick.Window
import AgentChat

// Root window and screen router. Two discrete full-screen views — Roster and
// Chat — swapped by `view`. Discrete swaps (not sliding panes) suit e-paper:
// every transition is one clean full repaint. `App` is the C++ controller,
// injected as a context property in main.cpp.
Window {
    id: win
    // On the device the epaper platform drives geometry; on the desktop we open a
    // portrait window roughly matching the Paper Pro aspect for a faithful preview.
    width: 1080
    height: 1440
    visible: true
    color: Theme.bg
    title: "Agent Chat"

    property string view: "roster"   // "roster" | "chat"

    // Connect on startup; the roster's status line reflects the result.
    Component.onCompleted: App.connectToServer()

    // Transient notices from the controller (errors, guidance).
    Connections {
        target: App
        function onNotice(message) { toast.show(message) }
    }

    // ── Header ───────────────────────────────────────────────────────────────
    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.touch + Theme.gap

        FlatButton {
            id: backButton
            anchors.left: parent.left
            anchors.leftMargin: Theme.pad
            anchors.verticalCenter: parent.verticalCenter
            visible: win.view === "chat"
            text: "‹ Agents"
            onClicked: win.view = "roster"
        }

        Text {
            anchors.centerIn: parent
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontL
            font.bold: true
            color: Theme.fg
            elide: Text.ElideRight
            width: parent.width - Theme.pad * 8
            horizontalAlignment: Text.AlignHCenter
            text: win.view === "chat" && App.agentSelected ? App.selectedAgent : "Agents"
        }
    }

    Rectangle {   // header underline
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.border
        color: Theme.hairline
    }

    // ── Content ──────────────────────────────────────────────────────────────
    Item {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.border

        RosterView {
            anchors.fill: parent
            visible: win.view === "roster"
            onAgentChosen: function(row) {
                App.selectAgent(row)
                win.view = "chat"
            }
        }

        ChatView {
            anchors.fill: parent
            visible: win.view === "chat"
        }
    }

    // ── Toast ────────────────────────────────────────────────────────────────
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.pad * 2
        visible: false
        color: Theme.fill
        width: Math.min(parent.width - Theme.pad * 2, toastText.implicitWidth + Theme.pad * 2)
        height: toastText.implicitHeight + Theme.gap * 2

        function show(message) {
            toastText.text = message
            visible = true
            toastTimer.restart()
        }

        Text {
            id: toastText
            anchors.centerIn: parent
            width: parent.width - Theme.pad * 2
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontS
            color: Theme.bg
        }

        Timer {
            id: toastTimer
            interval: 4000
            onTriggered: toast.visible = false
        }
    }
}
