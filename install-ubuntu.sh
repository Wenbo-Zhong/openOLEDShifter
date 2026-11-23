#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 OpenOLEDShifter contributors
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

uuid=openoledshifter@openoledshifter
data=${XDG_DATA_HOME:-$HOME/.local/share}
app="$data/openoledshifter"
desktop="$data/applications/org.openoledshifter.Desktop.desktop"
source_dir="$PWD/release/ubuntu"

if [[ ${1:-} == --uninstall ]]; then
    if gnome-extensions info "$uuid" >/dev/null 2>&1; then
        gnome-extensions disable "$uuid"
        gnome-extensions uninstall "$uuid"
    fi
    if [[ -f "$app/.openoledshifter-install" ]]; then
        rm -r -- "$app"
        if [[ -f "$desktop" ]] && grep -qx 'X-OpenOLEDShifter-Managed=true' "$desktop"; then
            rm -- "$desktop"
        fi
    fi
    exit
fi

[[ $# -eq 0 ]] || { printf 'Usage: %s [--uninstall]\n' "$0" >&2; exit 2; }
[[ -x "$source_dir/desktop/OpenOLEDShifter" ]] || { printf 'Run ./build-ubuntu.sh first.\n' >&2; exit 1; }
if [[ -L "$app" || ( -e "$app" && ! -f "$app/.openoledshifter-install" ) ]]; then
    printf 'Refusing to overwrite an unrecognized directory: %s\n' "$app" >&2
    exit 1
fi

if [[ -e "$desktop" ]] && ! grep -qx 'X-OpenOLEDShifter-Managed=true' "$desktop"; then
    printf 'Refusing to overwrite an unrecognized desktop entry: %s\n' "$desktop" >&2
    exit 1
fi

mkdir -p -- "$app" "$data/applications"
touch -- "$app/.openoledshifter-install"
cp -a -- "$source_dir/desktop/." "$app/"
install -m 0644 assets/app-icon.png "$app/app-icon.png"

# Desktop-entry Exec uses key-file escapes and then argument escapes.
escaped=$(python3 - "$app/OpenOLEDShifter" <<'PY'
import sys
path = sys.argv[1]
argument = '"' + ''.join(('\\' + char) if char in '\\"$`' else char for char in path) + '"'
print(argument.replace('\\', '\\\\').replace('%', '%%').replace('\n', '\\n'))
PY
)
cat > "$desktop" <<ENTRY
[Desktop Entry]
Type=Application
Name=OpenOLEDShifter
Comment=Configure OLED wallpaper pixel shifting
Exec=$escaped
Icon=$app/app-icon.png
StartupWMClass=OpenOLEDShifter
Terminal=false
Categories=Utility;
X-OpenOLEDShifter-Managed=true
ENTRY
gnome-extensions install --force "$source_dir/$uuid.shell-extension.zip"
printf 'Installed. Enable with: gnome-extensions enable %s\n' "$uuid"
