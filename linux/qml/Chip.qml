import QtQuick
import QtQuick.Templates as T
import Tandem

T.AbstractButton {
    id: c
    property bool selected: false
    property int badge: 0
    implicitHeight: 28
    implicitWidth: lbl.implicitWidth + 24 + (badge ? 22 : 0)
    hoverEnabled: true
    background: Rectangle {
        radius: height / 2
        color: c.selected ? Theme.text : c.hovered ? Theme.hover : "transparent"
        border.width: c.selected ? 0 : 1
        border.color: Theme.line
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    contentItem: Item {
        Text {
            id: lbl
            x: 12; anchors.verticalCenter: parent.verticalCenter
            text: c.text
            color: c.selected ? Theme.bg : Theme.sub
            font.family: Theme.sans; font.pixelSize: 12; font.weight: Font.Medium
        }
        Text {
            visible: c.badge > 0
            anchors.left: lbl.right; anchors.leftMargin: 6; anchors.verticalCenter: parent.verticalCenter
            text: c.badge
            color: c.selected ? Theme.bg : Theme.accent
            font.family: Theme.sans; font.pixelSize: 11; font.weight: Font.Bold
        }
    }
}
