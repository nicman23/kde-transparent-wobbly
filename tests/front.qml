import QtQuick
import QtQuick.Window

Window {
    title: "Front"
    width: 500; height: 350
    visible: true
    color: "#2050a0"
    Grid {
        anchors.fill: parent; rows: 7; columns: 10
        Repeater {
            model: 70
            Rectangle {
                width: 50; height: 50
                color: (index + Math.floor(index / 10)) % 2 ? "#2050a0" : "#f0c020"
            }
        }
    }
}
