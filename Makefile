# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
.DEFAULT_GOAL := help
.PHONY: help install setup build test test-only check
# One workflow owns the build tree/terminal at a time. Backends use JOBS below.
.NOTPARALLEL:

CONFIG ?= $(if $(ZH_BUILD_CONFIG),$(ZH_BUILD_CONFIG),Release)
CPU_TUNING ?= $(if $(ZH_MACOS_CPU_TUNING),$(ZH_MACOS_CPU_TUNING),auto)
JOBS ?= $(if $(ZH_BUILD_JOBS),$(ZH_BUILD_JOBS),$(if $(CMAKE_BUILD_PARALLEL_LEVEL),$(CMAKE_BUILD_PARALLEL_LEVEL),auto))
TEST_JOBS ?= $(if $(ZH_TEST_JOBS),$(ZH_TEST_JOBS),$(if $(CTEST_PARALLEL_LEVEL),$(CTEST_PARALLEL_LEVEL),auto))
PERSONNEL ?= $(if $(ZH_TEXT_PERSONNEL),$(ZH_TEXT_PERSONNEL),unchanged)
FACTIONS ?= $(if $(ZH_TEXT_FACTIONS),$(ZH_TEXT_FACTIONS),unchanged)
export ZH_BUILD_CONFIG := $(CONFIG)
export ZH_MACOS_CPU_TUNING := $(CPU_TUNING)
export ZH_BUILD_JOBS := $(JOBS)
export ZH_TEST_JOBS := $(TEST_JOBS)
export ZH_TEXT_PERSONNEL := $(PERSONNEL)
export ZH_TEXT_FACTIONS := $(FACTIONS)
# Leave the platform defaults and build.local.sh in charge when unset.
ifneq ($(origin BUILD),undefined)
export BUILD
endif
ifneq ($(origin CMAKE),undefined)
export CMAKE
endif

help:
	@printf '%s\n' \
	  'Usage: make <target> [VARIABLE=value ...] (run from the repository root)' \
	  '' \
	  '  install, setup  Guided setup; asks before each action' \
	  '  build           Build and package the application' \
	  '  test            Build, run every test and package' \
	  '  test-only       Run every test in an existing configured build' \
	  '  check           Check that retail game files stay outside Git' \
	  '' \
	  '  CONFIG=Release       Build configuration (also used by install)' \
	  '  JOBS=auto            Build workers: CPU/memory aware, at most 8' \
	  '  TEST_JOBS=auto       Test workers: at most 4 by default' \
	  '  BUILD=/path          Override the platform build directory' \
	  '  CMAKE=/path/cmake    Override the CMake executable' \
	  '  CPU_TUNING=generic   Opt out of automatic Apple Silicon tuning' \
	  '  PERSONNEL=unchanged  Unit names: unchanged, humans, robots' \
	  '  FACTIONS=unchanged   Faction names: unchanged, original, adapted' \
	  '' \
	  'Examples: make install JOBS=8 TEST_JOBS=4' \
	  '          make test-only TEST_JOBS=2'

install setup:
	@./GeneralsMD/Code/Tools/install-posix.sh

build:
	@case "$$(uname -s)" in Darwin) ./build-macos.sh "$$ZH_BUILD_CONFIG" ;; Linux) ./build-linux.sh "$$ZH_BUILD_CONFIG" ;; *) echo 'On Windows, please use build.bat.' >&2; exit 1 ;; esac

test:
	@case "$$(uname -s)" in Darwin) ./build-macos.sh "$$ZH_BUILD_CONFIG" test ;; Linux) ./build-linux.sh "$$ZH_BUILD_CONFIG" test ;; *) echo 'On Windows, please use build.bat.' >&2; exit 1 ;; esac

test-only:
	@case "$$(uname -s)" in Darwin) ZH_TEST_ONLY=1 ./build-macos.sh "$$ZH_BUILD_CONFIG" test ;; Linux) ZH_TEST_ONLY=1 ./build-linux.sh "$$ZH_BUILD_CONFIG" test ;; *) echo 'On Windows, please use build.bat.' >&2; exit 1 ;; esac

check:
	@python3 GeneralsMD/Code/Tools/check_game_assets.py
