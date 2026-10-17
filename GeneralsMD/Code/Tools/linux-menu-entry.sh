#!/usr/bin/env bash
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# build-posix.sh stages this as ZeroHourReforged/add-to-menu.sh. It puts the folder it sits in into the
# desktop's app menu, with Generals' icon:
#
#   ./add-to-menu.sh            ~/.local/share/applications/zero-hour-reforged.desktop and the icons
#   ./add-to-menu.sh --remove   takes them out again
#
# The file is named after the window's app id (PosixMain.cpp), which is how GNOME on Wayland finds the
# icon for a running window. Run it again after moving the folder.

set -euo pipefail

data=${XDG_DATA_HOME:-$HOME/.local/share}
desktop="$data/applications/zero-hour-reforged.desktop"
here=$(cd "$(dirname "$0")" && pwd)

refresh() {
  if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database -q "$data/applications" || true; fi
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then gtk-update-icon-cache -q -t "$data/icons/hicolor" || true; fi
}

if [ "${1:-}" = "--remove" ]; then
  rm -f "$desktop" "$data/icons/hicolor/48x48/apps/zero-hour-reforged.png" "$data/icons/hicolor/128x128/apps/zero-hour-reforged.png"
  refresh
  echo "[menu] removed Zero Hour Reforged from the app menu"
  exit 0
fi

[ -x "$here/bin/generals" ] || { echo "[menu] ERROR: no bin/generals beside $0" >&2; exit 1; }

for size in 48x48 128x128; do
  mkdir -p "$data/icons/hicolor/$size/apps"
  cp "$here/share/icons/hicolor/$size/apps/zero-hour-reforged.png" "$data/icons/hicolor/$size/apps/"
done

# Exec's quoting (the Desktop Entry spec): inside double quotes \ " ` $ are escaped, and % is doubled
exec_path=$(printf '%s' "$here/bin/generals" | sed -e 's/[\\"`$]/\\\\&/g' -e 's/%/%%/g')
mkdir -p "$data/applications"
cat > "$desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Zero Hour Reforged
Comment=Command & Conquer Generals Zero Hour, reforged
Exec="$exec_path"
Icon=zero-hour-reforged
StartupWMClass=zero-hour-reforged
Categories=Game;StrategyGame;
Terminal=false
DESKTOP
refresh
echo "[menu] added Zero Hour Reforged to the app menu, starting $here/bin/generals"
