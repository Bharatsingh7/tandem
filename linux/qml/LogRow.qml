import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Tandem

T.ItemDelegate {
    id: r
    required property var model
    required property string number
    required property string name
    required property string type
    required property var date
    required property var duration
    required property string place
    required property var cid
    required property bool hasPhoto
    property string account
    signal pick(string number)
    readonly property bool missed: type === "missed" || type === "rejected"
    height: 60
    hoverEnabled: true
    onClicked: pick(number)
    onDoubleClicked: Calls.dial(number, account)

    background: Rectangle {
        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
        radius: Theme.radiusSmall
        color: r.hovered ? Theme.hover : "transparent"
    }
    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 28; anchors.rightMargin: 24
        spacing: 14
        Avatar { size: 38; key: r.hasPhoto ? "contact/" + r.cid : ""; name: r.name || ""; rev: Contacts.photoRev }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Label { Layout.fillWidth: true; text: r.name || Contacts.pretty(r.number) || "Private number"; color: r.missed ? Theme.bad : Theme.text; font.weight: Font.Medium }
            RowLayout {
                spacing: 6
                Icon { name: r.type === "out" ? "callOut" : "callIn"; size: 13; color: r.missed ? Theme.bad : r.type === "out" ? Theme.sub : Theme.good }
                Label {
                    small: true; muted: true
                    text: [r.name ? Contacts.pretty(r.number) : r.place,
                           { missed: "Missed", rejected: "Declined", blocked: "Blocked", voicemail: "Voicemail" }[r.type] || (r.duration > 0 ? Theme.duration(r.duration) : "")
                          ].filter(x => x).join("  ·  ")
                }
            }
        }
        Label { small: true; color: Theme.faint; text: Theme.clock(r.date); visible: !r.hovered }
        Row {
            visible: r.hovered
            spacing: 2
            IconBtn { iconName: "message"; tip: "Message"; size: 32; onClicked: { Messages.openNumber(r.number); App.showPage("messages", {}) } }
            IconBtn { iconName: "trash"; tip: "Remove from history"; size: 32; onClicked: Calls.deleteLog(r.model.id) }
            IconBtn { iconName: "phone"; tip: "Call"; size: 32; tint: Theme.good; onClicked: Calls.dial(r.number, r.account) }
        }
    }
}
