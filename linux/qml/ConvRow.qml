import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Tandem

T.ItemDelegate {
    id: r
    required property string kind
    required property string ref
    required property string title
    required property string snippet
    required property var time
    required property int unread
    required property string app
    required property string pkg
    required property string avatar
    readonly property bool selected: Messages.current.kind === kind && Messages.current.ref === ref
    height: 72
    hoverEnabled: true
    onClicked: Messages.open(kind, ref)

    background: Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 8; anchors.rightMargin: 8
        radius: Theme.radiusSmall
        color: r.selected ? Theme.raised : r.hovered ? Theme.hover : "transparent"
    }
    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20; anchors.rightMargin: 20
        spacing: 12
        Item {
            implicitWidth: 44; implicitHeight: 44
            Avatar { anchors.fill: parent; size: 44; key: r.avatar; name: r.title; rev: Contacts.photoRev }
            Rectangle {
                visible: r.kind === "chat"
                anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.rightMargin: -2; anchors.bottomMargin: -2
                width: 16; height: 16; radius: 8
                color: Theme.appTint(r.pkg)
                border.width: 2; border.color: Theme.bg
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Label { Layout.fillWidth: true; text: r.title; font.weight: r.unread ? Font.Bold : Font.Medium }
                Label { small: true; text: Theme.when(r.time); color: r.unread ? Theme.accent : Theme.faint }
            }
            RowLayout {
                Label {
                    Layout.fillWidth: true
                    small: true
                    text: r.snippet || (r.kind === "chat" ? r.app : "")
                    color: r.unread ? Theme.text : Theme.sub
                }
                Rectangle {
                    visible: r.unread > 0
                    height: 18; width: Math.max(18, uc.implicitWidth + 10); radius: 9
                    color: Theme.accent
                    Text { id: uc; anchors.centerIn: parent; text: r.unread; color: Theme.onAccent; font.family: Theme.sans; font.pixelSize: 11; font.weight: Font.Bold }
                }
            }
        }
    }
}
