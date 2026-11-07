import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Tandem

Item {
    id: pg
    function open(args) { if (args && args.focus) composer.forceActiveFocus() }
    Connections {
        target: Messages
        function onCurrentChanged() { Qt.callLater(thread.positionViewAtEnd); if (composer.enabled) composer.forceActiveFocus() }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- conversation list ----
        Rectangle {
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            color: Theme.bg
            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 28
                spacing: 12
                RowLayout {
                    Layout.leftMargin: 20; Layout.rightMargin: 14
                    Heading { text: "Messages"; Layout.fillWidth: true }
                    IconBtn { iconName: "plus"; tip: "New SMS"; onClicked: newMsg.open() }
                }
                Field {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16; Layout.rightMargin: 16
                    iconName: "search"
                    placeholderText: "Search conversations"
                    text: Messages.search
                    onTextEdited: Messages.search = text
                }
                Row {
                    Layout.leftMargin: 16
                    spacing: 6
                    Repeater {
                        model: [{ id: "all", label: "All" }, { id: "wa", label: "WhatsApp" }, { id: "sms", label: "SMS" }, { id: "other", label: "Other apps" }]
                        Chip {
                            required property var modelData
                            text: modelData.label
                            selected: Messages.filter === modelData.id
                            onClicked: Messages.filter = modelData.id
                        }
                    }
                }
                ListView {
                    id: convList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: Messages.conversations
                    reuseItems: true
                    cacheBuffer: 600
                    boundsBehavior: Flickable.StopAtBounds
                    T.ScrollBar.vertical: Scroller {}
                    delegate: ConvRow { width: convList.width }
                    Label {
                        anchors.centerIn: parent
                        visible: convList.count === 0
                        muted: true
                        text: Link.online || Messages.search ? "No conversations" : "Connect your phone to see messages"
                    }
                }
            }
        }
        Separator { Layout.fillHeight: true }

        // ---- thread ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property var cur: Messages.current

            ColumnLayout {
                anchors.centerIn: parent
                visible: !parent.cur.kind
                spacing: 10
                Icon { name: "message"; size: 40; stroke: 1.2; color: Theme.faint; Layout.alignment: Qt.AlignHCenter }
                Label { text: "Pick a conversation"; muted: true; Layout.alignment: Qt.AlignHCenter }
                Label { small: true; color: Theme.faint; text: "WhatsApp chats appear here as messages arrive on the phone."; Layout.alignment: Qt.AlignHCenter }
            }

            ColumnLayout {
                anchors.fill: parent
                visible: !!parent.cur.kind
                spacing: 0

                // header
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 76
                    color: Theme.bg
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 24; anchors.rightMargin: 16
                        spacing: 14
                        Avatar { size: 42; key: Messages.current.avatar || ""; name: Messages.current.title || Messages.current.number || ""; rev: Contacts.photoRev }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { Layout.fillWidth: true; text: Messages.current.title || Contacts.pretty(Messages.current.number || ""); textFormat: Text.PlainText; color: Theme.text; font.family: Theme.serifText; font.pixelSize: 18; elide: Text.ElideRight }
                            Label { Layout.fillWidth: true; small: true; muted: true; text: Messages.current.subtitle || "" }
                        }
                        IconBtn {
                            visible: !!Messages.current.number
                            iconName: "phone"; tip: "Call"
                            onClicked: Calls.dial(Messages.current.number)
                        }
                        IconBtn { iconName: "trash"; tip: "Remove from Tandem"; visible: Messages.current.kind === "chat"; onClicked: { Messages.deleteChat(Messages.current.ref) } }
                        IconBtn { iconName: "x"; tip: "Close"; onClicked: Messages.close() }
                    }
                    Separator { anchors.bottom: parent.bottom; width: parent.width }
                }

                ListView {
                    id: thread
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: Messages.thread
                    onCountChanged: Qt.callLater(positionViewAtEnd)
                    spacing: 4
                    topMargin: 16; bottomMargin: 16
                    reuseItems: true
                    cacheBuffer: 800
                    boundsBehavior: Flickable.StopAtBounds
                    T.ScrollBar.vertical: Scroller {}
                    delegate: Bubble { width: thread.width }
                    Label {
                        anchors.centerIn: parent
                        visible: thread.count === 0
                        muted: true
                        text: Messages.current.kind === "new" ? "New message to " + Contacts.pretty(Messages.current.number || "") : "No messages yet"
                    }
                }

                // composer
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Math.min(160, composer.implicitHeight + 28)
                    color: Theme.bg
                    Separator { width: parent.width }
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        anchors.leftMargin: 20
                        spacing: 10
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 20
                            color: Theme.surface
                            border.width: 1
                            border.color: composer.activeFocus ? Theme.accent : Theme.line
                            Flickable {
                                id: cf
                                anchors.fill: parent
                                anchors.leftMargin: 16; anchors.rightMargin: 16
                                contentHeight: composer.implicitHeight
                                clip: true
                                T.TextArea {
                                    id: composer
                                    width: cf.width
                                    topPadding: 10; bottomPadding: 10
                                    enabled: !!Messages.current.canReply
                                    placeholderText: enabled ? "Message" : "Reply works while the chat's notification is on the phone"
                                    placeholderTextColor: Theme.faint
                                    color: Theme.text
                                    font.family: Theme.sans; font.pixelSize: 14
                                    wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    selectionColor: Theme.accentSoft
                                    Text { visible: !composer.length; text: composer.placeholderText; color: Theme.faint; font: composer.font; y: composer.topPadding; elide: Text.ElideRight; width: composer.width }
                                    Keys.onReturnPressed: event => {
                                        if (event.modifiers & Qt.ShiftModifier) { event.accepted = false; return }
                                        send.clicked()
                                    }
                                }
                            }
                        }
                        IconBtn {
                            id: send
                            Layout.alignment: Qt.AlignBottom
                            size: 40
                            iconName: "send"
                            tip: "Send (Enter)"
                            fill: composer.length ? Theme.accent : Theme.raised
                            tint: composer.length ? Theme.onAccent : Theme.faint
                            enabled: composer.enabled && composer.text.trim().length > 0
                            onClicked: { Messages.send(composer.text.trim()); composer.clear() }
                        }
                    }
                }
            }
        }
    }

    // New SMS: pick a contact or type a number
    T.Popup {
        id: newMsg
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        width: 420; height: 460
        modal: true
        padding: 20
        T.Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle { radius: 16; color: Theme.raised; border.width: 1; border.color: Theme.line }
        onOpened: { to.clear(); to.forceActiveFocus() }
        contentItem: ColumnLayout {
            spacing: 12
            Heading { text: "New message"; font.pixelSize: 22 }
            Field {
                id: to
                Layout.fillWidth: true
                iconName: "search"
                placeholderText: "Name or number"
                onAccepted: if (text.trim()) { Messages.openNumber(text.trim()); newMsg.close() }
            }
            ListView {
                id: hits
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: to.text.trim() ? Contacts.find(to.text, 30) : Contacts.favourites
                delegate: T.ItemDelegate {
                    required property var modelData
                    width: hits.width
                    height: 52
                    hoverEnabled: true
                    background: Rectangle { radius: Theme.radiusSmall; color: parent.hovered ? Theme.hover : "transparent" }
                    contentItem: RowLayout {
                        spacing: 12
                        Avatar { Layout.leftMargin: 8; size: 34; key: modelData.hasPhoto ? "contact/" + modelData.cid : ""; name: modelData.name; rev: Contacts.photoRev }
                        ColumnLayout {
                            spacing: 0
                            Label { text: modelData.name; Layout.fillWidth: true }
                            Label { text: modelData.phone || ""; small: true; muted: true }
                        }
                    }
                    onClicked: { if (modelData.phone) Messages.openNumber(modelData.phone); newMsg.close() }
                }
            }
        }
    }
}
