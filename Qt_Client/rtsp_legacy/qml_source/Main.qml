import QtQuick
import "./video"
import "./timeline"

Window {
    id: rootWindow
    width: 1280
    height: 720
    visible: true
    title: "RTSP Stream Monitor"
    color: Theme.bgMain

    property int expandedStream: -1
    property string selectedName: expandedStream === 0 ? "고화질" : expandedStream === 1 ? "저화질" : "없음"

    function toggleExpanded(index) {
        expandedStream = expandedStream === index ? -1 : index
    }

    Shortcut {
        sequence: "Esc"
        onActivated: rootWindow.expandedStream = -1
    }

    VideoGridView {
        id: videoGrid
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            bottom: timelinePanel.top
            margins: 10
        }
        expandedIndex: rootWindow.expandedStream
        onStreamSelected: function(index) {
            rootWindow.toggleExpanded(index)
        }
    }

    TimeLinePanel {
        id: timelinePanel
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: 10
        }
        selectedName: rootWindow.selectedName
        highStatus: videoGrid.highStatusText
        lowStatus: videoGrid.lowStatusText
        highSpec: videoGrid.highSpecText
        lowSpec: videoGrid.lowSpecText
        highStartup: videoGrid.highStartupText
        lowStartup: videoGrid.lowStartupText
        highDetail: videoGrid.highDetailText
        lowDetail: videoGrid.lowDetailText
    }
}
