import QtQuick
import QtQuick.Templates as T
import Tandem

T.Slider {
    id: s
    implicitWidth: 200
    implicitHeight: 24
    hoverEnabled: true
    background: Rectangle {
        x: s.leftPadding; y: s.topPadding + s.availableHeight / 2 - height / 2
        width: s.availableWidth; height: 4; radius: 2
        color: Theme.line
        Rectangle { width: s.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
    }
    handle: Rectangle {
        x: s.leftPadding + s.visualPosition * (s.availableWidth - width)
        y: s.topPadding + s.availableHeight / 2 - height / 2
        width: s.pressed || s.hovered ? 16 : 12; height: width; radius: width / 2
        color: Theme.text
        Behavior on width { NumberAnimation { duration: Theme.fast } }
    }
}
