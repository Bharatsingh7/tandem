import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Templates as T
import Tandem

// Floating call window. Fixed size so tiling compositors float it.
Window {
    id: w
    width: 360; height: 620
    minimumWidth: width; maximumWidth: width
    minimumHeight: height; maximumHeight: height
    flags: Qt.Dialog
    title: "Call · Tandem"
    color: Theme.bg

    readonly property var calls: Calls.calls
    readonly property var primary: {
        for (const s of ["ringing", "active", "dialing", "select-sim", "holding"])
            for (const c of calls) if (c.state === s) return c
        return calls.length ? calls[0] : lastCall
    }
    readonly property var others: calls.filter(c => c.id !== primary.id)
    property var lastCall: ({})
    property bool dismissed: false
    property bool userOpened: false
    property bool keypad: false
    property string typed: ""
    property real now: Date.now()
    readonly property var audio: Calls.audio
    readonly property bool ringing: primary.state === "ringing"

    function open() {
        dismissed = false
        userOpened = true
        w.show()
        w.raise()
        w.requestActivate()
    }
    function hideWindow() {
        dismissed = true
        userOpened = false
        w.hide()
    }

    onCallsChanged: {
        if (calls.length) lastCall = primary
        else { keypad = false; typed = ""; userOpened = false }
    }
    Connections {
        target: Calls
        function onCallsChanged() {
            if (!Calls.inCall && !Calls.justEnded) {
                w.dismissed = false
                w.userOpened = false
            }
        }
    }
    visible: (Calls.inCall || Calls.justEnded) && userOpened && !dismissed
    onVisibleChanged: if (visible) { raise(); requestActivate() }
    onClosing: close => { close.accepted = false; w.hideWindow() }

    Timer { interval: 1000; repeat: true; running: w.visible; triggeredOnStart: true; onTriggered: w.now = Date.now() }

    function statusText() {
        if (!Calls.inCall) return "Call ended"
        switch (primary.state) {
        case "ringing": return "Incoming call"
        case "dialing": return "Calling…"
        case "holding": return "On hold"
        case "select-sim": return "Choose a SIM on the phone"
        case "active": return primary.since > 0 ? Theme.duration((now - primary.since) / 1000) : "On call"
        }
        return ""
    }

    Shortcut { sequences: ["Return", "Enter"]; enabled: w.ringing; onActivated: Calls.answer(w.primary.id) }
    Shortcut { sequences: ["Escape"]; onActivated: w.ringing ? Calls.reject(w.primary.id) : (w.keypad = false) }

    // soft glow behind the caller
    Rectangle {
        width: 420; height: 420; radius: 210
        x: (parent.width - width) / 2; y: -170
        color: w.ringing ? Theme.good : Theme.accent
        opacity: 0.07
        SequentialAnimation on scale {
            running: w.ringing && w.visible; loops: Animation.Infinite
            NumberAnimation { from: 1; to: 1.08; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.08; to: 1; duration: 900; easing.type: Easing.InOutSine }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 0

        Label {
            Layout.alignment: Qt.AlignHCenter
            small: true
            font.letterSpacing: 1.2
            color: w.ringing ? Theme.good : Theme.sub
            text: w.statusText().toUpperCase()
        }
        Avatar {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 22
            size: 104
            key: w.primary.hasPhoto ? "contact/" + w.primary.cid : ""
            name: w.primary.title || ""
            rev: Contacts.photoRev
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: 16
            horizontalAlignment: Text.AlignHCenter
            text: w.primary.title || ""
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.serif; font.pixelSize: 28
            elide: Text.ElideRight
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            muted: true
            text: w.primary.subtitle || ""
            visible: text !== ""
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            small: true
            visible: Calls.inCall && !w.ringing
            color: w.audio.onPc ? Theme.good : Theme.faint
            text: w.audio.onPc ? "Audio on this PC" : { speaker: "Audio on phone speaker", bluetooth: "Audio on a Bluetooth device", wired: "Audio on wired headset" }[w.audio.route] || "Audio on phone"
        }

        // other calls (held / waiting)
        Repeater {
            model: w.others
            Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.topMargin: 12
                implicitHeight: 48
                radius: Theme.radiusSmall
                color: Theme.surface
                border.width: 1; border.color: Theme.line
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 6
                    Label { text: modelData.title; Layout.fillWidth: true }
                    Label { text: modelData.state === "holding" ? "on hold" : modelData.state; small: true; muted: true }
                    IconBtn { iconName: "swap"; tip: "Swap"; size: 30; visible: modelData.state === "holding"; onClicked: Calls.swap() }
                    IconBtn { iconName: "merge"; tip: "Merge"; size: 30; visible: w.primary.state === "active"; onClicked: Calls.merge(w.primary.id) }
                    IconBtn { iconName: "phone"; tip: "Answer"; size: 30; tint: Theme.good; visible: modelData.state === "ringing"; onClicked: Calls.answer(modelData.id) }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // ---- ringing ----
        ColumnLayout {
            visible: w.ringing
            Layout.fillWidth: true
            spacing: 18
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: w.ringing ? (Settings.read("quick_replies") || []) : []
                    Chip { required property string modelData; text: modelData; onClicked: Calls.reject(w.primary.id, modelData) }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                CallButton { label: "Decline"; iconName: "phoneOff"; tone: Theme.bad; onClicked: Calls.reject(w.primary.id) }
                Item { Layout.fillWidth: true }
                CallButton { label: Settings.read("call_audio_on_pc") && Bt.phoneState !== "unpaired" ? "Answer here" : "Answer"; iconName: "phone"; tone: Theme.good; pulse: true; onClicked: Calls.answer(w.primary.id) }
            }
        }

        // ---- in call ----
        ColumnLayout {
            visible: Calls.inCall && !w.ringing
            Layout.fillWidth: true
            spacing: 22

            Item {
                visible: w.keypad
                Layout.fillWidth: true
                implicitHeight: kp.implicitHeight + 34
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: w.typed || " "
                    color: Theme.text
                    font.family: Theme.serif; font.pixelSize: 22
                    elide: Text.ElideLeft
                }
                Grid {
                    id: kp
                    y: 34
                    anchors.horizontalCenter: parent.horizontalCenter
                    columns: 3; columnSpacing: 10; rowSpacing: 2
                    Repeater {
                        model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"]
                        Key { required property string modelData; digit: modelData; implicitHeight: 48; onClicked: { w.typed += digit; Calls.dtmf(digit, w.primary.id) } }
                    }
                }
            }

            Grid {
                visible: !w.keypad
                Layout.alignment: Qt.AlignHCenter
                columns: 3
                columnSpacing: 22
                rowSpacing: 16
                ControlButton { label: w.audio.muted ? "Unmute" : "Mute"; iconName: w.audio.muted ? "micOff" : "mic"; on: !!w.audio.muted; onClicked: Calls.mute(!w.audio.muted) }
                ControlButton { label: "Keypad"; iconName: "keypad"; onClicked: w.keypad = true }
                ControlButton { label: "Audio"; iconName: w.audio.onPc ? "monitor" : w.audio.route === "speaker" ? "volume" : "smartphone"; onClicked: routes.open() }
                ControlButton {
                    label: w.primary.state === "holding" ? "Resume" : "Hold"
                    iconName: w.primary.state === "holding" ? "play" : "pause"
                    on: w.primary.state === "holding"
                    enabled: !!w.primary.canHold || w.primary.state === "holding"
                    onClicked: Calls.hold(w.primary.id, w.primary.state !== "holding")
                }
                ControlButton { label: "Swap"; iconName: "swap"; enabled: w.others.length > 0; onClicked: Calls.swap() }
                ControlButton { label: "Message"; iconName: "message"; enabled: !!w.primary.number; onClicked: { Messages.openNumber(w.primary.number); App.showPage("messages", {}) } }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CallButton { label: w.keypad ? "" : "End call"; iconName: "phoneOff"; tone: Theme.bad; onClicked: Calls.end(w.primary.id) }
                Item { Layout.fillWidth: true }
            }
            Btn { visible: w.keypad; Layout.alignment: Qt.AlignHCenter; text: "Hide keypad"; kind: "ghost"; compact: true; onClicked: w.keypad = false }
        }

        Label {
            visible: !Calls.inCall
            Layout.alignment: Qt.AlignHCenter
            muted: true
            text: "Call ended"
        }
        Label {
            visible: Calls.inCall && w.primary.id === 0
            Layout.fillWidth: true
            Layout.topMargin: 12
            small: true; color: Theme.accent; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
            text: "Limited call control. Plug the phone in and use Settings → Set up over USB."
        }
    }

    T.Popup {
        id: routes
        anchors.centerIn: parent
        width: 260
        modal: true
        padding: 8
        T.Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle { radius: 14; color: Theme.raised; border.width: 1; border.color: Theme.line }
        contentItem: Column {
            spacing: 2
            Repeater {
                model: [
                    { id: "pc", label: "This PC", icon: "monitor", show: Bt.phoneState !== "unpaired" || !!w.audio.pcAvailable },
                    { id: "earpiece", label: "Phone", icon: "smartphone", show: true },
                    { id: "speaker", label: "Phone speaker", icon: "volume", show: true }
                ].filter(r => r.show)
                T.ItemDelegate {
                    required property var modelData
                    readonly property bool current: modelData.id === "pc" ? !!w.audio.onPc : !w.audio.onPc && w.audio.route === modelData.id
                    width: 244; height: 44
                    hoverEnabled: true
                    background: Rectangle { radius: 10; color: parent.hovered ? Theme.hover : "transparent" }
                    contentItem: RowLayout {
                        spacing: 12
                        Icon { Layout.leftMargin: 12; name: modelData.icon; size: 17; color: current ? Theme.accent : Theme.sub }
                        Label { text: modelData.label; Layout.fillWidth: true; color: current ? Theme.accent : Theme.text }
                        Icon { Layout.rightMargin: 12; name: "check"; size: 15; color: Theme.accent; visible: current }
                    }
                    onClicked: { Calls.route(modelData.id, w.primary.id); routes.close() }
                }
            }
        }
    }
}
