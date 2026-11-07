import QtQuick
import Tandem

Text {
    property bool muted: false
    property bool small: false
    color: muted ? Theme.sub : Theme.text
    font.family: Theme.sans
    font.pixelSize: small ? 12 : 14
    elide: Text.ElideRight
    wrapMode: Text.NoWrap
    textFormat: Text.PlainText
}
