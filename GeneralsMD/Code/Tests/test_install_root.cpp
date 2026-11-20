/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
 * P1 step 4: where the player's Zero Hour is (PosixInstallRoot.h), every path but the dialog.
 *
 * Built in a temporary folder: Zero Hour folders with and without the base game (beside them, in
 * ZH_Generals/, or through Registry.ini's Generals InstallPath and its First Decade subfolder), a folder
 * that is not Zero Hour, one inside a fake app bundle, a fake home with ~/Games and a CrossOver bottle,
 * and Registry.ini files. The chooser is a stub that answers from a list and records the reasons it
 * was given; the real one is SDL's native folder dialog, which needs a person to click it.
 *
 * What it cannot see: the dialog itself; players' real install layouts beyond the names in the known
 * places (a guess until players report theirs).
 */

#include "test_harness.h"

#include "PosixDevice/Common/PosixInstallRoot.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace {

std::string s_base;

void folder( const std::string &path )
{
	std::string partial;
	for (size_t i = 0; i < path.size(); ++i)
	{
		partial += path[i];
		if (path[i] == '/' || i + 1 == path.size())
			mkdir( partial.c_str(), 0777 );
	}
}

void file( const std::string &path, const char *text = "x" )
{
	folder( path.substr( 0, path.rfind( '/' ) ) );
	FILE *f = fopen( path.c_str(), "wb" );
	if (f != NULL)
	{
		fputs( text, f );
		fclose( f );
	}
}

std::string real( const std::string &path )
{
	char buffer[ PATH_MAX ];
	return realpath( path.c_str(), buffer ) != NULL ? buffer : path;
}

std::string at( const char *relative ) { return s_base + "/" + relative; }

void build()
{
	const char *temp = getenv( "TMPDIR" );
	char unique[ 64 ];
	snprintf( unique, sizeof( unique ), "/test_install_root_%d", (int)getpid() );
	s_base = std::string( temp != NULL && *temp ? temp : "/tmp" );
	if (s_base[s_base.size() - 1] == '/')
		s_base.erase( s_base.size() - 1 );
	s_base = real( s_base ) + unique;
	folder( s_base );

	file( at( "whole/INIZH.big" ) );							// Zero Hour with its base game inside
	file( at( "whole/ZH_Generals/Textures.big" ) );
	file( at( "nobase/INIZH.big" ) );							// Zero Hour alone
	file( at( "siblings/Zero Hour/INIZH.big" ) );				// the base game beside it
	file( at( "siblings/Command & Conquer Generals/Textures.big" ) );
	file( at( "notzh/readme.txt" ) );							// not Zero Hour at all
	file( at( "firstdecade/Command & Conquer(tm) Generals/Textures.big" ) );	// Generals' registered folder
	file( at( "Fake.app/Contents/Resources/Overlay/zh/INIZH.big" ) );		// inside the app
	file( at( "Fake.app/Contents/Resources/Overlay/zh/ZH_Generals/Textures.big" ) );
	file( at( "home/Games/Command & Conquer Generals Zero Hour/INIZH.big" ) );	// ~/Games, but no base game
	const char *bottle = "home/Library/Application Support/CrossOver/Bottles/Steam/drive_c/Program Files (x86)/"
		"Steam/steamapps/common/Command & Conquer Generals - Zero Hour";
	file( std::string( at( bottle ) ) + "/INIZH.big" );				// a CrossOver bottle, whole
	file( std::string( at( bottle ) ) + "/ZH_Generals/Textures.big" );
	folder( at( "emptyhome" ) );
	folder( at( "exe" ) );

	// the Steam Deck (P3): Steam's own library holds nothing; libraryfolders.vdf names an SD card's, which
	// holds Zero Hour with its base game beside it, as Steam installs the two; ~/.steam/steam is the usual
	// symbolic link to Steam's folder, so the one library must not be listed twice
	folder( at( "steamhome/.local/share/Steam/steamapps/common" ) );
	const std::string card = at( "sdcard" );
	file( at( "steamhome/.local/share/Steam/steamapps/libraryfolders.vdf" ),
		("\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + at( "steamhome/.local/share/Steam" )
		+ "\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + card + "\"\n\t\t\"label\"\t\t\"\"\n\t}\n}\n").c_str() );
	file( card + "/steamapps/common/Command & Conquer Generals - Zero Hour/INIZH.big" );
	file( card + "/steamapps/common/Command & Conquer Generals/Textures.big" );
	folder( at( "steamhome/.steam" ) );
	symlink( at( "steamhome/.local/share/Steam" ).c_str(), at( "steamhome/.steam/steam" ).c_str() );
	// A Mac's native Steam library, with another library on an external volume.
	file( at( "machome/Library/Application Support/Steam/steamapps/libraryfolders.vdf" ),
		("\"libraryfolders\"\n{\n\"1\" {\n\"path\" \"" + at( "macdisk" ) + "\"\n}\n}\n").c_str() );
	file( at( "macdisk/steamapps/common/Command & Conquer Generals - Zero Hour/INIZH.big" ) );
	file( at( "macdisk/steamapps/common/Command & Conquer Generals/Textures.big" ) );
}

std::string registry( const char *name, const std::string &lines )
{
	const std::string path = at( name );
	file( path, lines.c_str() );
	return path;
}

PosixInstallRequest request( const std::vector<std::string> &arguments, bool bundle, const char *home,
	const std::string &registryFile )
{
	PosixInstallRequest r;
	r.arguments = arguments;
	r.insideAppBundle = bundle;
	r.executableDirectory = at( "exe" );
	r.home = at( home );
	r.forbidden.push_back( at( "Fake.app" ) );
	r.registryFile = registryFile;
	r.chooser = NULL;
	r.chooserContext = NULL;
	return r;
}

// The stub chooser: answers from `answers` in turn ("" is Cancel) and keeps the reasons it was given
struct Stub
{
	std::vector<std::string> answers;
	std::vector<std::string> reasons;
	std::vector<PosixInstallQuestion> questions;
	size_t next;
};

bool stubChooser( PosixInstallQuestion question, const std::string &why, std::string &chosen, void *context )
{
	Stub *stub = (Stub *)context;
	stub->questions.push_back( question );
	stub->reasons.push_back( why );
	if (stub->next >= stub->answers.size() || stub->answers[stub->next].empty())
		return false;
	chosen = stub->answers[stub->next++];
	return true;
}

}  // namespace

TEST(install_folder_validation_and_its_messages)
{
	build();
	const std::vector<std::string> forbidden( 1, at( "Fake.app" ) );
	CHECK_EQ( PosixCheckInstallFolder( at( "whole" ), forbidden ), INSTALL_OK );
	CHECK_EQ( PosixCheckInstallFolder( at( "siblings/Zero Hour" ), forbidden ), INSTALL_OK );	// "../Command & Conquer Generals"
	CHECK_EQ( PosixCheckInstallFolder( at( "nobase" ), forbidden ), INSTALL_NO_BASE_GAME );
	CHECK_EQ( PosixCheckInstallFolder( at( "notzh" ), forbidden ), INSTALL_NO_ZERO_HOUR );
	CHECK_EQ( PosixCheckInstallFolder( at( "nowhere" ), forbidden ), INSTALL_NOT_A_FOLDER );
	CHECK_EQ( PosixCheckInstallFolder( at( "whole/INIZH.big" ), forbidden ), INSTALL_NOT_A_FOLDER );
	CHECK_EQ( PosixCheckInstallFolder( at( "Fake.app/Contents/Resources/Overlay/zh" ), forbidden ), INSTALL_INSIDE_THE_APP );
	// armed: the same folder is whole when nothing forbids it
	CHECK_EQ( PosixCheckInstallFolder( at( "Fake.app/Contents/Resources/Overlay/zh" ), std::vector<std::string>() ), INSTALL_OK );

	// the base game through Registry.ini's Generals InstallPath, and its First Decade subfolder
	const std::string generals = registry( "generals.ini", "Generals\\InstallPath = " + at( "firstdecade" ) + "\n" );
	CHECK_EQ( PosixCheckInstallFolderWith( at( "nobase" ), forbidden, generals ), INSTALL_OK );
	const std::string elsewhere = registry( "elsewhere.ini", "Generals\\InstallPath = " + at( "notzh" ) + "\n" );
	CHECK_EQ( PosixCheckInstallFolderWith( at( "nobase" ), forbidden, elsewhere ), INSTALL_NO_BASE_GAME );

	// two different failures, two different sentences, each naming what to do
	const std::string noZeroHour = PosixInstallCheckMessage( INSTALL_NO_ZERO_HOUR, "X" );
	const std::string noBase = PosixInstallCheckMessage( INSTALL_NO_BASE_GAME, "X" );
	CHECK( noZeroHour.find( "INIZH.big" ) != std::string::npos );
	CHECK( noBase.find( "Textures.big" ) != std::string::npos && noBase.find( "ZH_Generals" ) != std::string::npos );
	CHECK( noZeroHour != noBase );
	CHECK( PosixInstallCheckMessage( INSTALL_OK, "X" ).empty() );
	printf( "  no Zero Hour: %s\n", noZeroHour.c_str() );
	printf( "  no base game: %s\n", noBase.c_str() );
}

TEST(install_known_places_include_macos_steam_and_external_libraries)
{
	const std::vector<std::string> places = PosixKnownInstallPlaces( at( "machome" ) );
	bool own = false, external = false;
	for (size_t i = 0; i < places.size(); ++i) {
		own |= places[i] == at( "machome/Library/Application Support/Steam/steamapps/common/Command & Conquer Generals Zero Hour" );
		external |= places[i] == at( "macdisk/steamapps/common/Command & Conquer Generals - Zero Hour" );
	}
	CHECK(own);
	CHECK(external);
	PosixInstallRequest r = request( std::vector<std::string>(), true, "machome", "" );
	PosixInstallChoice choice;
	CHECK(PosixChooseInstallRoot(r, choice));
	CHECK_STR(choice.root.c_str(), real(at("macdisk/steamapps/common/Command & Conquer Generals - Zero Hour")).c_str());
}

TEST(install_root_precedence)
{
	const std::string valid = registry( "valid.ini", "InstallPath = " + at( "whole" ) + "\n" );
	const std::string moved = registry( "moved.ini", "InstallPath = " + at( "gone" ) + "\n" );

	// 1. -root wins over a valid Registry.ini, and is used as given, unvalidated
	std::vector<std::string> arguments;
	arguments.push_back( "-headless" );
	arguments.push_back( "-root" );
	arguments.push_back( at( "notzh" ) );
	PosixInstallChoice choice;
	CHECK( PosixChooseInstallRoot( request( arguments, true, "home", valid ), choice ) );
	CHECK_EQ( choice.source, ROOT_FROM_ARGUMENT );
	CHECK_STR( choice.root.c_str(), at( "notzh" ).c_str() );
	CHECK( !choice.writeInstallPath );

	// 2. Registry.ini's InstallPath, while it validates
	CHECK( PosixChooseInstallRoot( request( std::vector<std::string>(), true, "home", valid ), choice ) );
	CHECK_EQ( choice.source, ROOT_FROM_REGISTRY );
	CHECK_STR( choice.root.c_str(), at( "whole" ).c_str() );
	CHECK( !choice.writeInstallPath );

	// 5. an unpacked build with a moved install: the executable's directory, as always
	CHECK( PosixChooseInstallRoot( request( std::vector<std::string>(), false, "home", moved ), choice ) );
	CHECK_EQ( choice.source, ROOT_FROM_EXECUTABLE );
	CHECK_STR( choice.root.c_str(), at( "exe" ).c_str() );
	// ... and without Registry.ini at all
	CHECK( PosixChooseInstallRoot( request( std::vector<std::string>(), false, "home", "" ), choice ) );
	CHECK_EQ( choice.source, ROOT_FROM_EXECUTABLE );

	// 3. inside a bundle: the known places, the first that validates (~/Games' copy has no base game,
	// so the CrossOver bottle's is taken), and nothing written for it
	CHECK( PosixChooseInstallRoot( request( std::vector<std::string>(), true, "home", moved ), choice ) );
	CHECK_EQ( choice.source, ROOT_FROM_KNOWN_PLACE );
	CHECK( choice.root.find( "CrossOver/Bottles/Steam/drive_c" ) != std::string::npos );
	CHECK( !choice.writeInstallPath );

	// a bundle with nothing known and no chooser (headless): no root, and it says how to give one
	CHECK( !PosixChooseInstallRoot( request( std::vector<std::string>(), true, "emptyhome", "" ), choice ) );
	CHECK( choice.problem.find( "-root" ) != std::string::npos );
}

TEST(install_known_places)
{
	const std::vector<std::string> places = PosixKnownInstallPlaces( at( "home" ) );
	CHECK( !places.empty() );
	CHECK_STR( places[0].c_str(), at( "home/Games/Command & Conquer Generals Zero Hour" ).c_str() );
	bool applications = false, bottle = false;
	for (size_t i = 0; i < places.size(); ++i)
	{
		applications = applications || places[i] == "/Applications/Command & Conquer Generals Zero Hour";
		bottle = bottle || places[i].find( "CrossOver/Bottles/Steam/drive_c/Program Files (x86)/Steam/steamapps/common/"
			"Command & Conquer Generals - Zero Hour" ) != std::string::npos;
	}
	CHECK( applications );
	CHECK( bottle );
	// a home without bottles lists the fixed places only
	const std::vector<std::string> plain = PosixKnownInstallPlaces( at( "emptyhome" ) );
	CHECK( plain.size() < places.size() );
}

TEST(install_known_places_include_the_steam_libraries)
{
	const std::string card = at( "sdcard" );
	const std::vector<std::string> places = PosixKnownInstallPlaces( at( "steamhome" ) );
	int own = 0, onCard = 0;
	for (size_t i = 0; i < places.size(); ++i)
	{
		own += places[i] == at( "steamhome/.local/share/Steam/steamapps/common/Command & Conquer Generals Zero Hour" );
		onCard += places[i] == card + "/steamapps/common/Command & Conquer Generals - Zero Hour";
	}
	CHECK_EQ( own, 1 );			// Steam's own library, once though ~/.steam/steam leads to it too
	CHECK_EQ( onCard, 1 );		// the SD card's, from libraryfolders.vdf
	// packaged, nothing registered: found on the card, and nothing is written for it
	PosixInstallRequest r = request( std::vector<std::string>(), true, "steamhome", "" );
	PosixInstallChoice choice;
	CHECK( PosixChooseInstallRoot( r, choice ) );
	CHECK_STR( choice.root.c_str(), real( card + "/steamapps/common/Command & Conquer Generals - Zero Hour" ).c_str() );
	CHECK( choice.source == ROOT_FROM_KNOWN_PLACE );
	CHECK( !choice.writeInstallPath );
	// a home with no Steam lists none of it
	const std::vector<std::string> plain = PosixKnownInstallPlaces( at( "emptyhome" ) );
	for (size_t i = 0; i < plain.size(); ++i)
		CHECK( plain[i].find( "steamapps" ) == std::string::npos );
}

TEST(install_chooser_asks_for_generals_when_zero_hour_has_no_base_game)
{
	// Zero Hour alone (a CD or First Decade install with Generals elsewhere, or a Flatpak, which sees only the
	// folder chosen): the chooser then asks for the Generals folder, until one holds Textures.big
	Stub stub;
	stub.next = 0;
	stub.answers.push_back( at( "nobase" ) );			// Zero Hour, no base game anywhere it looks
	stub.answers.push_back( at( "notzh" ) );			// not Generals
	stub.answers.push_back( at( "Fake.app/Contents/Resources/Overlay/zh/ZH_Generals" ) );	// Generals, inside the app
	stub.answers.push_back( at( "firstdecade" ) );		// First Decade's folder: Generals in its subfolder
	PosixInstallRequest r = request( std::vector<std::string>(), true, "emptyhome", "" );
	r.chooser = stubChooser;
	r.chooserContext = &stub;
	PosixInstallChoice choice;
	CHECK( PosixChooseInstallRoot( r, choice ) );
	CHECK_EQ( choice.source, ROOT_FROM_CHOOSER );
	CHECK_STR( choice.root.c_str(), at( "nobase" ).c_str() );
	CHECK_STR( choice.generals.c_str(), at( "firstdecade" ).c_str() );
	CHECK( choice.writeInstallPath );
	CHECK( choice.writeGeneralsInstallPath );
	CHECK_EQ( stub.questions.size(), (size_t)4 );
	if (stub.questions.size() == 4)
	{
		CHECK_EQ( stub.questions[0], CHOOSE_ZERO_HOUR );
		CHECK_EQ( stub.questions[1], CHOOSE_GENERALS );
		CHECK( stub.reasons[1].find( "holds Zero Hour" ) != std::string::npos );
		CHECK_EQ( stub.questions[2], CHOOSE_GENERALS );
		CHECK( stub.reasons[2].find( "no Textures.big" ) != std::string::npos );
		CHECK_EQ( stub.questions[3], CHOOSE_GENERALS );		// the app's own copy was refused as well
		CHECK( stub.reasons[3].find( "inside the game's own app" ) != std::string::npos );
	}

	// a plain Generals folder, Textures.big at its top
	Stub plain;
	plain.next = 0;
	plain.answers.push_back( at( "nobase" ) );
	plain.answers.push_back( at( "siblings/Command & Conquer Generals" ) );
	r.chooserContext = &plain;
	CHECK( PosixChooseInstallRoot( r, choice ) );
	CHECK_STR( choice.generals.c_str(), at( "siblings/Command & Conquer Generals" ).c_str() );
	CHECK( choice.writeGeneralsInstallPath );

	// cancelled at the Generals question: no root, nothing written, and it says what was missing
	Stub cancel;
	cancel.next = 0;
	cancel.answers.push_back( at( "nobase" ) );
	cancel.answers.push_back( "" );
	r.chooserContext = &cancel;
	CHECK( !PosixChooseInstallRoot( r, choice ) );
	CHECK( !choice.writeInstallPath && !choice.writeGeneralsInstallPath );
	CHECK( choice.problem.find( "Generals folder" ) != std::string::npos );

	// armed: a Zero Hour with its base game inside is never followed by the Generals question
	Stub whole;
	whole.next = 0;
	whole.answers.push_back( at( "whole" ) );
	r.chooserContext = &whole;
	CHECK( PosixChooseInstallRoot( r, choice ) );
	CHECK( whole.questions.size() == 1 && !choice.writeGeneralsInstallPath && choice.generals.empty() );
}

TEST(install_chooser_asks_again_with_the_reason_and_can_be_cancelled)
{
	// 4. inside a bundle, nothing registered or known: the player chooses; wrong answers are said why
	Stub stub;
	stub.next = 0;
	stub.answers.push_back( at( "notzh" ) );			// not Zero Hour
	stub.answers.push_back( at( "Fake.app/Contents/Resources/Overlay/zh" ) );	// inside the app
	stub.answers.push_back( at( "whole" ) );			// right
	PosixInstallRequest r = request( std::vector<std::string>(), true, "emptyhome", "" );
	r.chooser = stubChooser;
	r.chooserContext = &stub;
	PosixInstallChoice choice;
	CHECK( PosixChooseInstallRoot( r, choice ) );
	CHECK_EQ( choice.source, ROOT_FROM_CHOOSER );
	CHECK_STR( choice.root.c_str(), at( "whole" ).c_str() );
	CHECK( choice.writeInstallPath );					// the one thing written, by PosixMain
	CHECK_EQ( stub.reasons.size(), (size_t)3 );
	if (stub.reasons.size() == 3)
	{
		CHECK( stub.reasons[0].empty() );
		CHECK( stub.reasons[1].find( "INIZH.big" ) != std::string::npos );
		CHECK( stub.reasons[2].find( "inside the game's own app" ) != std::string::npos );
	}
	CHECK( !choice.writeGeneralsInstallPath );			// Generals was inside it: never asked for
	for (size_t i = 0; i < stub.questions.size(); ++i)
		CHECK_EQ( stub.questions[i], CHOOSE_ZERO_HOUR );

	// cancelled: no root, nothing to write
	Stub cancel;
	cancel.next = 0;
	cancel.answers.push_back( "" );
	r.chooserContext = &cancel;
	CHECK( !PosixChooseInstallRoot( r, choice ) );
	CHECK( !choice.writeInstallPath );
	CHECK( !choice.problem.empty() );

	// a folder inside the app is refused with its own reason
	Stub inside;
	inside.next = 0;
	inside.answers.push_back( at( "Fake.app/Contents/Resources/Overlay/zh" ) );
	inside.answers.push_back( "" );
	r.chooserContext = &inside;
	CHECK( !PosixChooseInstallRoot( r, choice ) );
	CHECK( inside.reasons.size() == 2 && inside.reasons[1].find( "inside the game's own app" ) != std::string::npos );

	const std::string remove = "rm -rf '" + s_base + "'";
	if (system( remove.c_str() ) != 0)
		printf( "  could not remove %s\n", s_base.c_str() );
}
