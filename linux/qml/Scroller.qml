import QtQuick
import QtQuick.Templates as T
import Tandem

// Thin overlay scrollbar for Flickables/ListViews.
T.ScrollBar {
    id: s
    implicitWidth: 10
    padding: 2
    policy: T.ScrollBar.AsNeeded
    minimumSize: 0.06
    contentItem: Rectangle {
        implicitWidth: 6
        radius: 3
        color: s.pressed ? Theme.sub : Theme.faint
        opacity: s.active || s.hovered ? 0.8 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    }
}
