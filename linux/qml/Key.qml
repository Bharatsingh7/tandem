import QtQuick
import QtQuick.Templates as T
import Tandem

T.AbstractButton {
    id: k
    property string digit
    property string letters
    implicitWidth: 72
    implicitHeight: 56
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    autoRepeat: false
    background: Rectangle {
        radius: height / 2
        color: k.down ? Theme.line : k.hovered ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: 80 } }
    }
    contentItem: Item {
        Column {
            anchors.centerIn: parent
            spacing: -2
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: k.digit; color: Theme.text; font.family: Theme.serif; font.pixelSize: 26 }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: k.letters || " "; color: Theme.faint; font.family: Theme.sans; font.pixelSize: 10; font.letterSpacing: 1.5 }
        }
    }
}
