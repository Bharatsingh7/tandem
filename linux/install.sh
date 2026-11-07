#!/bin/sh
# Build Tandem and install it for the current user: command, desktop entry, tel:/sms: handler.
# Autostart is opt-in: Settings → "Start with the desktop".
set -e
cd "$(dirname "$0")"
mkdir -p build && (cd build && qmake6 ../tandem.pro >/dev/null && make -j"$(nproc)")
BIN="$HOME/.local/bin/tandem"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/scalable/apps"
ln -sf "$PWD/build/tandem" "$BIN"
cp ../desktop/data/com.cyberwolf.Tandem.svg "$HOME/.local/share/icons/hicolor/scalable/apps/"
sed "s|^Exec=tandem|Exec=$BIN|" data/com.cyberwolf.Tandem.desktop > "$HOME/.local/share/applications/com.cyberwolf.Tandem.desktop"
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
for m in tel sms smsto; do xdg-mime default com.cyberwolf.Tandem.desktop "x-scheme-handler/$m"; done
echo "Installed $BIN"
