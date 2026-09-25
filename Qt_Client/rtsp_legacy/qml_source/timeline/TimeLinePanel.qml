import QtQuick
import QtQuick.Layouts
import ".."

Item {
    id: panel
    height: Theme.statusPanelHeight

    property string selectedName: "없음"
    property string highStatus: "대기"
    property string lowStatus: "대기"
    property string highSpec: "규격 없음"
    property string lowSpec: "규격 없음"
    property string highStartup: "측정 중"
    property string lowStartup: "측정 중"
    property string highDetail: "정상"
    property string lowDetail: "정상"

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Theme.bgPanel
        border.color: Theme.border

        RowLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 14

            StatusBlock {
                title: "선택"
                value: panel.selectedName
                Layout.preferredWidth: 110
            }

            StatusBlock {
                title: "고화질"
                value: panel.highStatus + " / " + panel.highSpec + " / 초기 지연 " + panel.highStartup + " / " + panel.highDetail
                Layout.fillWidth: true
            }

            StatusBlock {
                title: "저화질"
                value: panel.lowStatus + " / " + panel.lowSpec + " / 초기 지연 " + panel.lowStartup + " / " + panel.lowDetail
                Layout.fillWidth: true
            }
        }
    }

    component StatusBlock: Rectangle {
        id: block
        required property string title
        required property string value

        Layout.fillHeight: true
        radius: 4
        color: Theme.bgPanelAlt
        border.color: Theme.border

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 6

            Text {
                text: block.title
                color: Theme.textMuted
                font.pixelSize: 12
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                text: block.value
                color: Theme.textMain
                font.bold: true
                font.pixelSize: 14
                elide: Text.ElideRight
                width: parent.width
            }
        }
    }
}
