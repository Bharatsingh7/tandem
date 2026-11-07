import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import Qt.labs.platform as P
import Tandem

Window {
    id: win
    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 560
    visible: false
    title: "Tandem"
    color: Theme.bg

    property string page: "home"
    readonly property var pages: [
        { id: "home", label: "Home", icon: "home", src: "HomePage.qml" },
        { id: "messages", label: "Messages", icon: "message", src: "MessagesPage.qml" },
        { id: "calls", label: "Calls", icon: "phone", src: "CallsPage.qml" },
        { id: "contacts", label: "Contacts", icon: "users", src: "ContactsPage.qml" },
        { id: "photos", label: "Photos & files", icon: "image", src: "PhotosPage.qml" },
        { id: "notifications", label: "Notifications", icon: "bell", src: "NotifsPage.qml" },
        { id: "settings", label: "Settings", icon: "sliders", src: "SettingsPage.qml" }
    ]
    property var pendingArgs: ({})

    function go(p, args) {
        if (!pages.some(x => x.id === p)) return
        pendingArgs[p] = args || {}
        page = p
        const l = loaders.itemAt(pages.findIndex(x => x.id === p))
        if (l && l.item && l.item.open) l.item.open(pendingArgs[p])
    }
    function present() { win.show(); win.raise(); win.requestActivate() }

    Component.onCompleted: Theme.dark = Settings.read("theme") !== "light"

    Connections {
        target: App
        function onShowPage(p, args) { if (p) win.go(p, args); win.present() }
        function onToast(t) { toast.show(t) }
        function onShowCallWindow() { callWin.open() }
    }

    Shortcut { sequences: ["Ctrl+1"]; onActivated: win.go("home") }
    Shortcut { sequences: ["Ctrl+2"]; onActivated: win.go("messages") }
    Shortcut { sequences: ["Ctrl+3"]; onActivated: win.go("calls") }
    Shortcut { sequences: ["Ctrl+4"]; onActivated: win.go("contacts") }
    Shortcut { sequences: ["Ctrl+5"]; onActivated: win.go("photos") }
    Shortcut { sequences: ["Ctrl+6"]; onActivated: win.go("notifications") }
    Shortcut { sequences: ["Ctrl+,"]; onActivated: win.go("settings") }
    Shortcut { sequences: ["Ctrl+W", "Escape"]; enabled: win.page === "home"; onActivated: win.close() }
    Shortcut { sequences: ["Ctrl+Q"]; onActivated: App.quit() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 236
            pages: win.pages
            current: win.page
            onNavigate: p => win.go(p)
        }
        Separator { Layout.fillHeight: true; implicitWidth: 1 }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Repeater {
                id: loaders
                model: win.pages
                Loader {
                    required property var modelData
                    readonly property bool shown: win.page === modelData.id
                    property bool visited: false
                    anchors.fill: parent
                    active: visited
                    visible: shown
                    asynchronous: false
                    source: modelData.src
                    opacity: shown ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
                    onShownChanged: if (shown) visited = true
                    Component.onCompleted: if (shown) visited = true
                    onLoaded: if (item.open && win.pendingArgs[modelData.id]) item.open(win.pendingArgs[modelData.id])
                }
            }
        }
    }

    Toast { id: toast }
    CallWindow { id: callWin; transientParent: null }

    // Back-to-call pill when the call window was closed mid-call.
    Btn {
        visible: Calls.inCall && !callWin.visible
        anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 16
        kind: "good"
        iconName: "phone"
        text: "Return to call"
        onClicked: { callWin.open() }
    }

    P.SystemTrayIcon {
        visible: true
        icon.name: "com.cyberwolf.Tandem"
        tooltip: Link.sleeping ? "Tandem · asleep" : Link.online ? "Tandem · " + Link.phoneName : "Tandem · not connected"
        onActivated: reason => { if (win.visible && win.active) win.hide(); else win.present() }
        menu: P.Menu {
            P.MenuItem { text: "Open Tandem"; onTriggered: win.present() }
            P.MenuItem { text: "Dialer"; onTriggered: { win.go("calls"); win.present() } }
            P.MenuItem { text: "Send clipboard to phone"; enabled: Link.online; onTriggered: Clip.sendNow() }
            P.MenuItem { text: "Ring my phone"; enabled: Link.online; onTriggered: Phone.ring(true) }
            P.MenuItem { text: Link.sleeping ? "Wake Tandem" : "Sleep (stop everything)"; onTriggered: Link.setSleeping(!Link.sleeping) }
            P.MenuSeparator {}
            P.MenuItem { text: "Quit"; onTriggered: App.quit() }
        }
    }
}
