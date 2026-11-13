#!/bin/sh
# SPDX-License-Identifier: MIT
# Adds the launcher to the desktop's application menu (~/.local/share/applications). The icon is
# the game's own icon0.png from your dump, used where it is (not copied).
# Usage: tools/launcher/install-desktop.sh [--remove]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
entry=$apps/io.github.robertoqimin.BattrideWarSouseiPort.desktop
if [ "${1:-}" = "--remove" ]; then
    rm -f "$entry"
    echo "Removed $entry"
    exit 0
fi
icon=applications-games
base=$(cd "$here" && python3 -c 'import core; print(core.load_settings().get("base", ""))' 2>/dev/null || true)
if [ -n "$base" ] && [ -f "$base/sce_sys/icon0.png" ]; then
    icon=$base/sce_sys/icon0.png
fi
mkdir -p "$apps"
cat > "$entry" <<EOF
[Desktop Entry]
Type=Application
Name=Kamen Rider: Battride War Genesis
Comment=Native PC port (your own PS4 dump)
Exec=python3 "$here/launcher.py"
Icon=$icon
Terminal=false
Categories=Game;
EOF
echo "Added $entry"
[ "$icon" = applications-games ] && echo "(Generic icon: choose the base game folder in the launcher, then run this again for the game's icon.)"
exit 0
