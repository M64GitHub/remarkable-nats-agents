import QtQuick
import QtQuick.Window

// E-paper notes:
//  - Paper Pro has a COLOR e-paper panel, but refresh is slow. Keep the UI
//    high-contrast, avoid animations/gradients, and prefer discrete state
//    changes over continuous motion.
//  - Touch is delivered as mouse events here, so MouseArea works on both the
//    desktop `qml` runtime and on-device.
Window {
    width: Screen.width
    height: Screen.height
    visible: true
    color: "white"

    Column {
        anchors.centerIn: parent
        spacing: 60

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: 72
            color: "black"
            text: "Hello reMarkable Paper Pro!"
        }

        Text {
            id: counter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: 48
            color: "black"
            property int taps: 0
            text: "Taps: " + taps
        }
    }

    MouseArea {
        anchors.fill: parent
        onPressed: counter.taps += 1
    }
}
