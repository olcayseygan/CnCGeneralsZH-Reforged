// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#include "GameClient/TextTerminology.h"

#include <cstring>

namespace TextTerminology {
namespace {

enum Person { Worker, Ranger, MissileDefender, Pathfinder, Redguard, TankHunter,
	Hacker, BlackLotus, Burton, Rebel, RPGTrooper, Terrorist, Hijacker, JarmenKell,
	Saboteur, AngryMob, Sniper, Minigunner, PersonCount };

// Short names from the existing translations, with ordinary names where a
// translation has no entry. No retail prose or external string table is stored.
const char16_t *const PersonNames[6][PersonCount] = {
	{ u"Worker", u"Ranger", u"Missile Defender", u"Pathfinder", u"Red Guard", u"Tank Hunter", u"Hacker", u"Black Lotus", u"Colonel Burton", u"Rebel", u"RPG Trooper", u"Terrorist", u"Hijacker", u"Jarmen Kell", u"Saboteur", u"Angry Mob", u"Sniper", u"Minigunner" },
	{ u"\u0130\u015F\u00E7i", u"Ranger", u"F\u00FCze Savunucusu", u"Pathfinder", u"K\u0131z\u0131l Muhaf\u0131z", u"Tank Avc\u0131s\u0131", u"Hacker", u"Black Lotus", u"Colonel Burton", u"Asi", u"RPG Askeri", u"Ter\u00F6rist", u"Korsan", u"Jarmen Kell", u"Sabotajc\u0131", u"\u00D6fkeli Kalabal\u0131k", u"Keskin Ni\u015Fanc\u0131", u"Minigun Eri" },
	{ u"Arbeiter", u"Ranger", u"Raketenabwehrsoldat", u"Pathfinder", u"Rotgardist", u"Panzerj\u00E4ger", u"Hacker", u"Black Lotus", u"Colonel Burton", u"Rebell", u"RPG-Soldat", u"Terrorist", u"Entf\u00FChrer", u"Jarmen Kell", u"Saboteur", u"W\u00FCtender Mob", u"Scharfsch\u00FCtze", u"Minigunner" },
	{ u"\u0420\u043E\u0431\u0456\u0442\u043D\u0438\u043A", u"Ranger", u"\u0420\u0430\u043A\u0435\u0442\u043D\u0438\u043A", u"Pathfinder", u"Red Guard", u"\u041C\u0438\u0441\u043B\u0438\u0432\u0435\u0446\u044C \u043D\u0430 \u0442\u0430\u043D\u043A\u0438", u"\u0425\u0430\u043A\u0435\u0440", u"Black Lotus", u"Colonel Burton", u"\u041F\u043E\u0432\u0441\u0442\u0430\u043D\u0435\u0446\u044C", u"\u0411\u043E\u0454\u0446\u044C \u0437 \u0420\u041F\u0413", u"\u0422\u0435\u0440\u043E\u0440\u0438\u0441\u0442", u"Hijacker", u"Jarmen Kell", u"\u0414\u0438\u0432\u0435\u0440\u0441\u0430\u043D\u0442", u"\u0420\u043E\u0437\u043B\u044E\u0447\u0435\u043D\u0438\u0439 \u043D\u0430\u0442\u043E\u0432\u043F", u"\u0421\u043D\u0430\u0439\u043F\u0435\u0440", u"\u041C\u0456\u043D\u0456-\u043A\u0443\u043B\u0435\u043C\u0435\u0442\u043D\u0438\u043A" },
	{ u"\u0420\u0430\u0431\u043E\u0447\u0438\u0439", u"Ranger", u"Missile Defender", u"Pathfinder", u"\u041A\u0440\u0430\u0441\u043D\u043E\u0433\u0432\u0430\u0440\u0434\u0435\u0435\u0446", u"\u0418\u0441\u0442\u0440\u0435\u0431\u0438\u0442\u0435\u043B\u044C \u0442\u0430\u043D\u043A\u043E\u0432", u"\u0425\u0430\u043A\u0435\u0440", u"Black Lotus", u"Colonel Burton", u"\u041F\u043E\u0432\u0441\u0442\u0430\u043D\u0435\u0446", u"\u0420\u041F\u0413-\u0441\u0442\u0440\u0435\u043B\u043E\u043A", u"\u0422\u0435\u0440\u0440\u043E\u0440\u0438\u0441\u0442", u"\u0423\u0433\u043E\u043D\u0449\u0438\u043A", u"Jarmen Kell", u"\u0414\u0438\u0432\u0435\u0440\u0441\u0430\u043D\u0442", u"\u0420\u0430\u0437\u044A\u044F\u0440\u0451\u043D\u043D\u0430\u044F \u0442\u043E\u043B\u043F\u0430", u"\u0421\u043D\u0430\u0439\u043F\u0435\u0440", u"\u041C\u0438\u043D\u0438\u043F\u0443\u043B\u0435\u043C\u0451\u0442\u0447\u0438\u043A" },
	{ u"Ouvrier", u"Ranger", u"Infanterie lance-missiles", u"Commando", u"Garde rouge", u"Tueur de chars", u"Technopirate", u"Lotus Noir", u"Colonel Burton", u"Rebelle", u"Lance-roquettes", u"Terroriste", u"Pirate de la route", u"Jarmen Kell", u"Saboteur", u"Emeutiers", u"Tireur embusqu\u00E9", u"Mitrailleur" },
};
const char16_t *const RobotTags[6] = { u" [Robot]", u" [Robot]", u" [Roboter]", u" [\u0420\u043E\u0431\u043E\u0442]", u" [\u0420\u043E\u0431\u043E\u0442]", u" [Robot]" };

struct PersonLabel { const char *label; Person person; };
const PersonLabel PersonLabels[] = {
	{ "OBJECT:Worker", Worker }, { "GUI:BarIdleWorker", Worker },
	{ "CONTROLBAR:ConstructGLAWorker", Worker },
	{ "OBJECT:Ranger", Ranger }, { "CONTROLBAR:ConstructAmericaInfantryRanger", Ranger },
	{ "OBJECT:MissileDefender", MissileDefender }, { "CONTROLBAR:ConstructAmericaInfantryMissileDefender", MissileDefender },
	{ "OBJECT:Pathfinder", Pathfinder }, { "CONTROLBAR:ConstructAmericaInfantryPathfinder", Pathfinder },
	{ "OBJECT:Redguard", Redguard }, { "CONTROLBAR:ConstructChinaInfantryRedguard", Redguard },
	{ "OBJECT:TankHunter", TankHunter }, { "CONTROLBAR:ConstructChinaInfantryTankHunter", TankHunter },
	{ "OBJECT:Hacker", Hacker }, { "CONTROLBAR:ConstructChinaInfantryHacker", Hacker },
	{ "OBJECT:BlackLotus", BlackLotus }, { "CONTROLBAR:ConstructChinaInfantryBlackLotus", BlackLotus },
	{ "OBJECT:ColonelBurton", Burton }, { "CONTROLBAR:ConstructAmericaInfantryColonelBurton", Burton },
	{ "OBJECT:Rebel", Rebel }, { "CONTROLBAR:ConstructGLAInfantryRebel", Rebel },
	{ "OBJECT:RPGTrooper", RPGTrooper }, { "CONTROLBAR:ConstructGLAInfantryRPGTrooper", RPGTrooper },
	{ "OBJECT:Terrorist", Terrorist }, { "CONTROLBAR:ConstructGLAInfantryTerrorist", Terrorist },
	{ "OBJECT:Hijacker", Hijacker }, { "CONTROLBAR:ConstructGLAInfantryHijacker", Hijacker },
	{ "OBJECT:JarmenKell", JarmenKell }, { "CONTROLBAR:ConstructGLAInfantryJarmenKell", JarmenKell },
	{ "OBJECT:Saboteur", Saboteur }, { "CONTROLBAR:ConstructGLAInfantrySaboteur", Saboteur },
	{ "OBJECT:AngryMob", AngryMob }, { "CONTROLBAR:ConstructGLAInfantryAngryMob", AngryMob },
	{ "OBJECT:Sniper", Sniper }, { "CONTROLBAR:ConstructGLAInfantrySniper", Sniper },
	{ "OBJECT:MiniGunner", Minigunner }, { "CONTROLBAR:ConstructChinaInfantryMiniGunner", Minigunner },
};

enum Nation { America, China, GLA };
struct FactionLabel { const char *label; Nation nation; bool nameOnly; };
const FactionLabel FactionLabels[] = {
	{ "INI:FactionAmerica", America, true }, { "INI:FactionChina", China, true }, { "INI:FactionGLA", GLA, true },
	{ "INI:FactionAmericaSuperWeaponGeneral", America, false }, { "INI:FactionAmericaLaserGeneral", America, false },
	{ "INI:FactionAmericaAirForceGeneral", America, false }, { "INI:FactionAmericaCommanderInChief", America, false },
	{ "INI:FactionChinaTankGeneral", China, false }, { "INI:FactionChinaInfantryGeneral", China, false },
	{ "INI:FactionChinaNukeGeneral", China, false }, { "INI:FactionChinaCommanderInChief", China, false },
	{ "INI:FactionGLAToxinGeneral", GLA, false }, { "INI:FactionGLADemolitionGeneral", GLA, false },
	{ "INI:FactionGLAStealthGeneral", GLA, false }, { "INI:FactionGLACommanderInChief", GLA, false },
	{ "SIDE:America", America, true }, { "SIDE:China", China, true }, { "SIDE:GLA", GLA, true },
	{ "GUI:America", America, true }, { "GUI:USA", America, true }, { "GUI:China", China, true }, { "GUI:GLA", GLA, true },
	{ "SIDE:AmericaSuperWeaponGeneral", America, false }, { "SIDE:AmericaLaserGeneral", America, false },
	{ "SIDE:AmericaAirForceGeneral", America, false }, { "SIDE:ChinaTankGeneral", China, false },
	{ "SIDE:ChinaInfantryGeneral", China, false }, { "SIDE:ChinaNukeGeneral", China, false },
	{ "SIDE:GLAToxinGeneral", GLA, false }, { "SIDE:GLADemolitionGeneral", GLA, false }, { "SIDE:GLAStealthGeneral", GLA, false },
	{ "GUI:AmericaAllies", America, false }, { "GUI:USAAllies", America, false }, { "GUI:AmericaEnemies", America, false },
	{ "GUI:USAEnemies", America, false }, { "GUI:ChinaAllies", China, false }, { "GUI:ChinaEnemies", China, false },
	{ "GUI:GLAAllies", GLA, false }, { "GUI:GLAEnemies", GLA, false },
};
const char16_t *const OriginalNames[6][3] = {
	{ u"USA", u"China", u"GLA" }, { u"ABD", u"\u00C7in", u"GLA" }, { u"USA", u"China", u"GLA" },
	{ u"\u0421\u0428\u0410", u"\u041A\u0438\u0442\u0430\u0439", u"\u0413\u041B\u0410" }, { u"\u0421\u0428\u0410", u"\u041A\u0438\u0442\u0430\u0439", u"\u0413\u041B\u0410" }, { u"USA", u"Chine", u"GLA" },
};
const char16_t *const AdaptedNames[6][3] = {
	{ u"Western Alliance", u"Asian Pact", u"International Liberation Group" },
	{ u"Bat\u0131 \u0130ttifak\u0131", u"Asya Pakt\u0131", u"Uluslararas\u0131 Kurtulu\u015F Grubu" },
	{ u"Westliche Allianz", u"Asiatischer Pakt", u"Internationale Befreiungsgruppe" },
	{ u"\u0417\u0430\u0445\u0456\u0434\u043D\u0438\u0439 \u0430\u043B\u044C\u044F\u043D\u0441", u"\u0410\u0437\u0456\u0439\u0441\u044C\u043A\u0438\u0439 \u043F\u0430\u043A\u0442", u"\u041C\u0456\u0436\u043D\u0430\u0440\u043E\u0434\u043D\u0430 \u0432\u0438\u0437\u0432\u043E\u043B\u044C\u043D\u0430 \u0433\u0440\u0443\u043F\u0430" },
	{ u"\u0417\u0430\u043F\u0430\u0434\u043D\u044B\u0439 \u0430\u043B\u044C\u044F\u043D\u0441", u"\u0410\u0437\u0438\u0430\u0442\u0441\u043A\u0438\u0439 \u043F\u0430\u043A\u0442", u"\u041C\u0435\u0436\u0434\u0443\u043D\u0430\u0440\u043E\u0434\u043D\u0430\u044F \u043E\u0441\u0432\u043E\u0431\u043E\u0434\u0438\u0442\u0435\u043B\u044C\u043D\u0430\u044F \u0433\u0440\u0443\u043F\u043F\u0430" },
	{ u"Alliance occidentale", u"Pacte asiatique", u"Groupe international de lib\u00E9ration" },
};

bool sameLabel( const char *a, const char *b )
{
	while (*a && *b) {
		const char x = *a >= 'A' && *a <= 'Z' ? *a + ('a' - 'A') : *a;
		const char y = *b >= 'A' && *b <= 'Z' ? *b + ('a' - 'A') : *b;
		if (x != y) return false;
		++a; ++b;
	}
	return *a == *b;
}

bool wordCharacter( char16_t c )
{
	if (c == u'\u2013' || c == u'\u2014') return false;
	return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z')
		|| (c >= u'0' && c <= u'9') || c == u'_' || c >= 128;
}

void replaceWord( std::u16string &text, const char16_t *from, const std::u16string &to )
{
	const std::u16string source( from );
	std::size_t position = 0;
	while ((position = text.find( source, position )) != std::u16string::npos) {
		const std::size_t end = position + source.size();
		if ((position == 0 || !wordCharacter( text[position - 1] ))
			&& (end == text.size() || !wordCharacter( text[end] ))) {
			text.replace( position, source.size(), to );
			position += to.size();
		} else position = end;
	}
}

std::u16string carryHotkey( std::u16string name, char16_t hotkey )
{
	if (hotkey == 0) return name;
	std::size_t at = name.find( hotkey );
	if (at == std::u16string::npos && hotkey >= u'A' && hotkey <= u'Z')
		at = name.find( hotkey + (u'a' - u'A') );
	if (at == std::u16string::npos && hotkey >= u'a' && hotkey <= u'z')
		at = name.find( hotkey - (u'a' - u'A') );
	if (at != std::u16string::npos) name.insert( at, 1, u'&' );
	else { name += u" (&"; name += hotkey; name += u')'; }
	return name;
}

} // namespace

std::u16string apply( const char *label, const std::u16string &text, int language, int personnel, int factions )
{
	if (label == nullptr || language < English || language > French || text.empty()
		|| text.find_first_of( u"%\r\n" ) != std::u16string::npos) return text;
	std::u16string plain = text;
	char16_t hotkey = 0;
	const std::size_t amp = plain.find( u'&' );
	if (amp != std::u16string::npos) {
		// Ambiguous/literal ampersands are left untouched rather than reinterpreted.
		if (amp + 1 == plain.size() || plain.find( u'&', amp + 1 ) != std::u16string::npos) return text;
		hotkey = plain[amp + 1];
		if (hotkey <= 0x20 || (hotkey >= 0x7F && hotkey <= 0xA0)
			|| (hotkey >= 0xD800 && hotkey <= 0xDFFF)
			|| (hotkey >= 0x2000 && hotkey <= 0x200F)
			|| hotkey == 0x1680 || hotkey == 0x2028 || hotkey == 0x2029
			|| hotkey == 0x202F || hotkey == 0x205F || hotkey == 0x3000) return text;
		plain.erase( amp, 1 );
	}
	if (personnel == Humans || personnel == Robots) {
		for (const PersonLabel &entry : PersonLabels) {
			if (sameLabel( label, entry.label )) {
				std::u16string name = PersonNames[language][entry.person];
				if (personnel == Robots) name += RobotTags[language];
				return carryHotkey( name, hotkey );
			}
		}
	}
	if (factions == Original || factions == Adapted) {
		for (const FactionLabel &entry : FactionLabels) {
			if (!sameLabel( label, entry.label )) continue;
			std::u16string name = factions == Original ? OriginalNames[language][entry.nation] : AdaptedNames[language][entry.nation];
			if (entry.nameOnly) return carryHotkey( name, hotkey );
			std::u16string result = plain;
			if (language == Ukrainian || language == Russian) {
				// These existing short faction labels use the genitive after
				// "general", "allies" and "enemies" in the Slavic translations.
				const char16_t *const originalGenitive[2][3] = {
					{ u"\u0421\u0428\u0410", u"\u041A\u0438\u0442\u0430\u044E", u"\u0413\u041B\u0410" }, { u"\u0421\u0428\u0410", u"\u041A\u0438\u0442\u0430\u044F", u"\u0413\u041B\u0410" },
				};
				const char16_t *const adaptedGenitive[2][3] = {
					{ u"\u0417\u0430\u0445\u0456\u0434\u043D\u043E\u0433\u043E \u0430\u043B\u044C\u044F\u043D\u0441\u0443", u"\u0410\u0437\u0456\u0439\u0441\u044C\u043A\u043E\u0433\u043E \u043F\u0430\u043A\u0442\u0443", u"\u041C\u0456\u0436\u043D\u0430\u0440\u043E\u0434\u043D\u043E\u0457 \u0432\u0438\u0437\u0432\u043E\u043B\u044C\u043D\u043E\u0457 \u0433\u0440\u0443\u043F\u0438" },
					{ u"\u0417\u0430\u043F\u0430\u0434\u043D\u043E\u0433\u043E \u0430\u043B\u044C\u044F\u043D\u0441\u0430", u"\u0410\u0437\u0438\u0430\u0442\u0441\u043A\u043E\u0433\u043E \u043F\u0430\u043A\u0442\u0430", u"\u041C\u0435\u0436\u0434\u0443\u043D\u0430\u0440\u043E\u0434\u043D\u043E\u0439 \u043E\u0441\u0432\u043E\u0431\u043E\u0434\u0438\u0442\u0435\u043B\u044C\u043D\u043E\u0439 \u0433\u0440\u0443\u043F\u043F\u044B" },
				};
				const int slavic = language == Ukrainian ? 0 : 1;
				name = factions == Original ? originalGenitive[slavic][entry.nation] : adaptedGenitive[slavic][entry.nation];
				replaceWord( result, adaptedGenitive[slavic][entry.nation], name );
				replaceWord( result, originalGenitive[slavic][entry.nation], name );
			}
			// Restrict replacements to this one curated faction label and faction.
			for (int source = English; source <= French; ++source) {
				replaceWord( result, AdaptedNames[source][entry.nation], name );
				replaceWord( result, OriginalNames[source][entry.nation], name );
			}
			const char16_t *const abbreviations[] = { u"WA", u"AP", u"IBG" };
			replaceWord( result, abbreviations[entry.nation], name );
			if (result != plain) return carryHotkey( result, hotkey );
			return text;
		}
	}
	return text;
}

} // namespace TextTerminology
