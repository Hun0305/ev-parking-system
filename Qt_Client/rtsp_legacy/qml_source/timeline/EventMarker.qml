import QtQuick
import ".."

Rectangle {
    id: marker
    width: 2
    height: Theme.timelineHeight + Theme.markerOffset
    color: Theme.eventRed
    clip: false

    Rectangle {
        width: 6
        height: 6
        radius: 3
        color: Theme.eventRed
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
    }
}
