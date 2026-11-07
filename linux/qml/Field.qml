import QtQuick
import QtQuick.Templates as T
import Tandem

T.TextField {
    id: f
    property string iconName
    implicitHeight: 38
    implicitWidth: 240
    leftPadding: iconName ? 36 : 14
    rightPadding: 14
    color: Theme.text
    placeholderTextColor: Theme.faint
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.text
    font.family: Theme.sans
    font.pixelSize: 14
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true

    Text {
        x: f.leftPadding; anchors.verticalCenter: parent.verticalCenter
        visible: !f.length && !f.preeditText
        text: f.placeholderText
        color: f.placeholderTextColor
        font: f.font
    }
    background: Rectangle {
        radius: f.height / 2
        color: Theme.surface
        border.width: 1
        border.color: f.activeFocus ? Theme.accent : Theme.line
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        Icon { visible: f.iconName !== ""; name: f.iconName; size: 15; color: Theme.faint; x: 13; anchors.verticalCenter: parent.verticalCenter }
    }
}
