import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import Tandem

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 28
        spacing: 14
        RowLayout {
            Layout.leftMargin: 32; Layout.rightMargin: 24
            Heading { text: "Notifications"; Layout.fillWidth: true }
            Btn { text: "Clear all"; kind: "ghost"; enabled: list.count > 0; onClicked: Notifs.dismiss() }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Notifs.model
            spacing: 10
            cacheBuffer: 1000
            bottomMargin: 24
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: Scroller {}
            add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal } }
            remove: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }
            displaced: Transition { NumberAnimation { property: "y"; duration: Theme.normal; easing.type: Easing.OutCubic } }
            delegate: NotifCard { width: Math.min(760, list.width - 64); x: 32 }
            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                muted: true
                text: Link.online ? "All clear" : "Connect your phone to see its notifications"
            }
        }
    }
}
