import QtQuick
import QtQuick.Layouts
import Tandem

Rectangle {
    id: c
    required property string key
    required property string pkg
    required property string app
    required property string title
    required property string text
    required property var time
    required property bool clearable
    required property var actions
    required property bool canReply
    required property string avatar
    required property string icon
    required property var progress
    property bool replying: false

    implicitHeight: col.implicitHeight + 32
    radius: Theme.radius
    color: hover.hovered ? Theme.raised : Theme.surface
    border.width: 1
    border.color: Theme.line
    Behavior on color { ColorAnimation { duration: Theme.fast } }
    HoverHandler { id: hover }
    TapHandler { onTapped: Notifs.open(c.key) }

    ColumnLayout {
        id: col
        x: 16; y: 16
        width: parent.width - 32
        spacing: 8
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Avatar { size: 18; key: c.icon; name: c.app; iconName: c.icon ? "" : "bell" }
            Label { text: c.app; small: true; muted: true }
            Label { text: "·  " + Theme.when(c.time); small: true; color: Theme.faint }
            Item { Layout.fillWidth: true }
            IconBtn { iconName: "bellOff"; tip: "Mute " + c.app + " on this PC"; size: 26; opacity: hover.hovered ? 1 : 0; onClicked: Notifs.muteApp(c.pkg, true) }
            IconBtn { iconName: "x"; tip: "Dismiss"; size: 26; visible: c.clearable; opacity: hover.hovered ? 1 : 0; onClicked: Notifs.dismiss(c.key) }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { Layout.fillWidth: true; text: c.title; font.weight: Font.DemiBold; visible: text !== "" }
                Label { Layout.fillWidth: true; text: c.text; muted: true; wrapMode: Text.WordWrap; maximumLineCount: 5; visible: text !== "" }
            }
            Avatar { visible: c.avatar !== ""; size: 40; key: c.avatar; name: c.title }
        }
        Rectangle {
            visible: c.progress !== undefined && c.progress !== null && c.progress >= 0
            Layout.fillWidth: true
            height: 4; radius: 2; color: Theme.line
            Rectangle { width: parent.width * Math.max(0, Math.min(1, c.progress || 0)); height: 4; radius: 2; color: Theme.accent }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: (c.actions || []).length > 0 || c.canReply
            Btn { visible: c.canReply && !c.replying; compact: true; text: "Reply"; iconName: "message"; iconSize: 14; onClicked: { c.replying = true; reply.forceActiveFocus() } }
            Repeater {
                model: c.actions || []
                Btn {
                    required property var modelData
                    compact: true
                    kind: "ghost"
                    text: modelData.title
                    onClicked: Notifs.action(c.key, modelData.i)
                }
            }
        }
        RowLayout {
            visible: c.replying
            Layout.fillWidth: true
            spacing: 8
            Field {
                id: reply
                Layout.fillWidth: true
                placeholderText: "Reply to " + (c.title || c.app)
                onAccepted: send.clicked()
                Keys.onEscapePressed: c.replying = false
            }
            IconBtn {
                id: send
                iconName: "send"; tip: "Send"
                fill: Theme.accent; tint: Theme.onAccent
                enabled: reply.text.trim() !== ""
                onClicked: { Notifs.reply(c.key, reply.text.trim()); reply.clear(); c.replying = false }
            }
        }
    }
}
