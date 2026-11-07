import QtQuick
import QtCore
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Templates as T
import Tandem

Item {
    id: pg
    property var sel: ({})
    property bool editing: false
    function select(cid) { sel = Contacts.get(cid); editing = false }

    Connections {
        target: Contacts
        function onChanged() { if (pg.sel.cid) pg.sel = Contacts.get(pg.sel.cid) }
    }
    Confirm { id: confirm }
    FileDialog {
        id: exportDlg
        fileMode: FileDialog.SaveFile
        nameFilters: ["vCard (*.vcf)"]
        defaultSuffix: "vcf"
        selectedFile: StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/contacts.vcf"
        onAccepted: App.toast(Contacts.exportVcf(App.fileUrlToPath(selectedFile)) + " contacts exported")
    }
    FileDialog {
        id: importDlg
        nameFilters: ["vCard (*.vcf)"]
        onAccepted: App.toast(Contacts.importVcf(App.fileUrlToPath(selectedFile)) + " contacts imported")
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- list ----
        ColumnLayout {
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            Layout.topMargin: 28
            spacing: 12
            RowLayout {
                Layout.leftMargin: 20; Layout.rightMargin: 14
                Heading { text: "Contacts"; Layout.fillWidth: true }
                IconBtn { iconName: "download"; tip: "Import vCard"; onClicked: importDlg.open() }
                IconBtn { iconName: "upload"; tip: "Export vCard"; onClicked: exportDlg.open() }
                IconBtn { iconName: "plus"; tip: "New contact"; onClicked: { pg.sel = {}; pg.editing = true } }
            }
            Field {
                Layout.fillWidth: true
                Layout.leftMargin: 16; Layout.rightMargin: 16
                iconName: "search"
                placeholderText: "Search " + Contacts.total + " contacts"
                text: Contacts.search
                onTextEdited: Contacts.search = text
            }
            Label {
                visible: Contacts.pending > 0
                Layout.leftMargin: 20
                small: true; color: Theme.accent
                text: Contacts.pending + " change" + (Contacts.pending > 1 ? "s" : "") + " waiting for the phone"
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Contacts.model
                reuseItems: true
                cacheBuffer: 1000
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: Scroller {}
                section.property: "section"
                section.criteria: ViewSection.FullString
                section.delegate: Text {
                    required property string section
                    leftPadding: 24; topPadding: 12; bottomPadding: 4
                    text: section === "★" ? "FAVOURITES" : section
                    color: Theme.accent
                    font.family: section === "★" ? Theme.sans : Theme.serif
                    font.pixelSize: section === "★" ? 11 : 17
                    font.letterSpacing: section === "★" ? 1.1 : 0
                    font.weight: section === "★" ? Font.DemiBold : Font.Normal
                }
                delegate: T.ItemDelegate {
                    id: row
                    required property var cid
                    required property string name
                    required property string phone
                    required property bool hasPhoto
                    width: list.width
                    height: 54
                    hoverEnabled: true
                    onClicked: pg.select(cid)
                    background: Rectangle {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                        radius: Theme.radiusSmall
                        color: pg.sel.cid === row.cid ? Theme.raised : row.hovered ? Theme.hover : "transparent"
                    }
                    contentItem: RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
                        spacing: 12
                        Avatar { size: 36; key: row.hasPhoto ? "contact/" + row.cid : ""; name: row.name; rev: Contacts.photoRev }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { Layout.fillWidth: true; text: row.name }
                            Label { Layout.fillWidth: true; text: row.phone; small: true; muted: true; visible: row.phone !== "" }
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    muted: true
                    text: Contacts.search ? "Nobody matches" : Link.online ? "Syncing contacts…" : "Contacts appear after the phone connects"
                }
            }
        }
        Separator { Layout.fillHeight: true }

        // ---- detail / editor ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                visible: !pg.sel.cid && !pg.editing
                spacing: 10
                Icon { name: "users"; size: 40; stroke: 1.2; color: Theme.faint; Layout.alignment: Qt.AlignHCenter }
                Label { text: "Contacts stay in sync with the phone, both ways"; muted: true; Layout.alignment: Qt.AlignHCenter }
            }

            Flickable {
                anchors.fill: parent
                visible: !!pg.sel.cid && !pg.editing
                contentHeight: detail.implicitHeight + 80
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: Scroller {}
                ColumnLayout {
                    id: detail
                    width: Math.min(620, parent.width - 80)
                    x: (parent.width - width) / 2
                    y: 48
                    spacing: 18
                    RowLayout {
                        spacing: 22
                        Avatar { size: 104; key: pg.sel.hasPhoto ? "contact/" + pg.sel.cid : ""; name: pg.sel.name || ""; rev: Contacts.photoRev }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Heading { big: true; text: pg.sel.name || ""; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            Label { muted: true; visible: text !== ""; text: [pg.sel.title, pg.sel.org].filter(x => x).join(", ") }
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Btn { text: "Call"; iconName: "phone"; kind: "good"; visible: !!pg.sel.phone; onClicked: Calls.dial(pg.sel.phone) }
                        Btn { text: "Message"; iconName: "message"; visible: !!pg.sel.phone; onClicked: { Messages.openNumber(pg.sel.phone); App.showPage("messages", {}) } }
                        IconBtn {
                            iconName: "star"; tip: pg.sel.starred ? "Remove from favourites" : "Add to favourites"
                            active: !!pg.sel.starred
                            onClicked: Contacts.star(pg.sel.cid, !pg.sel.starred)
                        }
                        IconBtn { iconName: "edit"; tip: "Edit"; onClicked: pg.editing = true }
                        IconBtn {
                            iconName: "trash"; tip: "Delete"
                            onClicked: confirm.ask("Delete " + pg.sel.name + "?", "The contact is removed from the phone too.", "Delete", () => { Contacts.remove(pg.sel.cid); pg.sel = {} }, true)
                        }
                    }
                    Card {
                        Layout.fillWidth: true
                        visible: (pg.sel.phones || []).length > 0
                        title: "Phone"
                        ColumnLayout {
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: pg.sel.phones || []
                                RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        TextEdit { text: Contacts.pretty(modelData.n); readOnly: true; selectByMouse: true; color: Theme.text; font.family: Theme.sans; font.pixelSize: 15 }
                                        Label { text: modelData.type || "mobile"; small: true; muted: true }
                                    }
                                    IconBtn { iconName: "message"; tip: "SMS"; onClicked: { Messages.openNumber(modelData.n); App.showPage("messages", {}) } }
                                    IconBtn { iconName: "external"; tip: "Open in WhatsApp on the phone"; onClicked: Phone.openUrl("https://wa.me/" + modelData.n.replace(/[^0-9]/g, "")) }
                                    IconBtn { iconName: "phone"; tip: "Call"; tint: Theme.good; onClicked: Calls.dial(modelData.n) }
                                }
                            }
                        }
                    }
                    Card {
                        Layout.fillWidth: true
                        visible: (pg.sel.emails || []).length > 0
                        title: "Email"
                        ColumnLayout {
                            width: parent.width
                            Repeater {
                                model: pg.sel.emails || []
                                RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        TextEdit { text: modelData.a; readOnly: true; selectByMouse: true; color: Theme.text; font.family: Theme.sans; font.pixelSize: 15 }
                                        Label { text: modelData.type || "email"; small: true; muted: true }
                                    }
                                    IconBtn { iconName: "send"; tip: "Write email"; onClicked: Qt.openUrlExternally("mailto:" + modelData.a) }
                                }
                            }
                        }
                    }
                }
            }

            ContactEditor {
                anchors.fill: parent
                visible: pg.editing
                contact: pg.sel
                onDone: cid => { pg.editing = false; if (cid) pg.select(cid) }
            }
        }
    }
}
