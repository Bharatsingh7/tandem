#!/bin/sh
# Installs the desktop side for the current user: launcher, menu entry, icon, tel:/sms: handlers.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p ~/.local/bin ~/.local/share/applications ~/.local/share/icons/hicolor/scalable/apps
ln -sf "$here/bin/tandem" ~/.local/bin/tandem
cp "$here/data/com.cyberwolf.Tandem.desktop" ~/.local/share/applications/
cp "$here/data/com.cyberwolf.Tandem.svg" ~/.local/share/icons/hicolor/scalable/apps/
update-desktop-database ~/.local/share/applications 2>/dev/null || true
for s in tel sms smsto; do xdg-mime default com.cyberwolf.Tandem.desktop "x-scheme-handler/$s"; done
echo "Installed. Run 'tandem' or find Tandem in your launcher."
