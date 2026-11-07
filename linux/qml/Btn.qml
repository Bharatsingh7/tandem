import QtQuick
import QtQuick.Templates as T
import Tandem

// Text button with optional icon. kind: "ghost" | "soft" | "accent" | "good" | "bad"
T.AbstractButton {
    id: b
    property string kind: "soft"
    property string iconName
    property real iconSize: 16
    property bool compact: false
    readonly property color fg: kind === "accent" ? Theme.onAccent : kind === "good" || kind === "bad" ? "#ffffff"
                                 : kind === "ghost" && !hovered ? Theme.sub : Theme.text
    readonly property color base: kind === "accent" ? Theme.accent : kind === "good" ? Theme.good : kind === "bad" ? Theme.bad
                                  : kind === "ghost" ? "transparent" : Theme.raised

    implicitHeight: compact ? 30 : 36
    implicitWidth: Math.max(implicitHeight, row.implicitWidth + (text ? (compact ? 22 : 28) : 0))
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1 : 0.45

    background: Rectangle {
        radius: height / 2
        color: b.kind === "ghost" ? (b.down ? Theme.line : b.hovered ? Theme.hover : "transparent")
                                  : b.down ? Qt.darker(b.base, 1.12) : b.hovered ? Qt.lighter(b.base, b.kind === "soft" ? 1.12 : 1.06) : b.base
        border.width: b.kind === "soft" ? 1 : 0
        border.color: Theme.line
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 7
            Icon { visible: b.iconName !== ""; name: b.iconName; size: b.iconSize; color: b.fg; anchors.verticalCenter: parent.verticalCenter }
            Text {
                visible: b.text !== ""
                text: b.text
                color: b.fg
                font.family: Theme.sans
                font.pixelSize: b.compact ? 12 : 14
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
