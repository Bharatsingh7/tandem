import QtQuick
import QtQuick.Templates as T
import Tandem

T.AbstractButton {
    id: t
    property string label
    property string iconName
    property string detail
    property bool on: false
    property bool busy: false
    signal flip()
    onClicked: if (!busy) flip()
    implicitHeight: 96
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45

    background: Rectangle {
        radius: Theme.radius
        color: t.on ? Theme.accentSoft : t.hovered ? Theme.hover : Theme.surface
        border.width: 1
        border.color: t.on ? Qt.alpha(Theme.accent, 0.45) : Theme.line
        Behavior on color { ColorAnimation { duration: Theme.normal } }
        Behavior on border.color { ColorAnimation { duration: Theme.normal } }
    }
    contentItem: Item {
        Rectangle {
            x: 16; y: 16
            width: 34; height: 34; radius: 17
            color: t.on ? Theme.accent : Theme.raised
            Behavior on color { ColorAnimation { duration: Theme.normal } }
            Icon { anchors.centerIn: parent; name: t.iconName; size: 17; color: t.on ? Theme.onAccent : Theme.sub }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: Theme.accent
                visible: t.busy
                opacity: 0.6
                SequentialAnimation on opacity {
                    running: t.busy; loops: Animation.Infinite
                    NumberAnimation { to: 0.1; duration: 500 }
                    NumberAnimation { to: 0.7; duration: 500 }
                }
            }
        }
        Column {
            x: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
            width: parent.width - 32
            spacing: 1
            Text { text: t.label; color: Theme.text; font.family: Theme.sans; font.pixelSize: 14; font.weight: Font.Medium; width: parent.width; elide: Text.ElideRight }
            Text {
                text: t.busy ? "Switching…" : t.detail || (t.on ? "On" : "Off")
                color: t.on ? Theme.accent : Theme.faint
                font.family: Theme.sans; font.pixelSize: 12
                width: parent.width; elide: Text.ElideRight
            }
        }
    }
}
