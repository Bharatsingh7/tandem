import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.Basic as C
import Tandem

// Round icon-only button with tooltip.
T.AbstractButton {
    id: b
    property string iconName
    property string tip
    property real size: 34
    property color tint: Theme.sub
    property color fill: "transparent"
    property bool active: false
    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1 : 0.4

    background: Rectangle {
        radius: width / 2
        color: b.active ? Theme.accentSoft : b.fill.a > 0 ? (b.down ? Qt.darker(b.fill, 1.1) : b.hovered ? Qt.lighter(b.fill, 1.08) : b.fill)
                                         : b.down ? Theme.line : b.hovered ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    contentItem: Item {
        Icon { anchors.centerIn: parent; name: b.iconName; size: b.size * 0.5; color: b.active ? Theme.accent : b.hovered && b.fill.a === 0 ? Theme.text : b.tint }
    }
    C.ToolTip.visible: hovered && tip !== ""
    C.ToolTip.text: tip
    C.ToolTip.delay: 500
}
