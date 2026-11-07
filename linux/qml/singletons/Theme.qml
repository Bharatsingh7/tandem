pragma Singleton
import QtQuick

QtObject {
    property bool dark: true

    readonly property color bg:      dark ? "#141210" : "#f3eee5"
    readonly property color side:    dark ? "#191613" : "#ebe5da"
    readonly property color surface: dark ? "#1f1b17" : "#faf7f1"
    readonly property color raised:  dark ? "#28231e" : "#ffffff"
    readonly property color hover:   dark ? "#2f2a24" : "#efe8dc"
    readonly property color line:    dark ? "#332d26" : "#ddd4c5"
    readonly property color text:    dark ? "#eee8de" : "#221d17"
    readonly property color sub:     dark ? "#a59b8d" : "#6b6155"
    readonly property color faint:   dark ? "#6e655a" : "#9d9284"
    readonly property color accent:  dark ? "#d8a25e" : "#a2652a"
    readonly property color accentSoft: Qt.alpha(accent, dark ? 0.16 : 0.12)
    readonly property color onAccent: dark ? "#1b140b" : "#fffaf2"
    readonly property color good:    dark ? "#62a977" : "#3c8752"
    readonly property color bad:     dark ? "#d0604f" : "#b4463a"
    readonly property color scrim:   Qt.rgba(0, 0, 0, dark ? 0.55 : 0.3)

    readonly property string serif: "Noto Serif Display"
    readonly property string serifText: "Noto Serif"
    readonly property string sans: "Noto Sans"
    readonly property string mono: "Noto Sans Mono"

    readonly property int radius: 12
    readonly property int radiusSmall: 8
    readonly property int fast: 120
    readonly property int normal: 200

    function clock(ms) { return new Date(ms).toLocaleTimeString(Qt.locale(), "HH:mm") }
    function when(ms) {
        if (!ms) return ""
        const d = new Date(ms), now = new Date()
        const days = Math.floor((new Date(now.getFullYear(), now.getMonth(), now.getDate()) - new Date(d.getFullYear(), d.getMonth(), d.getDate())) / 86400000)
        if (days === 0) return clock(ms)
        if (days === 1) return "Yesterday"
        if (days < 7) return d.toLocaleDateString(Qt.locale(), "dddd")
        return d.toLocaleDateString(Qt.locale(), d.getFullYear() === now.getFullYear() ? "d MMM" : "d MMM yyyy")
    }
    function duration(sec) {
        sec = Math.max(0, Math.floor(sec))
        const h = Math.floor(sec / 3600), m = Math.floor(sec % 3600 / 60), s = sec % 60
        return (h ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(s).padStart(2, "0")
    }
    function appTint(pkg) { return pkg === "com.whatsapp" || pkg === "com.whatsapp.w4b" ? good : accent }

    // Line icons, 24×24 grid, stroked.
    readonly property var icons: ({
        home: "M3 10.5 12 3l9 7.5V20a1 1 0 0 1-1 1h-5v-6h-6v6H4a1 1 0 0 1-1-1z",
        message: "M21 11.5a8.4 8.4 0 0 1-12.3 7.4L3 20.5l1.6-5.2A8.5 8.5 0 1 1 21 11.5z",
        phone: "M22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.5 19.5 0 0 1-6-6 19.79 19.79 0 0 1-3.07-8.67A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72 12.84 12.84 0 0 0 .7 2.81 2 2 0 0 1-.45 2.11L8.09 9.91a16 16 0 0 0 6 6l1.27-1.27a2 2 0 0 1 2.11-.45 12.84 12.84 0 0 0 2.81.7A2 2 0 0 1 22 16.92z",
        users: "M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2 M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M22 21v-2a4 4 0 0 0-3-3.87 M16 3.13a4 4 0 0 1 0 7.75",
        user: "M19 21v-2a4 4 0 0 0-4-4H9a4 4 0 0 0-4 4v2 M12 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z",
        image: "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z M9 10.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3z M21 15l-5-5L5 21",
        bell: "M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9 M10.3 21a1.94 1.94 0 0 0 3.4 0",
        bellOff: "M8.7 3A6 6 0 0 1 18 8a21.3 21.3 0 0 0 .6 5 M17 17H3s3-2 3-9a4.67 4.67 0 0 1 .3-1.7 M10.3 21a1.94 1.94 0 0 0 3.4 0 M2 2l20 20",
        sliders: "M4 21v-7 M4 10V3 M12 21v-9 M12 8V3 M20 21v-5 M20 12V3 M1 14h6 M9 8h6 M17 16h6",
        bluetooth: "M7 7l10 10-5 5V2l5 5L7 17",
        wifi: "M5 12.55a11 11 0 0 1 14.08 0 M1.42 9a16 16 0 0 1 21.16 0 M8.53 16.11a6 6 0 0 1 6.95 0 M12 20h.01",
        hotspot: "M4.9 19.1C1 15.2 1 8.8 4.9 4.9 M7.8 16.2c-2.3-2.3-2.3-6.1 0-8.5 M16.2 7.8c2.3 2.3 2.3 6.1 0 8.5 M19.1 4.9C23 8.8 23 15.1 19.1 19 M12 13a1 1 0 1 0 0-2 1 1 0 0 0 0 2z",
        data: "M7 3v18 M3 7l4-4 4 4 M17 21V3 M21 17l-4 4-4-4",
        plane: "M17.8 19.2 16 11l3.5-3.5C21 6 21.5 4 21 3c-1-.5-3 0-4.5 1.5L13 8 4.8 6.2c-.5-.1-.9.1-1.1.5l-.3.5c-.2.5-.1 1 .3 1.3L9 12l-2 3H4l-1 1 3 2 2 3 1-1v-3l3-2 3.5 5.3c.3.4.8.5 1.3.3l.5-.2c.4-.3.6-.7.5-1.2z",
        moon: "M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9z",
        location: "M20 10c0 6-8 12-8 12s-8-6-8-12a8 8 0 0 1 16 0z M12 13a3 3 0 1 0 0-6 3 3 0 0 0 0 6z",
        torch: "M18 6c0 2-2 2-2 4v10a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2V10c0-2-2-2-2-4V2h12z M6 6h12 M12 12v1",
        volume: "M11 5 6 9H2v6h4l5 4z M15.54 8.46a5 5 0 0 1 0 7.07 M19.07 4.93a10 10 0 0 1 0 14.14",
        vibrate: "M2 8l2 2-2 2 2 2-2 2 M22 8l-2 2 2 2-2 2 2 2 M9 5h6a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H9a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z",
        lock: "M5 11h14a2 2 0 0 1 2 2v7a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a2 2 0 0 1 2-2z M7 11V7a5 5 0 0 1 10 0v4",
        search: "M11 19a8 8 0 1 0 0-16 8 8 0 0 0 0 16z M21 21l-4.3-4.3",
        send: "M22 2 11 13 M22 2l-7 20-4-9-9-4z",
        x: "M18 6 6 18 M6 6l12 12",
        check: "M20 6 9 17l-5-5",
        mic: "M12 2a3 3 0 0 0-3 3v7a3 3 0 0 0 6 0V5a3 3 0 0 0-3-3z M19 10v2a7 7 0 0 1-14 0v-2 M12 19v3",
        micOff: "M2 2l20 20 M18.89 13.23A7.12 7.12 0 0 0 19 12v-2 M5 10v2a7 7 0 0 0 12 5 M15 9.34V5a3 3 0 0 0-5.68-1.33 M9 9v3a3 3 0 0 0 5.12 2.12 M12 19v3",
        pause: "M7 4h3v16H7z M14 4h3v16h-3z",
        play: "M7 4l13 8-13 8z",
        next: "M5 4l10 8-10 8z M19 5v14",
        prev: "M19 20 9 12l10-8z M5 19V5",
        keypad: "M6 4.5h.01 M12 4.5h.01 M18 4.5h.01 M6 10.5h.01 M12 10.5h.01 M18 10.5h.01 M6 16.5h.01 M12 16.5h.01 M18 16.5h.01 M12 22h.01",
        callIn: "M17 7 7 17 M16 17H7V8",
        callOut: "M7 17 17 7 M8 7h9v9",
        star: "M12 2.5l2.94 5.96 6.56.95-4.75 4.63 1.12 6.54L12 17.5l-5.87 3.08 1.12-6.54L2.5 9.41l6.56-.95z",
        plus: "M12 5v14 M5 12h14",
        trash: "M3 6h18 M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6 M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2",
        edit: "M12 20h9 M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z",
        download: "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4 M7 10l5 5 5-5 M12 15V3",
        upload: "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4 M17 8l-5-5-5 5 M12 3v12",
        refresh: "M21 12a9 9 0 0 1-15.5 6.2L3 16 M3 12a9 9 0 0 1 15.5-6.2L21 8 M21 3v5h-5 M3 21v-5h5",
        link: "M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71 M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71",
        clipboard: "M9 2h6a1 1 0 0 1 1 1v2a1 1 0 0 1-1 1H9a1 1 0 0 1-1-1V3a1 1 0 0 1 1-1z M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2",
        smartphone: "M7 2h10a2 2 0 0 1 2 2v16a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2z M12 18h.01",
        monitor: "M4 3h16a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z M8 21h8 M12 17v4",
        back: "M19 12H5 M12 19l-7-7 7-7",
        more: "M12 12.5a.5.5 0 1 0 0-1 .5.5 0 0 0 0 1z M19 12.5a.5.5 0 1 0 0-1 .5.5 0 0 0 0 1z M5 12.5a.5.5 0 1 0 0-1 .5.5 0 0 0 0 1z",
        merge: "M8 6l4-4 4 4 M12 2v10.3a4 4 0 0 1-1.17 2.87L4 22 M20 22l-5-5",
        swap: "M16 3l4 4-4 4 M20 7H4 M8 21l-4-4 4-4 M4 17h16",
        folder: "M4 20h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.93a2 2 0 0 1-1.66-.9l-.82-1.2A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13c0 1.1.9 2 2 2z",
        sun: "M12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M12 2v2 M12 20v2 M4.93 4.93l1.41 1.41 M17.66 17.66l1.41 1.41 M2 12h2 M20 12h2 M6.34 17.66l-1.41 1.41 M19.07 4.93l-1.41 1.41",
        backspace: "M21 4H8l-7 8 7 8h13a2 2 0 0 0 2-2V6a2 2 0 0 0-2-2z M18 9l-6 6 M12 9l6 6",
        chevron: "M9 18l6-6-6-6",
        external: "M15 3h6v6 M10 14 21 3 M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6",
        ring: "M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9 M10.3 21a1.94 1.94 0 0 0 3.4 0 M2 8c0-2.2.7-4.3 2-6 M22 8a10 10 0 0 0-2-6",
        battery: "M4 7h13a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2z M22 11v2",
        usb: "M12 2v14 M8 6l4-4 4 4 M7 10v2a5 5 0 0 0 10 0v-2 M12 16a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5z",
        upDown: "M7 15l5 5 5-5 M7 9l5-5 5 5"
    })
}
