import QtQuick
import QtQuick.Window

// Busy, high-contrast backdrop so blur is easy to see in screenshots.
Window {
    title: "Back"
    width: 1280; height: 800
    visible: true
    color: "white"
    Repeater {
        model: 32
        Rectangle {
            x: index * 40; y: 0; width: 20; height: 800
            color: Qt.hsla(index / 32, 0.9, 0.5, 1)
        }
    }
    Text {
        anchors.centerIn: parent
        text: "SHARP TEXT BEHIND"
        font.pixelSize: 72; font.bold: true
        color: "black"
    }
}
