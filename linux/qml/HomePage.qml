import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQuick.Dialogs
import Tandem

Item {
    id: home
    readonly property var st: Phone.status
    readonly property var tg: st.toggles || {}
    readonly property var net: st.network || {}
    readonly property var bat: st.battery || {}

    Confirm { id: confirm }
    FileDialog {
        id: picker
        title: "Send files to phone"
        fileMode: FileDialog.OpenFiles
        onAccepted: Transfers.sendFiles(selectedFiles)
    }

    function toggle(name, on) {
        const hot = Phone.pcUsesHotspot()
        if (name === "hotspot" && !on && hot)
            return confirm.ask("Turn off the hotspot?", "This PC is online through your phone's hotspot. Turning it off cuts this PC's internet and disconnects Tandem until USB or Wi-Fi brings it back.", "Turn off", () => Phone.setToggle(name, on), true)
        if (name === "airplane" && on && Link.transport === "wifi")
            return confirm.ask("Turn on airplane mode?", "Tandem is connected over Wi-Fi" + (hot ? " through the phone's hotspot" : "") + ". Airplane mode will disconnect it, and you'll have to turn it off on the phone.", "Turn on", () => Phone.setToggle(name, on), true)
        if (name === "data" && !on && hot)
            return confirm.ask("Turn off mobile data?", "This PC gets its internet through the phone's hotspot, which uses mobile data. Turning data off cuts this PC's internet.", "Turn off", () => Phone.setToggle(name, on), true)
        if (name === "wifi" && !on && Link.transport === "wifi" && !hot)
            return confirm.ask("Turn off Wi-Fi?", "Tandem is connected over Wi-Fi. Turning it off disconnects the phone until USB or Wi-Fi brings it back.", "Turn off", () => Phone.setToggle(name, on), true)
        Phone.setToggle(name, on)
    }

    // ---- not connected ----
    Item {
        anchors.fill: parent
        visible: !Link.online
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(520, parent.width - 80)
            spacing: 14

            Icon { name: Link.sleeping ? "moon" : Link.state === "pairing" ? "link" : "smartphone"; size: 36; stroke: 1.3; color: Theme.accent; Layout.bottomMargin: 6 }
            Heading {
                big: true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: Link.sleeping ? "Tandem is asleep" : Link.state === "pairing" ? "Check your phone" : Link.state === "locked" ? "Allow pairing on your phone" : Link.state === "mismatch" ? "This isn't the phone you paired" : "Connect your phone"
            }
            Label {
                Layout.fillWidth: true
                muted: true
                wrapMode: Text.WordWrap
                lineHeight: 1.3
                text: Link.sleeping ? "The phone is disconnected and nothing runs in the background: no reconnecting, notifications, clipboard sharing or calls. Wake it when you want your phone back."
                    : Link.state === "pairing" ? "Tap Pair on the phone if it shows the same code."
                    : Link.state === "locked" ? "For safety the phone only accepts new PCs while pairing is open. Open Tandem on the phone and tap Allow pairing, or plug it in over USB."
                    : Link.state === "mismatch" ? "The phone answered with a different identity than the one Tandem trusts. If you reinstalled Tandem on the phone, forget it here and pair again."
                    : "Plug it in with USB debugging on, or put both on the same Wi-Fi or the phone's hotspot. Tandem finds it on its own and reconnects whenever it can."
            }
            Text {
                visible: Link.state === "pairing"
                Layout.topMargin: 10
                text: Link.pairCode.slice(0, 3) + " " + Link.pairCode.slice(3)
                color: Theme.text
                font.family: Theme.serif; font.pixelSize: 64; font.letterSpacing: 6
            }
            RowLayout {
                Layout.topMargin: 14
                spacing: 10
                Btn { text: "Wake Tandem"; iconName: "sun"; kind: "accent"; visible: Link.sleeping; onClicked: Link.setSleeping(false) }
                Btn { text: "Try again"; iconName: "refresh"; kind: "accent"; visible: Link.state !== "mismatch" && !Link.sleeping; onClicked: Link.reconnect() }
                Btn { text: "Set up over USB"; iconName: "usb"; visible: Link.state !== "mismatch" && !Link.sleeping; onClicked: Phone.setupOverUsb() }
                Btn {
                    visible: Link.state === "mismatch"
                    text: "Forget and pair again"; kind: "bad"
                    onClicked: Link.forget()
                }
            }
            Label {
                visible: Link.serial !== "" && Link.state !== "pairing"
                Layout.topMargin: 6
                small: true; muted: true
                text: "USB device " + Link.serial + " found"
            }
        }
    }

    // ---- connected ----
    Flickable {
        id: flick
        anchors.fill: parent
        visible: Link.online
        contentHeight: col.implicitHeight + 72
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 4000
        maximumFlickVelocity: 5000
        T.ScrollBar.vertical: Scroller {}

        ColumnLayout {
            id: col
            width: Math.min(1040, flick.width - 72)
            x: (flick.width - width) / 2
            y: 36
            spacing: 22

            // Header
            ColumnLayout {
                spacing: 4
                Label {
                    small: true; muted: true
                    font.letterSpacing: 1.2
                    text: new Date().toLocaleDateString(Qt.locale(), "dddd, d MMMM").toUpperCase()
                }
                Heading { big: true; text: Link.phoneName || home.st.model || "Phone"; Layout.fillWidth: true }
                Label {
                    muted: true
                    text: [home.bat.level !== undefined ? "Battery " + home.bat.level + "%" + (home.bat.charging ? ", charging" : "") : "",
                           [home.net.carrier, home.net.type].filter(x => x).join(" "),
                           "Connected over " + (Link.transport === "usb" ? "USB" : Phone.pcUsesHotspot() ? "its hotspot" : "Wi-Fi"),
                           home.st.storage ? Math.round(home.st.storage.free / 1e9) + " GB free" : ""].filter(x => x).join("   ·   ")
                }
            }

            // Toggles
            GridLayout {
                Layout.fillWidth: true
                columns: width > 820 ? 4 : 2
                columnSpacing: 12
                rowSpacing: 12
                Repeater {
                    model: [
                        { id: "wifi", label: "Wi-Fi", icon: "wifi" },
                        { id: "bluetooth", label: "Bluetooth", icon: "bluetooth" },
                        { id: "hotspot", label: "Hotspot", icon: "hotspot" },
                        { id: "data", label: "Mobile data", icon: "data" },
                        { id: "airplane", label: "Airplane mode", icon: "plane" },
                        { id: "dnd", label: "Do not disturb", icon: "moon" },
                        { id: "location", label: "Location", icon: "location" },
                        { id: "torch", label: "Torch", icon: "torch" }
                    ]
                    delegate: ToggleTile {
                        required property var modelData
                        Layout.fillWidth: true
                        label: modelData.label
                        iconName: modelData.icon
                        on: !!home.tg[modelData.id]
                        busy: Phone.busy === modelData.id
                        enabled: Link.helper || ["dnd", "torch"].includes(modelData.id)
                        detail: modelData.id === "hotspot" && Phone.pcUsesHotspot() ? "This PC uses it"
                              : modelData.id === "bluetooth" && on && home.st.btName ? home.st.btName
                              : modelData.id === "data" && on ? (home.net.type || "")
                              : ""
                        onFlip: home.toggle(modelData.id, !on)
                    }
                }
            }
            Label {
                visible: !Link.helper
                Layout.topMargin: -8
                small: true; muted: true
                text: "Most switches need the phone's helper, which starts over USB. Plug the phone in once after it restarts."
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 820 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12

                NowPlaying { Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1 }

                Card {
                    title: "Sound"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 14
                        RowLayout {
                            spacing: 8
                            Repeater {
                                model: [{ id: "normal", label: "Ring", icon: "bell" }, { id: "vibrate", label: "Vibrate", icon: "vibrate" }, { id: "silent", label: "Silent", icon: "bellOff" }]
                                Btn {
                                    required property var modelData
                                    compact: true
                                    text: modelData.label
                                    iconName: modelData.icon
                                    iconSize: 14
                                    kind: home.st.ringer === modelData.id ? "accent" : "soft"
                                    onClicked: Phone.setRinger(modelData.id)
                                }
                            }
                        }
                        Repeater {
                            model: [{ id: "media", label: "Media" }, { id: "ring", label: "Ringtone" }, { id: "alarm", label: "Alarm" }]
                            RowLayout {
                                id: volRow
                                required property var modelData
                                readonly property var vol: (home.st.volume || {})[modelData.id] || { v: 0, max: 15 }
                                spacing: 12
                                Label { text: volRow.modelData.label; muted: true; Layout.preferredWidth: 70 }
                                Slide {
                                    id: volSlider
                                    Layout.fillWidth: true
                                    from: 0; to: volRow.vol.max; stepSize: 1
                                    value: volRow.vol.v
                                    onMoved: throttle.restart()
                                    Timer { id: throttle; interval: 90; onTriggered: Phone.setVolume(volRow.modelData.id, volSlider.value) }
                                }
                                Label { text: Math.round(volSlider.value); muted: true; small: true; Layout.preferredWidth: 20; horizontalAlignment: Text.AlignRight }
                            }
                        }
                    }
                }
            }

            Card {
                title: "Quick actions"
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 14
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Btn { text: Phone.ringing ? "Stop ringing" : "Ring my phone"; iconName: "ring"; kind: Phone.ringing ? "accent" : "soft"; onClicked: Phone.ring(!Phone.ringing) }
                        Btn { text: "Lock phone"; iconName: "lock"; enabled: Link.helper; onClicked: Phone.lock() }
                        Btn { text: "Send clipboard"; iconName: "clipboard"; onClicked: { const t = Clip.sendNow(); App.toast(t ? "Clipboard sent to phone" : "Clipboard is empty") } }
                        Btn { text: "Send files…"; iconName: "upload"; onClicked: picker.open() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Field {
                            id: url
                            Layout.fillWidth: true
                            iconName: "link"
                            placeholderText: "Open a link on the phone"
                            onAccepted: go.clicked()
                        }
                        Btn {
                            id: go
                            text: "Open"
                            enabled: url.text.trim() !== ""
                            onClicked: { Phone.openUrl(url.text.trim()); url.clear() }
                        }
                    }
                    Label {
                        visible: Transfers.progressName !== ""
                        small: true; muted: true
                        text: Transfers.progressName + " — " + Math.round(Transfers.progress * 100) + "%"
                    }
                }
            }

            Card {
                visible: Notifs.model.count > 0
                title: "Latest on the phone"
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 2
                    ListView {
                        Layout.fillWidth: true
                        implicitHeight: Math.min(4, count) * 48
                        interactive: false
                        clip: true
                        model: Notifs.model
                        delegate: RowLayout {
                            required property string app
                            required property string title
                            required property string text
                            required property string avatar
                            required property string icon
                            width: ListView.view.width
                            height: 48
                            spacing: 12
                            Avatar { size: 32; key: avatar || icon; name: app; iconName: avatar || icon ? "" : "bell" }
                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Label { text: title || app; font.weight: Font.Medium; Layout.fillWidth: true }
                                Label { text: parent.parent.text; muted: true; small: true; Layout.fillWidth: true }
                            }
                            Label { text: app; small: true; muted: true }
                        }
                    }
                    Btn { Layout.topMargin: 8; text: "All notifications"; kind: "ghost"; compact: true; onClicked: App.showPage("notifications", {}) }
                }
            }
        }
    }
}
