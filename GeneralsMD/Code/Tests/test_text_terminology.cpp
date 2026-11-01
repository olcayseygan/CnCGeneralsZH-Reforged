// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#include "GameClient/TextTerminology.h"

#include <cstdio>
#include <string>

using namespace TextTerminology;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (false)

int main()
{
	// The two axes are independent in every one of the nine combinations.
	for (int personnel = 0; personnel < 3; ++personnel) {
		for (int factions = 0; factions < 3; ++factions) {
			const std::u16string worker = apply( "OBJECT:Worker", u"Cyborg", German, personnel, factions );
			CHECK(worker == (personnel == 0 ? u"Cyborg" : personnel == 1 ? u"Arbeiter" : u"Arbeiter [Roboter]"));
			const std::u16string america = apply( "SIDE:America", u"WA", German, personnel, factions );
			CHECK(america == (factions == 0 ? u"WA" : factions == 1 ? u"USA" : u"Westliche Allianz"));
		}
	}
	CHECK(apply("OBJECT:Worker", u"Cyborg", English, Humans, 0) == u"Worker");
	CHECK(apply("OBJECT:Worker", u"Cyborg", Turkish, Humans, 0) == u"\u0130\u015F\u00E7i");
	CHECK(apply("OBJECT:Worker", u"Cyborg", Ukrainian, Humans, 0) == u"\u0420\u043E\u0431\u0456\u0442\u043D\u0438\u043A");
	CHECK(apply("OBJECT:Worker", u"Cyborg", Russian, Robots, 0) == u"\u0420\u0430\u0431\u043E\u0447\u0438\u0439 [\u0420\u043E\u0431\u043E\u0442]");
	CHECK(apply("OBJECT:Worker", u"Cyborg", French, Humans, 0) == u"Ouvrier");
	CHECK(apply("CONTROLBAR:ConstructAmericaInfantryColonelBurton", u"Unit", German, Robots, 0) == u"Colonel Burton [Roboter]");
	CHECK(apply("CONTROLBAR:ConstructChinaInfantryBlackLotus", u"Unit", French, Humans, 0) == u"Lotus Noir");
	CHECK(apply("OBJECT:Redguard", u"Unit", German, Humans, 0) == u"Rotgardist");
	CHECK(apply("OBJECT:Rebel", u"Unit", German, Robots, 0) == u"Rebell [Roboter]");
	CHECK(apply("SIDE:China", u"China", German, 0, Adapted) == u"Asiatischer Pakt");
	CHECK(apply("SIDE:GLA", u"GLA", German, 0, Adapted) == u"Internationale Befreiungsgruppe");
	CHECK(apply("INI:FactionAmerica", u"WA", German, 0, Original) == u"USA");
	CHECK(apply("INI:FactionChina", u"China", German, 0, Adapted) == u"Asiatischer Pakt");
	CHECK(apply("INI:FactionGLA", u"GLA", German, 0, Adapted) == u"Internationale Befreiungsgruppe");
	for (const char *label : { "INI:FactionAmericaSuperWeaponGeneral", "INI:FactionAmericaLaserGeneral",
		"INI:FactionAmericaAirForceGeneral", "INI:FactionAmericaCommanderInChief" }) {
		CHECK(apply(label, u"USA General", German, 0, Adapted) == u"Westliche Allianz General");
		CHECK(apply(label, u"WA General", German, 0, Original) == u"USA General");
	}
	for (const char *label : { "INI:FactionChinaTankGeneral", "INI:FactionChinaInfantryGeneral",
		"INI:FactionChinaNukeGeneral", "INI:FactionChinaCommanderInChief" })
		CHECK(apply(label, u"China General", German, 0, Adapted) == u"Asiatischer Pakt General");
	for (const char *label : { "INI:FactionGLAToxinGeneral", "INI:FactionGLADemolitionGeneral",
		"INI:FactionGLAStealthGeneral", "INI:FactionGLACommanderInChief" })
		CHECK(apply(label, u"GLA General", German, 0, Adapted) == u"Internationale Befreiungsgruppe General");
	CHECK(apply("SIDE:ChinaTankGeneral", u"China Panzergeneral", German, 0, Adapted) == u"Asiatischer Pakt Panzergeneral");
	CHECK(apply("SIDE:AmericaLaserGeneral", u"WA Lasergeneral", German, 0, Original) == u"USA Lasergeneral");
	CHECK(apply("GUI:ChinaAllies", u"China-Verb\u00FCndete", German, 0, Adapted) == u"Asiatischer Pakt-Verb\u00FCndete");
	CHECK(apply("SIDE:ChinaTankGeneral", u"Chinatown general", English, 0, Adapted) == u"Chinatown general");
	CHECK(apply("SIDE:America", u"USA", French, 0, Adapted) == u"Alliance occidentale");
	CHECK(apply("SIDE:China", u"China", Turkish, 0, Adapted) == u"Asya Pakt\u0131");
	CHECK(apply("SIDE:ChinaTankGeneral", u"\u0422\u0430\u043D\u043A\u043E\u0432\u044B\u0439 \u0433\u0435\u043D\u0435\u0440\u0430\u043B \u041A\u0438\u0442\u0430\u044F", Russian, 0, Adapted) == u"\u0422\u0430\u043D\u043A\u043E\u0432\u044B\u0439 \u0433\u0435\u043D\u0435\u0440\u0430\u043B \u0410\u0437\u0438\u0430\u0442\u0441\u043A\u043E\u0433\u043E \u043F\u0430\u043A\u0442\u0430");
	CHECK(apply("SIDE:ChinaTankGeneral", u"\u0422\u0430\u043D\u043A\u043E\u0432\u0438\u0439 \u0433\u0435\u043D\u0435\u0440\u0430\u043B \u0410\u0437\u0456\u0439\u0441\u044C\u043A\u043E\u0433\u043E \u043F\u0430\u043A\u0442\u0443", Ukrainian, 0, Original) == u"\u0422\u0430\u043D\u043A\u043E\u0432\u0438\u0439 \u0433\u0435\u043D\u0435\u0440\u0430\u043B \u041A\u0438\u0442\u0430\u044E");

	// Preserve the accelerator even when its letter is absent from the new name.
	CHECK(apply("CONTROLBAR:ConstructGLAWorker", u"&Worker", English, Robots, 0) == u"&Worker [Robot]");
	CHECK(apply("CONTROLBAR:ConstructGLAWorker", u"&Cyborg", German, Humans, 0) == u"Arbeiter (&C)");
	CHECK(apply("SIDE:America", u"&USA", German, 0, Adapted) == u"Westliche Allianz (&U)");
	CHECK(apply("OBJECT:Worker", u"A&&B", German, Robots, 0) == u"A&&B");
	CHECK(apply("OBJECT:Worker", u"Worker&", German, Robots, 0) == u"Worker&");
	CHECK(apply("OBJECT:Worker", u"&\U0001F916", German, Robots, 0) == u"&\U0001F916");
	CHECK(apply("OBJECT:Worker", u"& Worker", German, Robots, 0) == u"& Worker");
	CHECK(apply("OBJECT:Worker", u"&\u2003Worker", German, Robots, 0) == u"&\u2003Worker");
	for (const char16_t *format : { u"%d workers", u"%s", u"100%%", u"%1$d", u"one\ntwo" })
		CHECK(apply("OBJECT:Worker", format, German, Robots, Adapted) == format);

	// No broad prefix replacement: vehicles, drones, prose and mod names are data.
	for (const char *label : { "OBJECT:BattleDrone", "OBJECT:RepairDrone", "OBJECT:SpyDrone", "OBJECT:Dozer",
		"OBJECT:TankHunterVehicle", "OBJECT:WorkerMod", "M1~OBJECT:Worker", "SIDE:Turkiye", "INI:FactionTurkiye",
		"CONTROLBAR:ToolTipUSABuildRanger", "MAP:USA01Open", "GUI:Unknown" })
		CHECK(apply(label, u"USA Worker \U0001F916", German, Robots, Adapted) == u"USA Worker \U0001F916");
	CHECK(apply("OBJECT:Worker", u"Worker", -1, Robots, Adapted) == u"Worker");
	CHECK(apply("OBJECT:Worker", u"Worker", 6, Robots, Adapted) == u"Worker");
	CHECK(apply(nullptr, u"Worker", German, Robots, Adapted) == u"Worker");
	CHECK(apply("OBJECT:Worker", u"", German, Robots, Adapted).empty());
	CHECK(apply("OBJECT:Worker", u"Worker", German, 99, Adapted) == u"Worker");
	CHECK(apply("SIDE:America", u"USA", German, Robots, 99) == u"USA");
	CHECK(apply("object:worker", u"Worker", German, Robots, 0) == u"Arbeiter [Roboter]");
	const std::u16string once = apply("OBJECT:Worker", u"Worker", German, Robots, 0);
	CHECK(apply("OBJECT:Worker", once, German, Robots, 0) == once);
	CHECK(apply("OBJECT:Worker", once, German, Humans, 0) == u"Arbeiter");
	std::printf("text terminology: %s\n", failures ? "FAILED" : "passed");
	return failures ? 1 : 0;
}
