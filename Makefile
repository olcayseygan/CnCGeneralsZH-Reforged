# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
.DEFAULT_GOAL := help
.PHONY: help install setup build test check
CONFIG ?= Release
CPU_TUNING ?= auto
export ZH_MACOS_CPU_TUNING := $(CPU_TUNING)

help:
	@printf '%s\n' 'make install  Geführter Installer für macOS/Linux (fragt vor jedem Schritt)' 'make build    Direkt bauen und paketieren, ohne Installer-Dialoge' 'make test     Direkt bauen, testen und paketieren' 'make check    Schutz vor versehentlich eingecheckten Originaldateien prüfen' 'CONFIG=Debug  Optional eine andere Build-Konfiguration wählen' 'CPU_TUNING=generic  Automatisches Apple-M2-Scheduling deaktivieren'

install setup:
	@./GeneralsMD/Code/Tools/install-posix.sh

build:
	@case "$$(uname -s)" in Darwin) ./build-macos.sh "$(CONFIG)" ;; Linux) ./build-linux.sh "$(CONFIG)" ;; *) echo 'Bitte build.bat unter Windows verwenden.' >&2; exit 1 ;; esac

test:
	@case "$$(uname -s)" in Darwin) ./build-macos.sh "$(CONFIG)" test ;; Linux) ./build-linux.sh "$(CONFIG)" test ;; *) echo 'Bitte build.bat unter Windows verwenden.' >&2; exit 1 ;; esac

check:
	@python3 GeneralsMD/Code/Tools/check_game_assets.py
