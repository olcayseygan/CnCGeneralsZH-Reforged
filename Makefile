# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
.DEFAULT_GOAL := help
.PHONY: help install setup
help:
	@printf '%s\n' 'make install  Guided macOS/Linux setup (asks before actions)'
install setup:
	@./GeneralsMD/Code/Tools/install-posix.sh
