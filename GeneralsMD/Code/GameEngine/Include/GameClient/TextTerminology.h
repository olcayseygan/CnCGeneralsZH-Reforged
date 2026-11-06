// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#pragma once

#include <string>

namespace TextTerminology {

enum Personnel { UnchangedPersonnel = 0, Humans = 1, Robots = 2 };
enum Factions { UnchangedFactions = 0, Original = 1, Adapted = 2 };
enum Language { English = 0, Turkish = 1, German = 2, Ukrainian = 3, Russian = 4, French = 5 };

// Only curated display-name labels are eligible. IDs, prose, map and mod text
// remain the caller's own data. Robot names are a style, not replacement assets.
std::u16string apply( const char *label, const std::u16string &text,
	int language, int personnel, int factions );

} // namespace TextTerminology
