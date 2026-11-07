import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Tandem

// Modal question. Call ask(title, body, confirmLabel, action[, danger]).
T.Popup {
    id: d
    property string heading
    property string body
    property string confirmText: "Continue"
    property bool danger: false
    property var action: null
    function ask(h, b, c, fn, dangerous) { heading = h; body = b; confirmText = c; action = fn; danger = !!dangerous; open() }

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: 420
    modal: true
    padding: 24
    T.Overlay.modal: Rectangle { color: Theme.scrim }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }
    background: Rectangle { radius: 16; color: Theme.raised; border.width: 1; border.color: Theme.line }
    contentItem: ColumnLayout {
        spacing: 12
        Heading { text: d.heading; font.pixelSize: 22; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Label { text: d.body; muted: true; wrapMode: Text.WordWrap; Layout.fillWidth: true; lineHeight: 1.25 }
        RowLayout {
            Layout.topMargin: 10
            Item { Layout.fillWidth: true }
            Btn { text: "Cancel"; kind: "ghost"; onClicked: d.close() }
            Btn {
                text: d.confirmText; kind: d.danger ? "bad" : "accent"
                onClicked: { d.close(); if (d.action) d.action() }
            }
        }
    }
}
