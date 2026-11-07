import QtQuick
import QtQuick.Layouts
import Tandem

Card {
    id: np
    title: "Now playing"
    readonly property var m: Phone.media
    readonly property bool playing: m.state === "playing"
    property real now: Date.now()
    readonly property real pos: !m.active ? 0 : Math.min(m.duration || 0, (m.position || 0) + (playing ? (now - Phone.mediaAt) * (m.speed || 1) : 0))

    function fmt(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    Timer { interval: 500; repeat: true; running: np.playing && np.visible; onTriggered: np.now = Date.now() }

    RowLayout {
        width: parent.width
        spacing: 16
        visible: !!np.m.active
        Rectangle {
            Layout.preferredWidth: 112
            Layout.preferredHeight: 112
            radius: Theme.radiusSmall
            color: Theme.raised
            clip: true
            Icon { anchors.centerIn: parent; name: "volume"; size: 28; color: Theme.faint; visible: art.status !== Image.Ready }
            Image {
                id: art
                anchors.fill: parent
                source: Phone.artRev ? "image://t/art?" + Phone.artRev : ""
                sourceSize: Qt.size(224, 224)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text {
                Layout.fillWidth: true
                text: np.m.title || "Untitled"
                textFormat: Text.PlainText
                color: Theme.text
                font.family: Theme.serifText; font.pixelSize: 18
                elide: Text.ElideRight
            }
            Label { Layout.fillWidth: true; muted: true; text: [np.m.artist, np.m.album].filter(x => x).join(" — ") }
            Label { small: true; color: Theme.faint; text: np.m.app || "" }
            Item { Layout.fillHeight: true; implicitHeight: 6 }
            Slide {
                id: seek
                Layout.fillWidth: true
                visible: (np.m.duration || 0) > 0
                enabled: !!np.m.canSeek
                from: 0; to: np.m.duration || 1
                Binding on value { when: !seek.pressed; value: np.pos }
                onPressedChanged: if (!pressed) Phone.mediaCmd("seek", value)
            }
            RowLayout {
                Layout.fillWidth: true
                Label { small: true; muted: true; text: np.m.duration ? np.fmt(np.pos) + " / " + np.fmt(np.m.duration) : "" }
                Item { Layout.fillWidth: true }
                IconBtn { iconName: "prev"; tip: "Previous"; enabled: np.m.canPrev !== false; onClicked: Phone.mediaCmd("prev") }
                IconBtn {
                    iconName: np.playing ? "pause" : "play"
                    tip: np.playing ? "Pause" : "Play"
                    size: 40
                    tint: Theme.onAccent
                    fill: Theme.accent
                    onClicked: Phone.mediaCmd(np.playing ? "pause" : "play")
                }
                IconBtn { iconName: "next"; tip: "Next"; enabled: np.m.canNext !== false; onClicked: Phone.mediaCmd("next") }
            }
        }
    }
    Label {
        visible: !np.m.active
        width: parent.width
        muted: true
        wrapMode: Text.WordWrap
        text: "Nothing is playing on the phone. Music, podcasts and videos show up here with their controls."
    }
}
