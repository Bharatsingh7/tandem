import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Tandem

Item {
    id: pg
    property string account: ""
    function open(args) {
        if (args && args.number !== undefined) number.text = args.number
        number.forceActiveFocus()
        number.cursorPosition = number.length
    }
    function press(k) {
        number.insert(number.cursorPosition, k)
        number.forceActiveFocus()
    }
    function call() {
        if (!number.text.trim()) { number.text = Calls.lastDialed(); return }
        Calls.dial(number.text, pg.account)
    }
    readonly property var match: number.text.length > 2 ? Contacts.lookup(number.text) : ({})
    readonly property var suggestions: number.text ? Contacts.t9(number.text, 8) : []

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- dialer ----
        Rectangle {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            color: Theme.side
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 0

                Heading { text: "Dialer" }

                // number
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 22
                    spacing: 4
                    T.TextField {
                        id: number
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.text
                        font.family: Theme.serif; font.pixelSize: text.length > 14 ? 26 : 34; font.letterSpacing: 1
                        selectByMouse: true
                        selectionColor: Theme.accentSoft
                        validator: RegularExpressionValidator { regularExpression: /[0-9+*#,;() \-.]*/ }
                        background: null
                        Text {
                            anchors.centerIn: parent
                            visible: !number.length
                            text: "Type or paste a number"
                            color: Theme.faint
                            font.family: Theme.sans; font.pixelSize: 14
                        }
                        Keys.onReturnPressed: pg.call()
                        Keys.onEnterPressed: pg.call()
                        Keys.onPressed: event => {
                            // Letters dial like a phone keypad (T9).
                            const t = event.text.toLowerCase()
                            const map = { abc: "2", def: "3", ghi: "4", jkl: "5", mno: "6", pqrs: "7", tuv: "8", wxyz: "9" }
                            for (const k in map) if (t.length === 1 && k.includes(t) && !(event.modifiers & Qt.ControlModifier)) {
                                number.insert(number.cursorPosition, map[k]); event.accepted = true; return
                            }
                        }
                    }
                    IconBtn {
                        iconName: "backspace"; tip: "Delete (hold to clear)"
                        visible: number.length > 0
                        onClicked: number.remove(Math.max(0, number.cursorPosition - 1), number.cursorPosition)
                        onPressAndHold: number.clear()
                    }
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: 20
                    text: pg.match.name || ""
                    color: Theme.accent
                }

                // T9 suggestions
                ListView {
                    id: sugg
                    Layout.fillWidth: true
                    Layout.preferredHeight: 156
                    Layout.topMargin: 6
                    clip: true
                    model: pg.suggestions
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: T.ItemDelegate {
                        required property var modelData
                        width: sugg.width
                        height: 52
                        hoverEnabled: true
                        background: Rectangle { radius: Theme.radiusSmall; color: parent.hovered ? Theme.hover : "transparent" }
                        contentItem: RowLayout {
                            spacing: 12
                            Avatar { Layout.leftMargin: 8; size: 34; key: modelData.hasPhoto ? "contact/" + modelData.cid : ""; name: modelData.name; rev: Contacts.photoRev }
                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Label { text: modelData.name; Layout.fillWidth: true }
                                Label { text: modelData.phone || ""; small: true; muted: true }
                            }
                            IconBtn { iconName: "phone"; tip: "Call"; size: 30; onClicked: Calls.dial(modelData.phone, pg.account) }
                        }
                        onClicked: { number.text = modelData.phone; number.forceActiveFocus() }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: sugg.count === 0
                        small: true; color: Theme.faint
                        text: number.length ? "No matching contacts" : "Type digits or letters — 5674 finds “Lori”"
                    }
                }

                // keypad
                Grid {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 10
                    columns: 3
                    columnSpacing: 18
                    rowSpacing: 10
                    Repeater {
                        model: [["1", ""], ["2", "ABC"], ["3", "DEF"], ["4", "GHI"], ["5", "JKL"], ["6", "MNO"], ["7", "PQRS"], ["8", "TUV"], ["9", "WXYZ"], ["*", ""], ["0", "+"], ["#", ""]]
                        Key {
                            required property var modelData
                            digit: modelData[0]
                            letters: modelData[1]
                            onClicked: pg.press(digit)
                            onPressAndHold: if (digit === "0") pg.press("+")
                        }
                    }
                }

                Flow {
                    visible: Calls.accounts.length > 1
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 14
                    spacing: 6
                    Repeater {
                        model: Calls.accounts
                        Chip {
                            required property var modelData
                            text: modelData.label
                            selected: pg.account ? pg.account === modelData.id : modelData["default"]
                            onClicked: pg.account = modelData.id
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                T.AbstractButton {
                    id: callBtn
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 14
                    implicitWidth: 64; implicitHeight: 64
                    hoverEnabled: true
                    enabled: Link.online
                    opacity: enabled ? 1 : 0.4
                    onClicked: pg.call()
                    background: Rectangle {
                        radius: 32
                        color: callBtn.down ? Qt.darker(Theme.good, 1.15) : callBtn.hovered ? Qt.lighter(Theme.good, 1.08) : Theme.good
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                    contentItem: Item { Icon { anchors.centerIn: parent; name: "phone"; size: 24; color: "white"; filled: true; stroke: 1 } }
                }
            }
        }
        Separator { Layout.fillHeight: true }

        // ---- recents ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Item { implicitHeight: 14 }
            ListView {
                id: favs
                visible: Contacts.favourites.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.preferredHeight: 92
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                model: Contacts.favourites
                boundsBehavior: Flickable.StopAtBounds
                delegate: T.AbstractButton {
                    required property var modelData
                    width: 76; height: 92
                    hoverEnabled: true
                    onClicked: if (modelData.phone) Calls.dial(modelData.phone, pg.account)
                    background: Rectangle { radius: Theme.radius; color: parent.hovered ? Theme.hover : "transparent" }
                    contentItem: Column {
                        topPadding: 8
                        spacing: 6
                        Avatar { anchors.horizontalCenter: parent.horizontalCenter; size: 48; key: modelData.hasPhoto ? "contact/" + modelData.cid : ""; name: modelData.name; rev: Contacts.photoRev }
                        Label { width: 70; anchors.horizontalCenter: parent.horizontalCenter; horizontalAlignment: Text.AlignHCenter; small: true; text: modelData.name.split(" ")[0] }
                    }
                }
            }

            RowLayout {
                Layout.leftMargin: 28; Layout.rightMargin: 24
                Heading { text: "Recents"; Layout.fillWidth: true }
                Repeater {
                    model: [{ id: "", label: "All" }, { id: "missed", label: "Missed" }, { id: "in", label: "Incoming" }, { id: "out", label: "Outgoing" }]
                    Chip {
                        required property var modelData
                        text: modelData.label
                        selected: Calls.logFilter === modelData.id
                        onClicked: Calls.logFilter = modelData.id
                    }
                }
            }

            ListView {
                id: log
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Calls.log
                reuseItems: true
                cacheBuffer: 800
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: Scroller {}
                section.property: "day"
                section.delegate: Text {
                    required property string section
                    leftPadding: 28; topPadding: 14; bottomPadding: 6
                    text: section.toUpperCase()
                    color: Theme.faint
                    font.family: Theme.sans; font.pixelSize: 11; font.letterSpacing: 1.1; font.weight: Font.DemiBold
                }
                delegate: LogRow { width: log.width; account: pg.account; onPick: n => { number.text = n; number.forceActiveFocus() } }
                Label {
                    anchors.centerIn: parent
                    visible: log.count === 0
                    muted: true
                    text: "No calls yet"
                }
            }
        }
    }
}
