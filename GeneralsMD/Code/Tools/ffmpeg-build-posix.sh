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
# Portions adapted from GeneralsMD/Code/Tools/ffmpeg-build.sh by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.
#
# Builds the FFmpeg the POSIX build links, from the tarball Tools/vendor.sh keeps in
# Libraries/Source/FFmpeg, with the components the game's movies need and nothing else (V1).
#
#   ffmpeg-build-posix.sh <tarball> <work dir> <install prefix> <C compiler> [<extra cflags>]
#
# CMake runs it (the ffmpeg_posix custom command) whenever the tarball or this script changes. It
# unpacks into <work dir>, configures and builds there, installs include/ and five static libraries
# into <install prefix>, and then deletes <work dir>: the unpacked tree is 110MB and the objects
# another 9MB, while what is kept is 3.5MB. Measured on the Mac (arm64, load ~5): configure 23s,
# make -j8 7s.
#
# What the movies need, and therefore all that is built:
#   .bik movies -> the bink demuxer, the bink video decoder and both binkaudio decoders, the file
#                  protocol, and swscale and swresample for bink_ffmpeg.cpp's conversions.
# Unlike Windows' build (Tools/ffmpeg-build.sh) there is no mp3 or wav: off Windows the game's own
# audio is decoded by miniaudio (C4), so FFmpeg serves only the movies.
#
# --disable-asm, measured over all 70 movies of the install (V1): the Bink decoder is
# bit-exact between the NEON and C builds, but swscale's NEON yuv420p->BGRA path is not the C path's
# arithmetic. With the C paths every POSIX machine converts to identical bytes, so one golden table
# serves the Mac and Linux, and nasm is not a build dependency. The cost was 141s instead of 128s to
# decode everything, 1761s of video: still 12.5x real time on one core.
#
# --disable-autodetect keeps VideoToolbox, AudioToolbox, iconv, zlib and the rest out of the link.
#
# LGPL: configure prints "License: LGPL version 2.1 or later" for this line (no --enable-gpl, no
# --enable-nonfree). The libraries are static here; the game is GPL-3 with its source published, so
# anyone can relink it against a modified FFmpeg (LGPL 2.1 section 6). COPYING.LGPLv2.1 is installed
# beside the libraries as LICENSE.txt.
set -euo pipefail

if [ $# -lt 4 ]; then
  echo "usage: $0 <tarball> <work dir> <install prefix> <C compiler> [<extra cflags> [<target arch>]]" >&2
  exit 2
fi
tarball="$1" work="$2" prefix="$3" cc="$4" extra_cflags="${5:-}" target_arch="${6:-}"

tools_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
# shellcheck source=build-jobs.sh
. "$tools_dir/build-jobs.sh"
jobs=$(zh_resolve_build_jobs "${ZH_BUILD_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-auto}}") || exit 2

# A target architecture (FFmpeg's name for it) builds for that one rather than the host's: configure
# otherwise detects the machine it runs on.  The flags that name it go to the linker too, or
# configure's own link tests build for the host.
cross=()
if [ -n "$target_arch" ]; then
  cross=(--arch="$target_arch" --enable-cross-compile)
fi
version=8.1.2

rm -rf "$work" "$prefix"
mkdir -p "$work/build"
tar -xf "$tarball" -C "$work"
source="$work/ffmpeg-$version"
if [ ! -x "$source/configure" ]; then
  echo "ffmpeg-build-posix: $tarball did not unpack to ffmpeg-$version" >&2
  exit 1
fi

cd "$work/build"
if ! "$source/configure" \
    --prefix="$prefix" \
    --cc="$cc" \
    ${extra_cflags:+--extra-cflags="$extra_cflags"} \
    ${extra_cflags:+--extra-ldflags="$extra_cflags"} \
    ${cross[@]+"${cross[@]}"} \
    --enable-static \
    --disable-shared \
    --disable-everything \
    --disable-autodetect \
    --disable-programs \
    --disable-doc \
    --disable-avdevice \
    --disable-avfilter \
    --disable-network \
    --disable-debug \
    --enable-pic \
    --disable-asm \
    --enable-swscale \
    --enable-swresample \
    --enable-demuxer=bink \
    --enable-decoder=bink,binkaudio_dct,binkaudio_rdft \
    --enable-protocol=file > configure.log 2>&1; then
  tail -20 configure.log >&2
  exit 1
fi

# configure takes an unknown component name without a word (ffmpeg-build.sh's "binkvideo" lesson),
# so every one asked for is checked, and the licence with them.
for component in BINK_DECODER BINKAUDIO_DCT_DECODER BINKAUDIO_RDFT_DECODER BINK_DEMUXER FILE_PROTOCOL; do
  if ! grep -q "^#define CONFIG_${component} 1$" config_components.h; then
    echo "ffmpeg-build-posix: configure did not enable ${component}" >&2
    exit 1
  fi
done
if ! grep -q "^License: LGPL version 2.1 or later$" configure.log; then
  echo "ffmpeg-build-posix: configure did not report an LGPL 2.1+ build" >&2
  exit 1
fi

if ! make -j"$jobs" > make.log 2>&1; then
  tail -30 make.log >&2
  exit 1
fi
make install > install.log 2>&1
cp "$source/COPYING.LGPLv2.1" "$prefix/LICENSE.txt"
rm -rf "$prefix/lib/pkgconfig" "$prefix/share"

cd /
rm -rf "$work"
echo "ffmpeg-build-posix: FFmpeg $version installed into $prefix"
