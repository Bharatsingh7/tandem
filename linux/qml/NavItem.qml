import QtQuick
import QtQuick.Templates as T
import Tandem

T.AbstractButton {
    id: n
    property string iconName
    property bool selected: false
    property int badge: 0
    implicitHeight: 38
    hoverEnabled: true
    background: Rectangle {
        radius: Theme.radiusSmall
        color: n.selected ? Theme.raised : n.hovered ? Theme.hover : "transparent"
        border.width: n.selected ? 1 : 0
        border.color: Theme.line
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Rectangle {
            visible: n.selected
            width: 3; height: 16; radius: 1.5
            x: 0; anchors.verticalCenter: parent.verticalCenter
            color: Theme.accent
        }
    }
    contentItem: Item {
        Icon { id: ic; x: 12; anchors.verticalCenter: parent.verticalCenter; name: n.iconName; size: 17; color: n.selected ? Theme.text : Theme.sub }
        Text {
            anchors.left: ic.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
            text: n.text
            color: n.selected ? Theme.text : Theme.sub
            font.family: Theme.sans; font.pixelSize: 14; font.weight: n.selected ? Font.DemiBold : Font.Normal
        }
        Rectangle {
            visible: n.badge > 0
            anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
            height: 18; width: Math.max(18, bt.implicitWidth + 10); radius: 9
            color: Theme.accent
            Text { id: bt; anchors.centerIn: parent; text: n.badge > 99 ? "99+" : n.badge; color: Theme.onAccent; font.family: Theme.sans; font.pixelSize: 11; font.weight: Font.Bold }
        }
    }
}
