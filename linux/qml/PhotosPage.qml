import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Templates as T
import Tandem

Item {
    id: pg
    FileDialog {
        id: picker
        title: "Send files to phone"
        fileMode: FileDialog.OpenFiles
        onAccepted: Transfers.sendFiles(selectedFiles)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 28
        spacing: 14

        RowLayout {
            Layout.leftMargin: 32; Layout.rightMargin: 24
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Heading { text: "Photos & files" }
                Label { muted: true; small: true; text: "Recent photos and videos from the phone. Click to save to Pictures/Tandem. Drop files anywhere here to send them." }
            }
            IconBtn { iconName: "refresh"; tip: "Reload"; onClicked: Transfers.reload() }
            Btn { text: "Send files…"; iconName: "upload"; kind: "accent"; onClicked: picker.open() }
        }

        Rectangle {
            visible: Transfers.progressName !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 32; Layout.rightMargin: 32
            implicitHeight: 44
            radius: Theme.radiusSmall
            color: Theme.surface
            border.width: 1; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 12
                spacing: 12
                Label { text: Transfers.progressName; Layout.preferredWidth: 260 }
                Rectangle {
                    Layout.fillWidth: true
                    height: 4; radius: 2
                    color: Theme.line
                    Rectangle { width: parent.width * Transfers.progress; height: 4; radius: 2; color: Theme.accent; Behavior on width { NumberAnimation { duration: 150 } } }
                }
                Label { text: Math.round(Transfers.progress * 100) + "%"; small: true; muted: true }
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 28
            clip: true
            model: Transfers.photos
            readonly property int cols: Math.max(3, Math.floor((width - 8) / 170))
            cellWidth: Math.floor((width - 8) / cols)
            cellHeight: cellWidth
            cacheBuffer: 1200
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: Scroller {}
            onAtYEndChanged: if (atYEnd && count > 0) Transfers.loadMore()
            delegate: T.AbstractButton {
                id: cell
                required property int index
                required property string name
                required property bool video
                required property string thumb
                required property var date
                width: grid.cellWidth; height: grid.cellHeight
                hoverEnabled: true
                onClicked: Transfers.download(index)
                contentItem: Item {
                    Rectangle {
                        anchors.fill: parent; anchors.margins: 4
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: cell.thumb ? "image://t/" + cell.thumb : ""
                            sourceSize: Qt.size(grid.cellWidth, grid.cellHeight)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            opacity: status === Image.Ready ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.normal } }
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.scrim
                            opacity: cell.hovered ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                            Icon { anchors.centerIn: parent; name: "download"; size: 24; color: "white" }
                            Text {
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 10 }
                                text: cell.name
                                color: "white"; elide: Text.ElideMiddle
                                font.family: Theme.sans; font.pixelSize: 11
                            }
                        }
                        Rectangle {
                            visible: cell.video
                            anchors { right: parent.right; top: parent.top; margins: 8 }
                            width: 26; height: 26; radius: 13
                            color: Qt.rgba(0, 0, 0, 0.55)
                            Icon { anchors.centerIn: parent; name: "play"; size: 12; color: "white"; filled: true }
                        }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: grid.count === 0
                muted: true
                text: Transfers.loading ? "Loading…" : Link.online ? "No photos found" : "Connect your phone to browse photos"
            }
        }
    }

    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: d => { if (d.hasUrls) { Transfers.sendFiles(d.urls); d.acceptProposedAction() } }
        Rectangle {
            anchors.fill: parent; anchors.margins: 16
            radius: 18
            visible: drop.containsDrag
            color: Qt.alpha(Theme.bg, 0.85)
            border.width: 2; border.color: Theme.accent
            ColumnLayout {
                anchors.centerIn: parent
                Icon { name: "upload"; size: 40; color: Theme.accent; Layout.alignment: Qt.AlignHCenter }
                Heading { text: "Drop to send to your phone" }
            }
        }
    }
}
