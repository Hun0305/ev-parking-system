import QtQuick

Item {
    id: grid

    property int expandedIndex: -1
    readonly property string highStatusText: highCard.statusText
    readonly property string lowStatusText: lowCard.statusText
    readonly property string highSpecText: highCard.videoSpecText
    readonly property string lowSpecText: lowCard.videoSpecText
    readonly property string highStartupText: highCard.startupDelayText
    readonly property string lowStartupText: lowCard.startupDelayText
    readonly property string highDetailText: highCard.detailText
    readonly property string lowDetailText: lowCard.detailText

    signal streamSelected(int index)

    VideoCard {
        id: highCard
        streamIndex: 0
        cameraTitle: "고화질 스트림"
        profileName: "profile1"
        qualityLabel: "High Quality"
        rtspUrl: streamConfig.highUrl
        sourceLabel: streamConfig.highUrl === "" ? "RTSP_HIGH_URL 미설정" : "RTSP_HIGH_URL"
        expanded: grid.expandedIndex === 0
        visible: grid.expandedIndex === -1 || grid.expandedIndex === 0
        z: expanded ? 2 : 1
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: grid.expandedIndex === 0 ? parent.right : parent.horizontalCenter
        anchors.rightMargin: grid.expandedIndex === -1 ? 5 : 0
        onClicked: grid.streamSelected(streamIndex)
    }

    VideoCard {
        id: lowCard
        streamIndex: 1
        cameraTitle: "저화질 스트림"
        profileName: "profile2"
        qualityLabel: "Low Quality"
        rtspUrl: streamConfig.lowUrl
        sourceLabel: streamConfig.lowUrl === "" ? "RTSP_LOW_URL 미설정" : "RTSP_LOW_URL"
        expanded: grid.expandedIndex === 1
        visible: grid.expandedIndex === -1 || grid.expandedIndex === 1
        z: expanded ? 2 : 1
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: grid.expandedIndex === 1 ? parent.left : parent.horizontalCenter
        anchors.right: parent.right
        anchors.leftMargin: grid.expandedIndex === -1 ? 5 : 0
        onClicked: grid.streamSelected(streamIndex)
    }
}
