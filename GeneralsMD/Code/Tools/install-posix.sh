#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
set -eu
root=$(CDPATH= cd "$(dirname "$0")/../../.." && pwd -P)
# Python may not exist yet, so these few bootstrap messages cannot use install_i18n.py.
case "${ZH_INSTALL_LANG:-${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}}" in [dD][eE]|[dD][eE][_.@-]*) lang=de ;; *) lang=en ;; esac
say() { if [ "$lang" = de ]; then printf '%s' "$2"; else printf '%s' "$1"; fi; }
# Apple's system Python can otherwise open an Xcode installation dialog before
# our own installer has asked. Explain that bootstrap step explicitly.
if [ "$(uname -s)" = Darwin ] && [ "$(command -v python3 || true)" = /usr/bin/python3 ] &&
   ! xcode-select -p >/dev/null 2>&1; then
  say "Apple's system Python needs the Command Line Tools. Open the installer dialog? [y/N] " \
      'Apples System-Python benötigt die Command Line Tools. Installationsdialog öffnen? [j/N] '
  echo
  IFS= read -r answer || exit 130
  case "$lang:$answer" in
    *:[yY]|*:[yY][eE][sS]|de:[jJ]|de:[jJ][aA]) xcode-select --install ;;
  esac
  say 'Run make install again after the installation.' 'Nach der Installation make install erneut ausführen.'; echo
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  { say 'The guided installer needs Python 3.' 'Der geführte Installer benötigt Python 3.'; echo; } >&2
  case "$(uname -s)" in
    Darwin) { say 'Install the Apple Command Line Tools (xcode-select --install) or Python 3 from python.org.' \
                  'Installiere die Apple Command Line Tools (xcode-select --install) oder Python 3 von python.org.'; echo; } >&2 ;;
    Linux) { say "Install Python 3 with your distribution's package manager (e.g. sudo apt-get install python3)." \
                 'Installiere Python 3 mit dem Paketmanager deiner Distribution (z. B. sudo apt-get install python3).'; echo; } >&2 ;;
  esac
  exit 1
fi
exec python3 "$root/GeneralsMD/Code/Tools/install.py" "$@"
