import QtQuick

Row {
    id: ruler
    anchors.fill: parent
    spacing: (width / 6) - 1

    Repeater {
        model: 6
        delegate: Item {
            required property int index

            width: ruler.width / 6
            height: parent.height

            Rectangle {
                width: 1
                height: 10
                color: "#555555"
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: (12 + index) + ":00"
                color: "#888888"
                font.pixelSize: 11
                anchors.centerIn: parent
            }
        }
    }
}
