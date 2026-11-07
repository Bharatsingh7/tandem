import QtQuick
import QtQuick.Layouts
import Tandem

Rectangle {
    id: side
    property var pages: []
    property string current
    signal navigate(string page)
    color: Theme.side

    readonly property var st: Phone.status
    readonly property var bat: st.battery || {}

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 4

        Text {
            Layout.leftMargin: 8
            Layout.topMargin: 8
            text: "Tandem"
            color: Theme.text
            font.family: Theme.serif; font.pixelSize: 26; font.italic: true
        }

        // Phone card
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 16
            Layout.bottomMargin: 14
            implicitHeight: card.implicitHeight + 28
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.line
            ColumnLayout {
                id: card
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
                spacing: 6
                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: Link.online ? Theme.good : Link.state === "pairing" ? Theme.accent : Theme.faint
                        SequentialAnimation on opacity {
                            running: !Link.online && Link.state !== "offline" && !Link.sleeping
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 700 }
                            NumberAnimation { to: 1; duration: 700 }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: Link.phoneName || side.st.model || "Your phone"
                        font.weight: Font.DemiBold
                    }
                }
                Label {
                    Layout.fillWidth: true
                    small: true; muted: true
                    text: !Link.online ? ({ "pairing": "Confirm the code on your phone", "mismatch": "Phone identity changed", "locked": "Open pairing on the phone", "sleeping": "Asleep — nothing is running" }[Link.state] || "Looking for your phone…")
                        : [side.bat.level !== undefined ? side.bat.level + "%" + (side.bat.charging ? " charging" : "") : "",
                           (side.st.network || {}).carrier || "",
                           Link.transport === "usb" ? "USB" : "Wi-Fi"].filter(x => x).join("  ·  ")
                }
                Rectangle {
                    visible: Link.online && side.bat.level !== undefined
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    height: 3; radius: 1.5
                    color: Theme.line
                    Rectangle {
                        width: parent.width * (side.bat.level || 0) / 100
                        height: parent.height; radius: 1.5
                        color: side.bat.level <= 15 && !side.bat.charging ? Theme.bad : side.bat.charging ? Theme.good : Theme.accent
                        Behavior on width { NumberAnimation { duration: 400 } }
                    }
                }
            }
        }

        Repeater {
            model: side.pages
            delegate: NavItem {
                required property var modelData
                visible: modelData.id !== "settings"
                Layout.fillWidth: true
                text: modelData.label
                iconName: modelData.icon
                selected: side.current === modelData.id
                badge: modelData.id === "messages" ? Messages.unread : modelData.id === "notifications" ? Notifs.unread : 0
                onClicked: side.navigate(modelData.id)
            }
        }

        Item { Layout.fillHeight: true }

        // Sleep: disconnects the phone and stops every background job until woken
        NavItem {
            Layout.fillWidth: true
            text: Link.sleeping ? "Wake Tandem" : "Sleep"
            iconName: Link.sleeping ? "sun" : "moon"
            selected: false
            onClicked: Link.setSleeping(!Link.sleeping)
        }

        NavItem {
            Layout.fillWidth: true
            text: "Settings"
            iconName: "sliders"
            selected: side.current === "settings"
            onClicked: side.navigate("settings")
        }
    }
}
