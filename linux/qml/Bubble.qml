import QtQuick
import QtQuick.Layouts
import Tandem

// One message; `day` is set only on the first message of each day.
Item {
    id: b
    required property int index
    required property string text
    required property var time
    required property bool self
    required property string sender
    required property string status
    required property string day
    readonly property bool firstOfDay: day !== ""
    readonly property bool showSender: !self && sender !== "" && !!Messages.current.group
    height: col.implicitHeight

    Column {
        id: col
        width: parent.width
        spacing: 4
        Text {
            visible: b.firstOfDay
            width: parent.width
            topPadding: 14; bottomPadding: 8
            horizontalAlignment: Text.AlignHCenter
            text: b.day
            color: Theme.faint
            font.family: Theme.sans; font.pixelSize: 11; font.letterSpacing: 0.8; font.weight: Font.DemiBold
        }
        Item {
            width: parent.width
            height: bubble.height
            Rectangle {
                id: bubble
                x: b.self ? parent.width - width - 24 : 24
                width: Math.min(body.implicitWidth, parent.width * 0.66) + 28
                height: body.implicitHeight + meta.implicitHeight + (b.showSender ? who.implicitHeight : 0) + 20
                radius: 16
                color: b.self ? Theme.accentSoft : Theme.surface
                border.width: b.self ? 0 : 1
                border.color: Theme.line
                Text {
                    id: who
                    visible: b.showSender
                    x: 14; y: 9
                    text: b.sender
                    color: Theme.accent
                    font.family: Theme.sans; font.pixelSize: 12; font.weight: Font.DemiBold
                }
                TextEdit {
                    id: body
                    x: 14; y: b.showSender ? who.y + who.implicitHeight + 2 : 9
                    width: Math.min(implicitWidth, b.width * 0.66)
                    text: b.text
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: Theme.text
                    selectionColor: Theme.accentSoft
                    font.family: Theme.sans; font.pixelSize: 14
                    textFormat: TextEdit.PlainText
                }
                Text {
                    id: meta
                    anchors.right: parent.right; anchors.rightMargin: 14
                    anchors.top: body.bottom; anchors.topMargin: 2
                    text: Theme.clock(b.time) + (b.status === "failed" ? "  ·  not sent" : b.status === "sending" ? "  ·  sending" : "")
                    color: b.status === "failed" ? Theme.bad : Theme.faint
                    font.family: Theme.sans; font.pixelSize: 11
                }
            }
        }
    }
}
