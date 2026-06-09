import QtQuick
import AgentChat

// Attachment browser: pick a notebook, choose a page range, attach. Lists only
// notebooks (App.notes / NoteStore). Tapping a note reveals the page-range bar.
Item {
    id: root
    signal attached()
    signal cancelled()

    property int selRow: -1
    property int pageCount: 1
    property string fmt: "png"   // attachment format: "png" (full-res images) or "pdf"

    function selectNote(row, pages) {
        selRow = row
        pageCount = pages
        fromBox.value = 1
        toBox.value = pages
    }

    // Reset selection whenever the browser is shown.
    onVisibleChanged: if (visible) selRow = -1

    ListView {
        id: list
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: rangeBar.top
        clip: true
        model: App.notes
        boundsBehavior: Flickable.StopAtBounds

        delegate: Item {
            width: ListView.view.width
            height: col.implicitHeight + Theme.pad * 1.5
            readonly property bool chosen: index === root.selRow

            Rectangle {
                anchors.fill: parent
                color: (parent.chosen || noteMouse.pressed) ? Theme.fill : Theme.bg
            }
            Column {
                id: col
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.pad
                anchors.rightMargin: Theme.pad
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.gap / 3

                Text {
                    width: parent.width
                    text: model.name
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontL
                    font.bold: true
                    color: (parent.parent.chosen || noteMouse.pressed) ? Theme.bg : Theme.fg
                }
                Text {
                    width: parent.width
                    text: (model.folder.length ? model.folder + " · " : "")
                          + model.pageCount + (model.pageCount === 1 ? " page" : " pages")
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontS
                    color: (parent.parent.chosen || noteMouse.pressed) ? Theme.bg : Theme.mute
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: Theme.border
                color: Theme.hairline
            }
            MouseArea {
                id: noteMouse
                anchors.fill: parent
                onClicked: root.selectNote(index, model.pageCount)
            }
        }
    }

    Text {
        anchors.centerIn: list
        visible: list.count === 0
        width: parent.width - Theme.pad * 2
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: "No notebooks found.\nAttachments read the device's library."
        font.family: Theme.uiFont
        font.pixelSize: Theme.fontM
        color: Theme.mute
    }

    // ── Page range + attach (shown once a note is picked) ───────────────────────
    Rectangle {
        id: rangeBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.selRow >= 0 ? Theme.touch + Theme.gap * 2 : 0
        visible: height > 0
        clip: true
        color: Theme.bg

        Rectangle {   // top border
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.border
            color: Theme.hairline
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.pad
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gap

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "pages"
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontM
                color: Theme.fg
            }
            StepBox {
                id: fromBox
                anchors.verticalCenter: parent.verticalCenter
                minValue: 1
                maxValue: toBox.value
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "to"
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontM
                color: Theme.fg
            }
            StepBox {
                id: toBox
                anchors.verticalCenter: parent.verticalCenter
                minValue: fromBox.value
                maxValue: root.pageCount
            }

            // Format toggle: full-res PNG per page, or one compact vector PDF.
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0
                Repeater {
                    model: ["PNG", "PDF"]
                    delegate: Rectangle {
                        width: Theme.touch * 1.1
                        height: Theme.touch * 0.78
                        readonly property bool sel: root.fmt === modelData.toLowerCase()
                        color: sel ? Theme.fg : Theme.bg
                        border.color: Theme.hairline
                        border.width: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontS
                            color: parent.sel ? Theme.bg : Theme.fg
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.fmt = modelData.toLowerCase()
                        }
                    }
                }
            }
        }

        FlatButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.pad
            anchors.verticalCenter: parent.verticalCenter
            text: "Attach"
            onClicked: {
                App.stageNotePages(root.selRow, fromBox.value, toBox.value, root.fmt)
                root.attached()
            }
        }
    }
}
