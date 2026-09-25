import QtQuick
import Rtsp
import ".."

Item {
    id: root

    property int streamIndex: -1
    property string cameraTitle: "Camera"
    property string rtspUrl: ""
    property string sourceLabel: ""
    property string profileName: ""
    property string qualityLabel: ""
    property bool expanded: false
    readonly property bool hasFrame: videoItem.videoSize.width > 0 && videoItem.videoSize.height > 0
    readonly property string startupDelayText: videoItem.startupDelayMs >= 0 ? videoItem.startupDelayMs + " ms" : root.rtspUrl === "" ? "주소 없음" : "측정 중"
    readonly property string videoSpecText: {
        if (hasFrame) {
            return videoItem.videoSize.width + "x" + videoItem.videoSize.height
        }
        if (root.statusText === "재생 중" || root.statusText === "연결 중") {
            return "규격 확인 중"
        }
        return "규격 없음"
    }
    readonly property string errorDetail: videoItem.errorString
    readonly property string detailText: errorDetail !== "" ? errorDetail : "정상"
    readonly property string statusText: videoItem.status

    signal clicked()

    Rectangle {
        id: container
        anchors.fill: parent
        color: "#000000"
        border.color: mouseArea.containsMouse ? Theme.primary : "#333333"
        border.width: mouseArea.containsMouse ? 2 : 1
        clip: true

        RtspVideoItem {
            id: videoItem
            anchors.fill: parent
            source: root.rtspUrl
        }

        Rectangle {
            id: topBar
            height: root.expanded ? 72 : 58
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            color: "#B0000000"

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 4

                Text {
                    text: root.cameraTitle
                    color: Theme.textMain
                    font.bold: true
                    font.pixelSize: root.expanded ? 20 : 15
                    elide: Text.ElideRight
                    width: parent.width
                }

                Text {
                    text: root.profileName + " | " + root.qualityLabel + " | " + root.sourceLabel + " | " + root.videoSpecText
                    color: Theme.textMuted
                    font.pixelSize: root.expanded ? 13 : 11
                    elide: Text.ElideRight
                    width: parent.width
                }
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            spacing: 8

            Text {
                visible: root.errorDetail !== ""
                text: root.errorDetail
                color: Theme.eventRed
                font.pixelSize: 12
                elide: Text.ElideRight
                width: parent.width - 140
            }

            Row {
                spacing: 8

                StatusPill {
                    label: root.statusText
                    colorValue: root.statusText === "재생 중" ? Theme.accent : root.statusText === "오류" ? Theme.eventRed : Theme.warning
                }

                StatusPill {
                    label: "Spec " + root.videoSpecText
                    colorValue: Theme.accent
                }

                StatusPill {
                    label: "Startup " + root.startupDelayText
                    colorValue: Theme.primary
                }

                StatusPill {
                    label: root.qualityLabel
                    colorValue: root.qualityLabel === "High Quality" ? Theme.primary : Theme.accent
                }
            }
        }

        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 14
            text: root.expanded ? "클릭 또는 ESC로 복귀" : "클릭하여 확대"
            color: Theme.textMuted
            font.pixelSize: 12
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.clicked()
        }
    }

    component StatusPill: Rectangle {
        id: pill
        required property string label
        required property color colorValue

        height: 26
        width: pillText.implicitWidth + 20
        radius: 4
        color: "#CC202029"
        border.color: colorValue

        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.label
            color: Theme.textMain
            font.pixelSize: 12
        }
    }
}
