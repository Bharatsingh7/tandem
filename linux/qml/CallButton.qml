import QtQuick
import QtQuick.Templates as T
import Tandem

T.AbstractButton {
    id: b
    property string label
    property string iconName
    property color tone: Theme.good
    property bool pulse: false
    implicitWidth: 92
    implicitHeight: label ? 96 : 68
    hoverEnabled: true
    contentItem: Item {
        Rectangle {
            id: disc
            width: 68; height: 68; radius: 34
            anchors.horizontalCenter: parent.horizontalCenter
            color: b.down ? Qt.darker(b.tone, 1.15) : b.hovered ? Qt.lighter(b.tone, 1.08) : b.tone
            Behavior on color { ColorAnimation { duration: Theme.fast } }
            Icon { anchors.centerIn: parent; name: b.iconName; size: 26; color: "white"; filled: true; stroke: 1 }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width; height: width; radius: width / 2
                color: "transparent"
                border.width: 2; border.color: b.tone
                visible: b.pulse
                SequentialAnimation on scale { running: b.pulse && b.visible; loops: Animation.Infinite; NumberAnimation { from: 1; to: 1.45; duration: 1200; easing.type: Easing.OutCubic } }
                SequentialAnimation on opacity { running: b.pulse && b.visible; loops: Animation.Infinite; NumberAnimation { from: 0.7; to: 0; duration: 1200; easing.type: Easing.OutCubic } }
            }
        }
        Text {
            visible: b.label !== ""
            anchors.top: disc.bottom; anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            text: b.label
            color: Theme.sub
            font.family: Theme.sans; font.pixelSize: 12
        }
    }
}
