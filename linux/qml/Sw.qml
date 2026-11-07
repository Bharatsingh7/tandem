import QtQuick
import QtQuick.Templates as T
import Tandem

T.Switch {
    id: s
    implicitWidth: 40
    implicitHeight: 22
    hoverEnabled: true
    indicator: Rectangle {
        width: 40; height: 22; radius: 11
        color: s.checked ? Theme.accent : Theme.line
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Rectangle {
            width: 16; height: 16; radius: 8
            y: 3
            x: s.checked ? 21 : 3
            color: s.checked ? Theme.onAccent : Theme.sub
            Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
        }
    }
}
