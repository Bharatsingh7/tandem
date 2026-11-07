import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import Tandem

Flickable {
    id: pg
    contentHeight: col.implicitHeight + 80
    boundsBehavior: Flickable.StopAtBounds
    T.ScrollBar.vertical: Scroller {}

    Confirm { id: confirm }


    ColumnLayout {
        id: col
        width: Math.min(760, pg.width - 72)
        x: (pg.width - width) / 2
        y: 36
        spacing: 18

        Heading { text: "Settings"; big: true }

        Card {
            title: "Phone"
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: 12
                Row2 { label: "Status"; value: Link.online ? "Connected to " + Link.phoneName : { pairing: "Waiting for you to confirm on the phone", mismatch: "Phone identity changed" }[Link.state] || "Not connected" }
                Row2 { label: "Connection"; value: !Link.online ? "—" : Link.transport === "usb" ? "USB (" + Link.serial + ")" : "Wi-Fi, " + Link.host + (Phone.pcUsesHotspot() ? " (phone's hotspot)" : "") }
                Row2 { label: "Helper"; value: Link.helper ? "Running — switches and lock work" : "Not running — plug in USB once to start it" }
                Row2 { label: "Fingerprint"; value: Link.phoneFingerprint ? Link.phoneFingerprint.slice(0, 23).toUpperCase() + "…" : "—"; mono: true }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Btn { text: "Reconnect"; iconName: "refresh"; onClicked: Link.reconnect() }
                    Btn { text: "Set up over USB"; iconName: "usb"; onClicked: Phone.setupOverUsb() }
                    Btn { text: "Start helper"; iconName: "sliders"; enabled: Link.serial !== ""; onClicked: Link.ensureHelper() }
                    Btn {
                        text: "Forget phone"; kind: "ghost"
                        onClicked: confirm.ask("Forget this phone?", "Tandem stops trusting it. You'll confirm a pairing code on the phone the next time it connects.", "Forget", () => Link.forget(), true)
                    }
                }
                Label {
                    Layout.fillWidth: true
                    small: true; muted: true; wrapMode: Text.WordWrap
                    text: "“Set up over USB” grants the phone app its permissions (notifications, SMS, calls, contacts) and call control in one go. Run it again after reinstalling the phone app."
                }
            }
        }

        Card {
            title: "Calls"
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Rectangle {
                        width: 40; height: 40; radius: 20
                        color: Bt.phoneState === "connected" ? Theme.good : Bt.phoneState === "paired" ? Theme.accentSoft : Theme.raised
                        Icon { anchors.centerIn: parent; name: "bluetooth"; size: 18; color: Bt.phoneState === "connected" ? "white" : Bt.phoneState === "paired" ? Theme.accent : Theme.sub }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            font.weight: Font.Medium
                            text: !Bt.available ? "No Bluetooth adapter on this PC"
                                : Bt.pairing ? (Bt.passkey ? "Confirm " + Bt.passkey + " on the phone" : "Waiting for the phone… accept the pairing request there")
                                : { connected: "Call audio ready", paired: "Paired for call audio", unpaired: "Not paired" }[Bt.phoneState] || "Not paired"
                        }
                        Label {
                            Layout.fillWidth: true
                            small: true; muted: true; wrapMode: Text.WordWrap
                            text: "To talk on this PC, the phone pairs with it over Bluetooth as a headset. Calls you answer or dial here play through this PC; calls you take on the phone stay on the phone."
                        }
                    }
                    Btn {
                        visible: Bt.available && !Bt.pairing
                        text: Bt.phoneState === "unpaired" ? "Pair" : "Pair again"
                        kind: Bt.phoneState === "unpaired" ? "accent" : "soft"
                        enabled: Link.online
                        onClicked: App.pairBluetooth()
                    }
                    Btn { visible: Bt.pairing; text: "Cancel"; kind: "ghost"; onClicked: Bt.endPairing() }
                }
                Separator { Layout.fillWidth: true }
                SwitchRow { key: "call_audio_on_pc"; label: "Use this PC's speakers and mic"; detail: "When you answer or dial from the PC" }
                SwitchRow { key: "pause_media_on_call"; label: "Pause music on this PC during calls"; detail: "Resumes when the call ends" }
                Label { text: "QUICK REPLIES"; small: true; color: Theme.faint; font.letterSpacing: 1.1; Layout.topMargin: 6 }
                Repeater {
                    id: replies
                    model: Settings.read("quick_replies")
                    RowLayout {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        Field {
                            Layout.fillWidth: true
                            text: modelData
                            onEditingFinished: {
                                const list = Settings.read("quick_replies")
                                list[index] = text
                                Settings.write("quick_replies", list.filter(x => x.trim()))
                            }
                        }
                        IconBtn {
                            iconName: "x"; tip: "Remove"
                            onClicked: {
                                const list = Settings.read("quick_replies")
                                list.splice(index, 1)
                                Settings.write("quick_replies", list)
                                replies.model = list
                            }
                        }
                    }
                }
                Btn {
                    compact: true; kind: "ghost"; text: "Add reply"; iconName: "plus"
                    onClicked: {
                        const list = Settings.read("quick_replies")
                        list.push("New reply")
                        Settings.write("quick_replies", list)
                        replies.model = list
                    }
                }
            }
        }

        Card {
            title: "Sync"
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: 12
                SwitchRow { key: "notify_mirror"; label: "Show phone notifications on this PC"; detail: "As desktop popups, with reply and actions" }
                SwitchRow { key: "clipboard_sync"; label: "Share clipboard"; detail: "Text you copy on this PC goes to the phone" }
                SwitchRow {
                    key: "phone_media_on_pc"
                    label: "Play the phone's audio on this PC"
                    detail: "Bluetooth media audio; needs the phone paired"
                    onToggledTo: on => Bt.setMediaAudio(on)
                }
                Row2 { label: "Received files"; value: Settings.read("downloads") || "~/Downloads/Tandem" }
                Repeater {
                    model: (Settings.read("muted_apps") || [])
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        Label { text: "Muted: " + modelData; muted: true; Layout.fillWidth: true }
                        Btn { compact: true; kind: "ghost"; text: "Unmute"; onClicked: { Notifs.muteApp(modelData, false); parent.visible = false } }
                    }
                }
            }
        }

        Card {
            title: "This PC"
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label { text: "Start with the desktop" }
                        Label { small: true; muted: true; text: "Runs in the background so calls and messages arrive even with the window closed" }
                    }
                    Sw { checked: App.autostart; onToggled: App.autostart = checked }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Appearance"; Layout.fillWidth: true }
                    Chip { text: "Dark"; selected: Theme.dark; onClicked: { Theme.dark = true; Settings.write("theme", "dark") } }
                    Chip { text: "Light"; selected: !Theme.dark; onClicked: { Theme.dark = false; Settings.write("theme", "light") } }
                }
                Label {
                    Layout.fillWidth: true
                    small: true; color: Theme.faint; wrapMode: Text.WordWrap
                    text: "Shortcuts: Ctrl+1–6 switch pages, Ctrl+, opens settings, Ctrl+Q quits. From a terminal: tandem --call NUMBER, --sms NUMBER, --toggle hotspot=on, --send FILES, --ring."
                }
            }
        }
    }
}
