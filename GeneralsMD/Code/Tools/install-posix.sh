#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
set -eu
root=$(CDPATH= cd "$(dirname "$0")/../../.." && pwd -P)
# Apple's system Python can otherwise open an Xcode installation dialog before
# our own installer has asked. Explain that bootstrap step explicitly.
if [ "$(uname -s)" = Darwin ] && [ "$(command -v python3 || true)" = /usr/bin/python3 ] &&
   ! xcode-select -p >/dev/null 2>&1; then
  printf 'Apples System-Python benötigt die Command Line Tools. Installationsdialog öffnen? [j/N] '
  IFS= read -r answer || exit 130
  case "$answer" in j|J|ja|Ja|JA|y|Y|yes|YES) xcode-select --install ;; esac
  echo 'Nach der Installation make install erneut ausführen.'
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo 'Der geführte Installer benötigt Python 3.' >&2
  case "$(uname -s)" in
    Darwin) echo 'Installiere die Apple Command Line Tools (xcode-select --install) oder Python 3 von python.org.' >&2 ;;
    Linux) echo 'Installiere Python 3 mit dem Paketmanager deiner Distribution (z. B. sudo apt-get install python3).' >&2 ;;
  esac
  exit 1
fi
exec python3 "$root/GeneralsMD/Code/Tools/install.py" "$@"
