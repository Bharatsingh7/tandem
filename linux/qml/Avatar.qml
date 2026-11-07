import QtQuick
import Tandem

// Circular avatar: an image key (image://t/<key>) or initials on a tone derived from the name.
Item {
    id: a
    property string key
    property string name
    property int rev: 0
    property real size: 40
    property string iconName: ""
    implicitWidth: size
    implicitHeight: size

    readonly property var tones: Theme.dark ? ["#4a3b2c", "#2f4038", "#3d3446", "#46332f", "#2e3b47", "#45412b"]
                                            : ["#e8d5bd", "#cfe0d4", "#ddd2e4", "#ebd0c8", "#cfdbe6", "#e4dfc0"]
    function hash(s) { let h = 0; for (let i = 0; i < s.length; ++i) h = (h * 31 + s.charCodeAt(i)) | 0; return Math.abs(h) }
    function initials(s) {
        const w = s.replace(/[^\p{L}\p{N} ]/gu, "").trim().split(/\s+/).filter(x => x.length)
        if (!w.length) return ""
        return (w[0][0] + (w.length > 1 ? w[w.length - 1][0] : "")).toUpperCase()
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: img.status !== Image.Ready
        color: a.tones[a.hash(a.name) % a.tones.length]
        Text {
            anchors.centerIn: parent
            visible: label.length > 0
            property string label: a.iconName ? "" : a.initials(a.name)
            text: label
            color: Theme.text
            font.family: Theme.serifText
            font.pixelSize: Math.round(a.size * 0.38)
        }
        Icon {
            anchors.centerIn: parent
            visible: a.iconName !== "" || a.initials(a.name) === ""
            name: a.iconName || "user"
            size: a.size * 0.45
            color: Theme.sub
        }
    }
    Image {
        id: img
        anchors.fill: parent
        source: a.key ? "image://t/" + a.key + "?round&r=" + a.rev : ""
        sourceSize: Qt.size(a.size, a.size)
        asynchronous: true
        smooth: true
        mipmap: true
        cache: true
    }
}
