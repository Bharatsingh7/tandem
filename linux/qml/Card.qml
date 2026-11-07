import QtQuick
import Tandem

Rectangle {
    default property alias content: inner.data
    property real pad: 18
    property string title
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: Theme.line
    implicitHeight: inner.implicitHeight + pad * 2 + (title ? 28 : 0)
    Text {
        visible: parent.title !== ""
        x: parent.pad; y: parent.pad - 2
        text: parent.title.toUpperCase()
        color: Theme.faint
        font.family: Theme.sans; font.pixelSize: 11; font.letterSpacing: 1.2; font.weight: Font.DemiBold
    }
    Item {
        id: inner
        x: parent.pad; y: parent.pad + (parent.title ? 28 : 0)
        width: parent.width - parent.pad * 2
        height: parent.height - y - parent.pad
        implicitHeight: childrenRect.height
    }
}
