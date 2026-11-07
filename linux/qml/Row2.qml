import QtQuick
import QtQuick.Layouts
import Tandem

RowLayout {
    property string label
    property string value
    property bool mono: false
    Layout.fillWidth: true
    spacing: 16
    Label { text: parent.label; muted: true; Layout.preferredWidth: 130 }
    TextEdit {
        Layout.fillWidth: true
        text: parent.value
        readOnly: true; selectByMouse: true
        wrapMode: TextEdit.Wrap
        color: Theme.text
        selectionColor: Theme.accentSoft
        font.family: parent.mono ? Theme.mono : Theme.sans
        font.pixelSize: parent.mono ? 12 : 14
    }
}
