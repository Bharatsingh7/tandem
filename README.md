# Tandem 🚲⚡
> **Private, Zero-Cloud Phone & Desktop Continuity Suite for Linux & Android**

<div align="center">

[![Kotlin](https://img.shields.io/badge/Android-Kotlin%202.2-7F52FF?style=for-the-badge&logo=kotlin&logoColor=white)](https://kotlinlang.org)
[![Compose](https://img.shields.io/badge/UI-Jetpack%20Compose-4285F4?style=for-the-badge&logo=android&logoColor=white)](https://developer.android.com/jetpack/compose)
[![Qt6](https://img.shields.io/badge/Client-Qt6%20%2F%20QML-41CD52?style=for-the-badge&logo=qt&logoColor=white)](https://qt.io)
[![GTK4](https://img.shields.io/badge/Desktop-GTK4%20%2F%20Libadwaita-3584E4?style=for-the-badge&logo=gnome&logoColor=white)](https://gnome.org)
[![Linux](https://img.shields.io/badge/Platform-Arch%20%2F%20Hyprland-1793D1?style=for-the-badge&logo=archlinux&logoColor=white)](https://archlinux.org)
[![License](https://img.shields.io/badge/License-MIT-blue?style=for-the-badge)](LICENSE)

**A private, local ecosystem that seamlessly unites your Android smartphone with your Linux desktop (Hyprland / Wayland / Arch). Enjoy Apple Continuity & KDE Connect-level features with zero third-party cloud dependencies.**

[✨ Highlights](#-key-features) • [🏗️ Architecture](#️-architecture) • [🚀 Setup & Installation](#-setup--installation) • [📱 Android App](#-android-client) • [💻 Desktop Clients](#-desktop-clients)

</div>

---

## ✨ Key Features

* 🔔 **Interactive Notification Mirroring**: Instant low-latency push of smartphone notifications to your desktop notification daemon with action buttons (Reply, Dismiss, Mark Read).
* 📋 **Bidirectional Real-Time Clipboard**: Copy on Android, paste on Linux; copy on Linux, paste on Android seamlessly over encrypted local sockets.
* 💬 **SMS & Chat Threads**: Browse contact message threads, read incoming texts, and send SMS messages directly from your desktop keyboard.
* 📞 **Call Management & In-Call HUD**: Real-time incoming call alerts, contact caller ID lookup, desktop dialing, call muting, and notification island banners.
* 📁 **Instant Local File Transfers**: Transfer photos, videos, and APKs between devices over high-speed local Wi-Fi without cloud bandwidth limits.
* 🎵 **MPRIS Media Controller**: Synchronized media status; control smartphone music playback from your PC bar or control desktop music from your phone.
* 🔋 **Device Telemetry & Status**: Monitor phone battery percentage, charging states, and cellular signal strength from your desktop bar.
* ⚡ **Quick Remote Toggles**: Toggle phone Wi-Fi, Mobile Data, Personal Hotspot, Bluetooth, Do Not Disturb, Flashlight/Torch, or trigger "Find My Phone" ringer remotely.

---

## 🏗️ Architecture

```
┌─────────────────────────────────┐               ┌─────────────────────────────────┐
│       Android Smartphone        │               │          Linux Desktop          │
│                                 │               │                                 │
│  • LinkService (Foreground)     │   Wi-Fi LAN   │  • Daemon (Python / AsyncIO)    │
│  • NotificationListenerService  │ ────────────> │  • GTK4 / Libadwaita Client     │
│  • Sms & Call Observers         │ <───────────  │  • C++20 / Qt6 / QML Mecha UI   │
│  • Jetpack Compose Shell UI     │   Bluetooth   │  • Hyprland IPC & MPRIS Bridge  │
└─────────────────────────────────┘               └─────────────────────────────────┘
```

Tandem operates strictly over your local Wi-Fi network and Bluetooth. No external servers or cloud accounts are involved.

---

## 🚀 Setup & Installation

### 1. Android Client

Build and install the Android app from the repository:

```bash
cd ~/Projects/tandem/android
./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

Grant requested notification listener and battery optimization permissions when launching the app.

---

### 2. Desktop Clients

Tandem provides two desktop interfaces depending on your desktop workflow:

#### Option A: Native C++20 / Qt6 / QML Client (Recommended for Hyprland / Wayland)

Super-responsive, GPU-accelerated QML client with fluid glass animations:

```bash
cd ~/Projects/tandem/linux
qmake6 tandem.pro
make -j$(nproc)
./install.sh
```

#### Option B: Modern GTK4 / Libadwaita Client

Clean GNOME-style desktop application:

```bash
cd ~/Projects/tandem/desktop
./install.sh
```

Run in background mode on system login:
```bash
python3 -m tandem --background
```

---

## 📱 Android Client

The Android app is built using modern **Kotlin 2.2** and **Jetpack Compose**, featuring:
- Background `LinkService` with automatic reconnection on network change.
- Strict battery-efficient networking with low-overhead frame protocol.
- Encrypted device pairing and token authentication.

---

## 💻 Desktop Clients

* **Dynamic Island / Notch Window**: Compact HUD widget that pops up for incoming calls, transfers, and notifications.
* **Dialer & Messages App**: Full-window phone dialer, contact book editor, and threaded SMS conversation viewer.
* **CLI Control Utility**:
  ```bash
  tandem --ring          # Ring phone to locate it
  tandem --lock          # Remotely lock phone screen
  tandem --clipboard     # Push desktop clipboard to phone
  ```

---

## 📄 License

MIT License. Crafted with care by [Bharat Singh](https://github.com/Bharatsingh7).
