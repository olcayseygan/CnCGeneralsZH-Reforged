// A map's rules file (map.ini, solo.ini) is read INI_LOAD_CREATE_OVERRIDES at map load, and every
// store it may write to puts the game's own data back in reset() at the end of the match.  These
// play two maps back to back the way GameLogic does: load the first map's file, reset the store,
// load the second's, and check nothing of the first is left - on a machine that never played it,
// the second match would otherwise run on different rules.

#include "Common/DamageFX.h"
#include "GameLogic/Weapon.h"
#include "GameClient/Snow.h"
#include "GameClient/TerrainVisual.h"

// GameLogic::loadMapINI's load: unknown fields skipped, any throw stops the file there
static Bool loadMapIni( const char *name )
{
	INI ini;
	ini.setSkipUnknownFields( TRUE );
	try
	{
		ini.load( AsciiString( name ), INI_LOAD_CREATE_OVERRIDES, NULL );
		return TRUE;
	}
	catch( ... )
	{
	}
	return FALSE;
}

/* A block whose store cannot put it back is skipped in a map's rules file, nested sub-block and
	 all, and the blocks after it still load. */
TEST(map_ini_refuses_a_block_its_store_cannot_put_back)
{
	CHECK( bootOnce() );

	if( TheArmorStore == NULL )
		TheArmorStore = NEW ArmorStore;

	writeFile( TEST_INI,
		"Armor MapRefuseProbeArmor\r\n"
		"  Armor = SMALL_ARMS 50%\r\n"
		"End\r\n"
		"Video MapRefuseProbeVideo\r\n"
		"  Filename = Probe\r\n"
		"End\r\n"
		"Campaign MapRefuseProbeCampaign\r\n"
		"  CampaignNameLabel = Probe ; a comment with an End in it\r\n"
		"  Mission MISSION01\r\n"
		"    Map = Probe\r\n"
		"  End\r\n"
		"End\r\n"
		"Armor MapRefuseProbeArmorAfter\r\n"
		"  Armor = SMALL_ARMS 50%\r\n"
		"End\r\n" );
	CHECK( loadMapIni( TEST_INI ) );
	CHECK( TheArmorStore->findArmorTemplate( "MapRefuseProbeArmor" ) != NULL );
	CHECK( TheArmorStore->findArmorTemplate( "MapRefuseProbeArmorAfter" ) != NULL );

	TheArmorStore->reset();
	remove( TEST_INI );
}

/* A map's edit of an existing weapon is dropped, as retail dropped it: EA parsed the block into a
	 copy nothing pointed at (WeaponStore::newOverride in the original source), and the campaigns were
	 tuned that way.  Generals USA04's map.ini asks for a 1000 radius CarpetBombWeapon and its cutscene
	 wiped the whole map while the edit applied.  A weapon the map made from nothing is gone after the
	 match, so the second map that defines the same name builds it from a new weapon's defaults. */
TEST(map_ini_weapons_go_back_after_the_match)
{
	CHECK( bootOnce() );

	if( TheWeaponStore == NULL )
		TheWeaponStore = NEW WeaponStore;

	writeFile( TEST_INI,
		"Weapon MapRollbackProbeGun\r\n"
		"  PrimaryDamage = 60.0\r\n"
		"  AttackRange = 150.0\r\n"
		"End\r\n" );
	CHECK( loadIni( TEST_INI ) );

	writeFile( TEST_INI,
		"Weapon MapRollbackProbeGun\r\n"
		"  PrimaryDamage = 999.0\r\n"
		"End\r\n"
		"Weapon MapOnlyProbeGun\r\n"
		"  PrimaryDamage = 5.0\r\n"
		"  AttackRange = 300.0\r\n"
		"End\r\n" );
	CHECK( loadMapIni( TEST_INI ) );

	WeaponBonus noBonus;
	const WeaponTemplate *gun = TheWeaponStore->findWeaponTemplate( "MapRollbackProbeGun" );
	CHECK( gun != NULL );
	if( gun == NULL )
		return;
	CHECK_NEAR( gun->getPrimaryDamage( noBonus ), 60.0f, 0.01f );
	CHECK( TheWeaponStore->findWeaponTemplate( "MapOnlyProbeGun" ) != NULL );

	TheWeaponStore->reset();

	CHECK( TheWeaponStore->findWeaponTemplate( "MapRollbackProbeGun" ) == gun );
	CHECK_NEAR( gun->getPrimaryDamage( noBonus ), 60.0f, 0.01f );
	CHECK_NEAR( gun->getUnmodifiedAttackRange(), 150.0f, 0.01f );
	CHECK( TheWeaponStore->findWeaponTemplate( "MapOnlyProbeGun" ) == NULL );

	// the second map names the first map's weapon without its range: a fresh weapon's, not 300
	writeFile( TEST_INI,
		"Weapon MapOnlyProbeGun\r\n"
		"  PrimaryDamage = 7.0\r\n"
		"End\r\n" );
	CHECK( loadMapIni( TEST_INI ) );
	const WeaponTemplate *again = TheWeaponStore->findWeaponTemplate( "MapOnlyProbeGun" );
	CHECK( again != NULL );
	if( again != NULL )
		CHECK( again->getUnmodifiedAttackRange() < 299.0f );

	TheWeaponStore->reset();
	CHECK( TheWeaponStore->findWeaponTemplate( "MapOnlyProbeGun" ) == NULL );
	remove( TEST_INI );
}

/* DamageFX was rewritten in place and never put back. */
TEST(map_ini_damage_fx_goes_back_after_the_match)
{
	CHECK( bootOnce() );

	if( TheDamageFXStore == NULL )
		TheDamageFXStore = NEW DamageFXStore;

	writeFile( TEST_INI,
		"DamageFX MapRollbackProbeDFX\r\n"
		"  ThrottleTime = Default 100\r\n"
		"End\r\n" );
	CHECK( loadIni( TEST_INI ) );
	const DamageFX *dfx = TheDamageFXStore->findDamageFX( "MapRollbackProbeDFX" );
	CHECK( dfx != NULL );
	if( dfx == NULL )
		return;
	const UnsignedInt before = dfx->getDamageFXThrottleTime( DAMAGE_EXPLOSION, NULL );

	writeFile( TEST_INI,
		"DamageFX MapRollbackProbeDFX\r\n"
		"  ThrottleTime = Default 2000\r\n"
		"End\r\n" );
	CHECK( loadMapIni( TEST_INI ) );
	CHECK( dfx->getDamageFXThrottleTime( DAMAGE_EXPLOSION, NULL ) != before );

	TheDamageFXStore->reset();
	CHECK( TheDamageFXStore->findDamageFX( "MapRollbackProbeDFX" ) == dfx );
	CHECK_EQ( dfx->getDamageFXThrottleTime( DAMAGE_EXPLOSION, NULL ), before );
	remove( TEST_INI );
}

/* A map's BuildCost is what the object costs - the base template answers with the override's price -
	 and it is the game's price again after the match.  BuildTime and BuildCost a platform would read
	 differently (out of range, past an Int) are refused. */
TEST(map_ini_build_cost_is_charged_and_goes_back)
{
	CHECK( bootOnce() );

	GlobalData *savedGlobals = TheWritableGlobalData;
	if( savedGlobals == NULL )
		TheWritableGlobalData = NEW GlobalData;
	if( TheModuleFactory == NULL )
	{
		TheModuleFactory = NEW ModuleFactory;
		TheModuleFactory->init();
	}
	if( TheThingFactory == NULL )
		TheThingFactory = NEW ThingFactory;

	writeFile( TEST_INI,
		"Object MapCostProbe\r\n"
		"  Behavior = DestroyDie ModuleTag_01\r\n"
		"  End\r\n"
		"  BuildCost = 100\r\n"
		"  BuildTime = 10\r\n"
		"End\r\n" );
	CHECK( loadIni( TEST_INI ) );
	const ThingTemplate *probe = TheThingFactory->findTemplate( "MapCostProbe" );
	CHECK( probe != NULL );
	if( probe == NULL )
		return;
	CHECK_EQ( probe->friend_getBuildCost(), 100 );

	writeFile( TEST_INI,
		"Object MapCostProbe\r\n"
		"  BuildCost = 900\r\n"
		"End\r\n" );
	CHECK( loadMapIni( TEST_INI ) );
	CHECK( TheThingFactory->findTemplate( "MapCostProbe" ) == probe );
	CHECK_EQ( probe->friend_getBuildCost(), 900 );

	TheThingFactory->reset();
	CHECK_EQ( probe->friend_getBuildCost(), 100 );

	const char *const refused[] =
	{
		"Object MapCostProbe\r\n  BuildTime = 1000000000\r\nEnd\r\n",
		"Object MapCostProbe\r\n  BuildTime = -1\r\nEnd\r\n",
		"Object MapCostProbe\r\n  BuildCost = 99999999999\r\nEnd\r\n",
		"Object MapCostProbe\r\n  BuildCost = 70000\r\nEnd\r\n",
	};
	// only the line is refused: the file loads, the value stays the game's
	for( Int i = 0; i < (Int)ARRAY_SIZE( refused ); ++i )
	{
		writeFile( TEST_INI, refused[ i ] );
		CHECK( loadMapIni( TEST_INI ) );
		CHECK_EQ( probe->friend_getBuildCost(), 100 );
		TheThingFactory->reset();
	}

	// and in the game's own data it still stops the load
	writeFile( TEST_INI, "Object MapCostProbeStrict\r\n  BuildTime = -1\r\nEnd\r\n" );
	CHECK( loadIni( TEST_INI ) == FALSE );

	remove( TEST_INI );
	if( savedGlobals == NULL )
	{
		delete TheWritableGlobalData;
		TheWritableGlobalData = NULL;
	}
}

/* A special power a map adds takes the next id, and the id is what a power's command carries: the
	 same map gives it the same id whatever map was played before it. */
TEST(map_ini_power_ids_do_not_depend_on_earlier_maps)
{
	CHECK( bootOnce() );

	if( TheSpecialPowerStore == NULL )
		TheSpecialPowerStore = NEW SpecialPowerStore;

	writeFile( TEST_INI,
		"SpecialPower MapIdProbePower\r\n"
		"End\r\n" );
	CHECK( loadMapIni( TEST_INI ) );
	const SpecialPowerTemplate *first = TheSpecialPowerStore->findSpecialPowerTemplate( "MapIdProbePower" );
	CHECK( first != NULL );
	if( first == NULL )
		return;
	const UnsignedInt id = first->getID();
	TheSpecialPowerStore->reset();
	CHECK( TheSpecialPowerStore->findSpecialPowerTemplate( "MapIdProbePower" ) == NULL );

	// another custom map in between, with two powers of its own
	writeFile( TEST_INI,
		"SpecialPower MapIdProbeOtherA\r\n"
		"End\r\n"
		"SpecialPower MapIdProbeOtherB\r\n"
		"End\r\n" );
	CHECK( loadMapIni( TEST_INI ) );
	TheSpecialPowerStore->reset();

	writeFile( TEST_INI,
		"SpecialPower MapIdProbePower\r\n"
		"End\r\n" );
	CHECK( loadMapIni( TEST_INI ) );
	const SpecialPowerTemplate *again = TheSpecialPowerStore->findSpecialPowerTemplate( "MapIdProbePower" );
	CHECK( again != NULL );
	if( again != NULL )
		CHECK_EQ( again->getID(), id );

	TheSpecialPowerStore->reset();
	remove( TEST_INI );
}

/* Snow settings that divided by zero or drew billions of flakes are refused; shipped values pass. */
TEST(map_ini_refuses_extreme_snow)
{
	CHECK( bootOnce() );

	writeFile( TEST_INI,
		"Weather\r\n"
		"  SnowBoxDimensions = 150\r\n"
		"  SnowBoxDensity = 4\r\n"
		"  SnowVelocity = 25\r\n"
		"End\r\n" );
	CHECK( loadMapIni( TEST_INI ) );
	CHECK_NEAR( TheWeatherSetting->m_snowBoxDimensions, 150.0f, 0.001f );

	const char *const refused[] =
	{
		"Weather\r\n  SnowBoxDimensions = 0\r\nEnd\r\n",
		"Weather\r\n  SnowBoxDimensions = 100000\r\nEnd\r\n",
		"Weather\r\n  SnowBoxDensity = 0\r\nEnd\r\n",
		"Weather\r\n  SnowBoxDensity = 1000\r\nEnd\r\n",
		"Weather\r\n  SnowVelocity = 0\r\nEnd\r\n",
	};
	for( Int i = 0; i < (Int)ARRAY_SIZE( refused ); ++i )
	{
		writeFile( TEST_INI, refused[ i ] );
		CHECK( loadMapIni( TEST_INI ) );	// the line goes, the file loads
		CHECK_NEAR( TheWeatherSetting->m_snowBoxDimensions, 150.0f, 0.001f );
		CHECK( TheWeatherSetting->m_snowBoxDensity > 0.0f && TheWeatherSetting->m_snowBoxDensity <= 4.0f );
		CHECK( TheWeatherSetting->m_snowVelocity > 0.0f );
	}

	// what GameLogic::reset does with the map's weather
	WeatherSetting *ws = (WeatherSetting *)TheWeatherSetting.getNonOverloadedPointer();
	TheWeatherSetting = (WeatherSetting *)ws->deleteOverrides();
	remove( TEST_INI );
}

namespace {
// WaterTransparency's real INI parser notifies the terrain renderer even when
// only water colors change. These tests load no terrain or graphics device.
class WaterRuleTestTerrain : public TerrainVisual
{
public:
	void getTerrainColorAt(Real, Real, RGBColor *) override {}
	TerrainType *getTerrainTile(Real, Real) override { return NULL; }
	void enableWaterGrid(Bool) override {}
	void setTerrainTracksDetail() override {}
	void setShoreLineDetail() override {}
	void addFactionBib(Object *, Bool, Real) override {}
	void removeFactionBib(Object *) override {}
	void addFactionBibDrawable(Drawable *, Bool, Real) override {}
	void removeFactionBibDrawable(Drawable *) override {}
	void removeAllBibs() override {}
	void removeBibHighlighting() override {}
	void removeTreesAndPropsForConstruction(const Coord3D *, const GeometryInfo &, Real) override {}
	void addProp(const ThingTemplate *, const Coord3D *, Real) override {}
	void setRawMapHeight(const ICoord2D *, Int) override {}
	Int getRawMapHeight(const ICoord2D *) override { return 0; }
#ifdef DO_SEISMIC_SIMULATIONS
	void updateSeismicSimulations() override {}
	void addSeismicSimulation(const SeismicSimulationNode &) override {}
#endif
	void replaceSkyboxTextures(const AsciiString *[NumSkyboxTextures], const AsciiString *[NumSkyboxTextures]) override {}
};

const char *const WATER_RULE_TEST_INI = "test_gameengine_water_rule_tmp.ini";
class WaterRuleTestFiles : public TestLocalFileSystem
{
public:
	Bool present = FALSE;
	File *openFile(const Char *filename, Int access = 0) override
	{
		if (strcmp(filename, "Data\\INI\\ModernWater.ini") == 0)
			return present ? TestLocalFileSystem::openFile(WATER_RULE_TEST_INI, access) : NULL;
		return TestLocalFileSystem::openFile(filename, access);
	}
};

// The real VFS, INI loader, override chain and material cache are exercised.
// Only the graphics callback and ModernWater.ini's backing file are replaced.
struct WaterRuleFixture
{
	LocalFileSystem *savedFiles = TheLocalFileSystem;
	ArchiveFileSystem *savedArchives = TheArchiveFileSystem;
	GlobalData *savedGlobals = TheWritableGlobalData;
	TerrainVisual *savedTerrain = TheTerrainVisual;
	const WaterTransparencySetting *savedWater = TheWaterTransparency.getNonOverloadedPointer();
	WaterRuleTestFiles files;
	WaterRuleTestTerrain terrain;
	WaterTransparencySetting *base = newInstance(WaterTransparencySetting);

	WaterRuleFixture()
	{
		TheLocalFileSystem = &files;
		TheArchiveFileSystem = NULL;
		TheWritableGlobalData = NULL;
		TheTerrainVisual = &terrain;
		TheWaterTransparency = base;
		loadModernWaterSettings(); // absent through this fixture: deterministic built-in defaults
	}
	~WaterRuleFixture()
	{
		files.present = FALSE;
		loadModernWaterSettings();
		base->deleteInstance(); // also frees this test's map overrides
		TheWaterTransparency = savedWater;
		TheTerrainVisual = savedTerrain;
		TheWritableGlobalData = savedGlobals;
		TheArchiveFileSystem = savedArchives;
		TheLocalFileSystem = savedFiles;
		remove(TEST_INI);
		remove(WATER_RULE_TEST_INI);
	}
	void resetMap()
	{
		TheWaterTransparency = static_cast<WaterTransparencySetting *>(base->deleteOverrides());
	}
	void loadRules(const char *text)
	{
		writeFile(WATER_RULE_TEST_INI, text);
		files.present = TRUE;
		loadModernWaterSettings();
	}
};

void checkWaterMaterial(Real r, Real g, Real b, Real brightness, Real tint, Real pollution, Real bubbles, Real clarity = 0)
{
	Real color[4], parameters[4], optics[4];
	getModernWaterMaterial(color, parameters, optics);
	CHECK_NEAR(color[0], r / 255.0f, .00001f);
	CHECK_NEAR(color[1], g / 255.0f, .00001f);
	CHECK_NEAR(color[2], b / 255.0f, .00001f);
	CHECK_NEAR(color[3], brightness, .00001f);
	CHECK_NEAR(parameters[0], tint, .00001f);
	CHECK_NEAR(parameters[1], pollution, .00001f);
	CHECK_NEAR(parameters[2], bubbles, .00001f);
	CHECK_EQ(parameters[3], 0.0f);
	CHECK_NEAR(optics[0], clarity, .00001f);
	CHECK_EQ(optics[1], 0.0f);
	CHECK_EQ(optics[2], 0.0f);
	CHECK_EQ(optics[3], 0.0f);
}
}

TEST(map_ini_modern_water_resolves_effective_override_without_changing_classic)
{
	CHECK(bootOnce());
	WaterRuleFixture fixture;
	checkWaterMaterial(255, 255, 255, 1, 0, 0, 0, .90f);
	writeFile(TEST_INI,
		"WaterTransparency\n"
		" StandingWaterColor = R:50 G:75 B:50\n"
		" StandingWaterTexture = TSWater03.tga\n"
		" TransparentWaterDepth = 2.0\n"
		" TransparentWaterMinOpacity = 0.8\n"
		"End\n");
	CHECK(loadMapIni(TEST_INI));
	checkWaterMaterial(45, 58, 11, .67f, 1, .80f, 1);
	// Resolve repeatedly through the cache; Classic still sees authored values.
	checkWaterMaterial(45, 58, 11, .67f, 1, .80f, 1);
	CHECK_NEAR(TheWaterTransparency->m_standingWaterColor.red, 50 / 255.0f, .00001f);
	CHECK_NEAR(TheWaterTransparency->m_standingWaterColor.green, 75 / 255.0f, .00001f);
	CHECK_NEAR(TheWaterTransparency->m_standingWaterColor.blue, 50 / 255.0f, .00001f);
	CHECK_STR(TheWaterTransparency->m_standingWaterTexture.str(), "TSWater03.tga");
	CHECK_NEAR(TheWaterTransparency->m_transparentWaterDepth, 2, .00001f);
	CHECK_NEAR(TheWaterTransparency->m_minWaterOpacity, .8f, .00001f);
	CHECK(TheWaterTransparency.getNonOverloadedPointer() == fixture.base);
	CHECK_NEAR(fixture.base->m_standingWaterColor.red, 1, .00001f);

	// The next map can have the same color and a different texture; the cache
	// must notice that change even if it never rendered the reset state.
	fixture.resetMap();
	writeFile(TEST_INI,
		"WaterTransparency\n"
		" StandingWaterColor = R:50 G:75 B:50\n"
		" StandingWaterTexture = OtherWater.tga\n"
		"End\n");
	CHECK(loadMapIni(TEST_INI));
	checkWaterMaterial(50, 75, 50, 1, 1, 0, 0);
	fixture.resetMap();
	checkWaterMaterial(255, 255, 255, 1, 0, 0, 0, .90f);
	writeFile(TEST_INI,
		"WaterTransparency\n"
		" StandingWaterColor = R:255 G:0 B:0\n"
		" StandingWaterTexture = TSWater03.tga\n"
		"End\n");
	CHECK(loadMapIni(TEST_INI));
	checkWaterMaterial(255, 0, 0, 1, 1, 0, 0);
	fixture.resetMap();
	checkWaterMaterial(255, 255, 255, 1, 0, 0, 0, .90f);
}

TEST(map_ini_modern_water_config_reload_replaces_cache_and_falls_back_atomically)
{
	CHECK(bootOnce());
	WaterRuleFixture fixture;
	writeFile(TEST_INI,
		"WaterTransparency\n"
		" StandingWaterColor = R:50 G:75 B:50\n"
		" StandingWaterTexture = TSWater03.tga\n"
		"End\n");
	CHECK(loadMapIni(TEST_INI));
	checkWaterMaterial(45, 58, 11, .67f, 1, .80f, 1);
	const char *const custom =
		"ModernWaterRule Custom\n"
		" MatchColors\n Color = R:50 G:75 B:50\n End\n"
		" MatchTextures\n Texture = TSWater03.tga\n End\n"
		" ReplacementColor = R:120 G:10 B:20\n"
		" Brightness = 0.5\n Pollution = 0.25\n Bubbles = No\nEnd\n";
	fixture.loadRules(custom);
	checkWaterMaterial(120, 10, 20, .5f, 1, .25f, 0);
	fixture.loadRules("; intentionally disabled\n");
	checkWaterMaterial(50, 75, 50, 1, 1, 0, 0);

	// A valid first rule followed by a malformed rule cannot install a partial edit.
	const std::string malformed = std::string(custom) + "ModernWaterRule Broken\nUnknown = 1\nEnd\n";
	fixture.loadRules(malformed.c_str());
	checkWaterMaterial(45, 58, 11, .67f, 1, .80f, 1);
	fixture.loadRules(custom);
	checkWaterMaterial(120, 10, 20, .5f, 1, .25f, 0);
	fixture.files.present = FALSE;
	loadModernWaterSettings();
	checkWaterMaterial(45, 58, 11, .67f, 1, .80f, 1);
}
