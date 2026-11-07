import QtQuick
import Tandem

Text {
    property bool big: false
    color: Theme.text
    font.family: Theme.serif
    font.pixelSize: big ? 34 : 24
    font.weight: Font.Normal
    elide: Text.ElideRight
    textFormat: Text.PlainText
}
