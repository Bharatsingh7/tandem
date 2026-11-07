import QtQuick
import QtQuick.Shapes
import Tandem

// Vector line icon from Theme.icons. "phoneOff" is the phone glyph turned on its side.
Item {
    id: root
    property string name
    property color color: Theme.text
    property real size: 18
    property real stroke: 1.7
    property bool filled: false
    implicitWidth: size
    implicitHeight: size

    Shape {
        width: 24; height: 24
        anchors.centerIn: parent
        scale: root.size / 24
        rotation: root.name === "phoneOff" ? 135 : 0
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.stroke * 24 / root.size * (root.name === "keypad" || root.name === "more" ? 1.7 : 1)
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: Theme.icons[root.name === "phoneOff" ? "phone" : root.name] || "" }
        }
    }
}
