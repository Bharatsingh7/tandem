import QtQuick
import Tandem

Rectangle {
    id: t
    function show(msg) { label.text = msg; t.opacity = 1; hide.restart() }
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: opacity > 0 ? 28 : 12
    width: Math.min(label.implicitWidth + 36, parent.width - 80)
    height: 40
    radius: 20
    color: Theme.text
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    Behavior on anchors.bottomMargin { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
    Text {
        id: label
        textFormat: Text.PlainText
        anchors.centerIn: parent
        width: Math.min(implicitWidth, t.width - 36)
        elide: Text.ElideRight
        color: Theme.bg
        font.family: Theme.sans; font.pixelSize: 14
    }
    Timer { id: hide; interval: 3200; onTriggered: t.opacity = 0 }
}
