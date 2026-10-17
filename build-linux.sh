#!/usr/bin/env bash
#	Copyright 2026 İlyas Akın
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
# Build Zero Hour Reforged on Linux, from a fresh clone, in one command. build.bat's arguments:
#
#   ./build-linux.sh                    check the toolchain, fetch, configure, build Release, stage the game
#   ./build-linux.sh Debug              another configuration (Release|RelWithDebInfo|Debug)
#   ./build-linux.sh Release test       the same, with ctest before the staging
#   ./build-linux.sh Release generals   build a single target, and nothing else
#   ./build-linux.sh clean              delete build-linux/ and start again
#
# Needs a C++ compiler, CMake, Ninja and SDL3's X11, Wayland and Vulkan headers; it prints the apt, dnf
# or pacman command when something is missing. The result is build-linux/ZeroHourReforged/bin/generals.
# build-linux/ZeroHourReforged/add-to-menu.sh puts it in the desktop's app menu, with Generals' icon.
# GeneralsMD/Code/Tools/build-posix.sh does the work, shared with build-macos.sh. The release packages
# (AppImage, .deb, .rpm, Arch, Flatpak) are GeneralsMD/Code/Tools/linux-packages.sh's.

exec "$(dirname "$0")/GeneralsMD/Code/Tools/build-posix.sh" linux "$@"
