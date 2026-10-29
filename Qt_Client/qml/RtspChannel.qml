import QtQuick
import Rtsp

Item {
    id: root

    property string channel: typeof channelName === "undefined" ? "CH" : channelName
    property string lowRtspUrl: typeof rtspLowSourceUrl === "undefined" ? "" : rtspLowSourceUrl
    property string highRtspUrl: typeof rtspHighSourceUrl === "undefined" ? lowRtspUrl : rtspHighSourceUrl
    property string sourceLabel: typeof rtspSourceLabel === "undefined" ? "" : rtspSourceLabel
    property bool expanded: typeof channelExpanded === "undefined" ? false : channelExpanded
    property bool streamEnabled: typeof channelStreamEnabled === "undefined" ? false : channelStreamEnabled
    property bool fireAlarmActive: false
    readonly property string activeRtspUrl: lowRtspUrl
    readonly property bool diagnosticConfigured: activeRtspUrl !== ""
    readonly property string diagnosticStatus: videoItem.status
    readonly property string diagnosticError: videoItem.errorString
    readonly property bool errorVisible: videoItem.errorString !== ""
        && videoItem.status !== "Playing"
    readonly property size diagnosticVideoSize: videoItem.videoSize
    readonly property int diagnosticStartupDelayMs: videoItem.startupDelayMs
    readonly property double diagnosticFrameWallClockMs: videoItem.frameWallClockMs
    readonly property bool reconnecting: videoItem.status === "Connecting"
        || videoItem.status === "Reconnecting"
    signal clicked()

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        border.color: root.fireAlarmActive ? "#ff1744"
                                          : mouseArea.containsMouse ? "#64b5f6" : "#3a4148"
        border.width: root.fireAlarmActive ? 4 : mouseArea.containsMouse ? 2 : 1
        clip: true

        RtspVideoItem {
            id: videoItem
            anchors.fill: parent
            source: root.streamEnabled ? root.activeRtspUrl : ""
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.expanded ? 72 : 64
            color: "#B0000000"

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: reconnectIndicator.visible ? 38 : 10
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 10
                spacing: 2

                Text {
                    text: root.channel
                    color: "#eef2f5"
                    font.pixelSize: root.expanded ? 16 : 14
                    font.bold: true
                    elide: Text.ElideRight
                    width: parent.width
                }

                Text {
                    text: root.sourceLabel
                        + (root.reconnecting ? "" : " | " + videoItem.status)
                        + " | " + videoSpec
                    color: root.errorVisible ? "#ff8a80" : "#aeb9c2"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: parent.width
                }

                Text {
                    text: "Frame " + videoItem.frameClockText
                    color: "#cfd8dc"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: parent.width
                }
            }

            Text {
                id: reconnectIndicator
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 10
                text: "↻"
                visible: root.reconnecting
                color: "#ffca28"
                font.pixelSize: 22
                font.bold: true

                RotationAnimation on rotation {
                    running: reconnectIndicator.visible
                    from: 0
                    to: 360
                    duration: 850
                    loops: Animation.Infinite
                }

                SequentialAnimation on opacity {
                    running: reconnectIndicator.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 420 }
                    NumberAnimation { to: 1.0; duration: 420 }
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: videoItem.videoSize.width <= 0 || videoItem.videoSize.height <= 0
            text: !root.streamEnabled ? "Waiting" : root.activeRtspUrl === "" ? "RTSP URL NOT SET" : videoItem.status
            color: "#8d9aa5"
            font.pixelSize: 13
        }

        Rectangle {
            id: fireOverlay
            z: 2
            anchors.fill: parent
            visible: root.fireAlarmActive
            color: "transparent"
            border.color: "#ff1744"
            border.width: root.expanded ? 8 : 5
            opacity: 1.0

            SequentialAnimation on opacity {
                running: root.fireAlarmActive
                loops: Animation.Infinite
                NumberAnimation { to: 0.45; duration: 420 }
                NumberAnimation { to: 1.0; duration: 420 }
                onStopped: fireOverlay.opacity = 1.0
            }
        }

        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            text: root.expanded ? "Click to return" : "Click for full-screen"
            color: "#aeb9c2"
            font.pixelSize: 11
        }

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            text: root.errorVisible ? videoItem.errorString : ""
            visible: root.errorVisible
            color: "#ff8a80"
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        MouseArea {
            id: mouseArea
            z: 1
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.clicked()
        }
    }

    readonly property string videoSpec: videoItem.videoSize.width > 0
        ? videoItem.videoSize.width + "x" + videoItem.videoSize.height
        : "no frame"
}
