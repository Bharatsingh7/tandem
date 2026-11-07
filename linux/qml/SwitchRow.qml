import QtQuick
import QtQuick.Layouts
import Tandem

RowLayout {
    id: r
    property string key
    property string label
    property string detail
    signal toggledTo(bool on)
    Layout.fillWidth: true
    spacing: 16
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 1
        Label { text: r.label }
        Label { text: r.detail; small: true; muted: true; visible: r.detail !== ""; Layout.fillWidth: true }
    }
    Sw {
        checked: !!Settings.read(r.key)
        onToggled: { Settings.write(r.key, checked); r.toggledTo(checked) }
    }
}
