import QtQuick
import QtQuick.Templates as T
import Tandem

T.AbstractButton {
    id: b
    property string label
    property string iconName
    property bool on: false
    implicitWidth: 72
    implicitHeight: 84
    hoverEnabled: true
    opacity: enabled ? 1 : 0.35
    contentItem: Item {
        Rectangle {
            id: disc
            width: 58; height: 58; radius: 29
            anchors.horizontalCenter: parent.horizontalCenter
            color: b.on ? Theme.text : b.down ? Theme.line : b.hovered ? Theme.hover : Theme.surface
            border.width: b.on ? 0 : 1
            border.color: Theme.line
            Behavior on color { ColorAnimation { duration: Theme.fast } }
            Icon { anchors.centerIn: parent; name: b.iconName; size: 21; color: b.on ? Theme.bg : Theme.text }
        }
        Text {
            anchors.top: disc.bottom; anchors.topMargin: 7
            anchors.horizontalCenter: parent.horizontalCenter
            text: b.label
            color: Theme.sub
            font.family: Theme.sans; font.pixelSize: 12
        }
    }
}
