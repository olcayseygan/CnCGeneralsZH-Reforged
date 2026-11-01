# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
.DEFAULT_GOAL := help
.PHONY: help install setup build test check
CONFIG ?= Release
CPU_TUNING ?= auto
export ZH_MACOS_CPU_TUNING := $(CPU_TUNING)

help:
	@printf '%s\n' 'make install  Guided installer for macOS/Linux (asks before every step)' 'make build    Build and package directly, without installer prompts' 'make test     Build, test and package directly' 'make check    Guard against accidentally committed original game files' 'CONFIG=Debug  Choose a different build configuration' 'CPU_TUNING=generic  Opt out of automatic CPU scheduling tuning (Apple Silicon)'

install setup:
	@./GeneralsMD/Code/Tools/install-posix.sh

build:
	@case "$$(uname -s)" in Darwin) ./build-macos.sh "$(CONFIG)" ;; Linux) ./build-linux.sh "$(CONFIG)" ;; *) echo 'On Windows, please use build.bat.' >&2; exit 1 ;; esac

test:
	@case "$$(uname -s)" in Darwin) ./build-macos.sh "$(CONFIG)" test ;; Linux) ./build-linux.sh "$(CONFIG)" test ;; *) echo 'On Windows, please use build.bat.' >&2; exit 1 ;; esac

check:
	@python3 GeneralsMD/Code/Tools/check_game_assets.py
