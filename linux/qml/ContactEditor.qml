import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQuick.Dialogs
import Tandem

Flickable {
    id: ed
    property var contact: ({})
    signal done(var cid)
    contentHeight: form.implicitHeight + 80
    boundsBehavior: Flickable.StopAtBounds
    T.ScrollBar.vertical: Scroller {}

    readonly property var phoneTypes: ["mobile", "home", "work", "other"]
    readonly property var emailTypes: ["home", "work", "other"]

    ListModel { id: phones }
    ListModel { id: emails }

    onVisibleChanged: if (visible) load()
    function load() {
        name.text = contact.name || ""
        org.text = contact.org || ""
        jobTitle.text = contact.title || ""
        phones.clear(); emails.clear()
        for (const p of contact.phones || []) phones.append({ n: p.n, type: p.type || "mobile" })
        for (const e of contact.emails || []) emails.append({ a: e.a, type: e.type || "home" })
        if (!phones.count) phones.append({ n: "", type: "mobile" })
        name.forceActiveFocus()
    }
    function save() {
        const d = { name: name.text.trim(), org: org.text.trim(), title: jobTitle.text.trim(), phones: [], emails: [] }
        if (!d.name) { App.toast("A contact needs a name"); return }
        for (let i = 0; i < phones.count; ++i) if (phones.get(i).n.trim()) d.phones.push({ n: phones.get(i).n.trim(), type: phones.get(i).type })
        for (let i = 0; i < emails.count; ++i) if (emails.get(i).a.trim()) d.emails.push({ a: emails.get(i).a.trim(), type: emails.get(i).type })
        if (contact.cid) d.id = contact.cid
        Contacts.save(d)
        done(contact.cid || 0)
    }

    FileDialog {
        id: photo
        nameFilters: ["Images (*.jpg *.jpeg *.png *.webp)"]
        onAccepted: Contacts.setPhotoFromFile(ed.contact.cid, App.fileUrlToPath(selectedFile))
    }

    ColumnLayout {
        id: form
        width: Math.min(560, parent.width - 80)
        x: (parent.width - width) / 2
        y: 48
        spacing: 14

        RowLayout {
            spacing: 18
            Avatar { size: 72; key: ed.contact.hasPhoto ? "contact/" + ed.contact.cid : ""; name: name.text; rev: Contacts.photoRev }
            ColumnLayout {
                Heading { text: ed.contact.cid ? "Edit contact" : "New contact" }
                Btn { compact: true; kind: "ghost"; text: "Choose photo…"; iconName: "image"; visible: !!ed.contact.cid; onClicked: photo.open() }
            }
        }
        Field { id: name; Layout.fillWidth: true; placeholderText: "Name"; iconName: "user" }
        RowLayout {
            spacing: 8
            Field { id: org; Layout.fillWidth: true; placeholderText: "Company" }
            Field { id: jobTitle; Layout.fillWidth: true; placeholderText: "Job title" }
        }

        Label { text: "PHONE"; small: true; color: Theme.faint; font.letterSpacing: 1.1; Layout.topMargin: 8 }
        Repeater {
            model: phones
            RowLayout {
                required property int index
                required property string n
                required property string type
                spacing: 8
                Field {
                    Layout.fillWidth: true
                    text: n
                    placeholderText: "Number"
                    iconName: "phone"
                    onTextEdited: phones.setProperty(index, "n", text)
                }
                Chip { text: type; onClicked: phones.setProperty(index, "type", ed.phoneTypes[(ed.phoneTypes.indexOf(type) + 1) % ed.phoneTypes.length]) }
                IconBtn { iconName: "x"; tip: "Remove"; onClicked: phones.remove(index) }
            }
        }
        Btn { kind: "ghost"; compact: true; text: "Add number"; iconName: "plus"; onClicked: phones.append({ n: "", type: "mobile" }) }

        Label { text: "EMAIL"; small: true; color: Theme.faint; font.letterSpacing: 1.1; Layout.topMargin: 8 }
        Repeater {
            model: emails
            RowLayout {
                required property int index
                required property string a
                required property string type
                spacing: 8
                Field { Layout.fillWidth: true; text: a; placeholderText: "Email"; onTextEdited: emails.setProperty(index, "a", text) }
                Chip { text: type; onClicked: emails.setProperty(index, "type", ed.emailTypes[(ed.emailTypes.indexOf(type) + 1) % ed.emailTypes.length]) }
                IconBtn { iconName: "x"; tip: "Remove"; onClicked: emails.remove(index) }
            }
        }
        Btn { kind: "ghost"; compact: true; text: "Add email"; iconName: "plus"; onClicked: emails.append({ a: "", type: "home" }) }

        RowLayout {
            Layout.topMargin: 18
            spacing: 8
            Item { Layout.fillWidth: true }
            Btn { text: "Cancel"; kind: "ghost"; onClicked: ed.done(ed.contact.cid || 0) }
            Btn { text: "Save"; kind: "accent"; iconName: "check"; onClicked: ed.save() }
        }
    }
}
