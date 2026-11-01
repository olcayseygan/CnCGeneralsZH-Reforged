/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// Snow.cpp ////////////////////////////////////////////////////////////////////////////////
// Snow Rendering implementation
// Author: Mark Wilczynski, July 2003
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the Game
#include "GameClient/Snow.h"
#include "GameClient/View.h"
#include "GameLogic/ScriptEngine.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

SnowManager *TheSnowManager=NULL;

SnowManager::SnowManager()
{
	m_time = 0;
	m_velocity = 1;
	m_isVisible = TRUE;	//default to showing if it's enabled via INI.
	m_weatherOn = FALSE;
	m_weatherKind = WEATHER_KIND_NONE;
	m_weatherWind.zero();
}

void SnowManager::init( void )
{
	//starting heights of each particle
	//TODO: replace this lookup table with some kind of procedural method that takes x,y as input.
	m_startingHeights=NEW Real [ SNOW_NOISE_X * SNOW_NOISE_Y];
	m_time = 0;

	updateIniSettings();
}

void SnowManager::updateIniSettings(void)
{
	Real *dst=m_startingHeights;
	//initialize a table of random starting positions for each particle.
	Int boxDimensions = (Int)TheWeatherSetting->m_snowBoxDimensions;
	for (Int y=0; y<SNOW_NOISE_Y; y++)
	{
		for (Int x=0; x<SNOW_NOISE_X; x++)
		{
			*dst=(Real)(rand()%(boxDimensions));
			dst++;
		}
	}

	m_velocity = TheWeatherSetting->m_snowVelocity;
	m_frequencyScaleX = TheWeatherSetting->m_snowFrequencyScaleX;
	m_frequencyScaleY = TheWeatherSetting->m_snowFrequencyScaleY;
	m_amplitude	= TheWeatherSetting->m_snowAmplitude;	
	m_pointSize = TheWeatherSetting->m_snowPointSize;	
	m_quadSize	= TheWeatherSetting->m_snowQuadSize;		
	m_boxDimensions	= TheWeatherSetting->m_snowBoxDimensions;
	m_emitterSpacing = 1.0f/TheWeatherSetting->m_snowBoxDensity;
	m_maxPointSize = TheWeatherSetting->m_snowMaxPointSize;
	m_minPointSize = TheWeatherSetting->m_snowMinPointSize;

	//Time for snow flake to make it from top to bottom of rendered cube around camera.
	m_fullTimePeriod = m_boxDimensions/m_velocity;
}

/* The Weather option's wind.  Strength 1 is a sandstorm.  Sway and lean are radians over the default
	breeze's 0.055 of each, the smoke's drift is world units per particle update; a smoke column rises
	about twice that, so it leans by some 25 degrees. */
static const Real WEATHER_SWAY = 0.11f;
static const Real WEATHER_LEAN = 0.16f;
static const Real WEATHER_SMOKE_DRIFT = 0.5f;
static const Short WEATHER_BREEZE_PERIOD = LOGICFRAMES_PER_SECOND * 3;
static const Short WEATHER_BREEZE_VERSION = 0x2000;	// past any count a map's scripts reach, so trees notice the switch
static BreezeInfo s_weatherBreeze;

const BreezeInfo &weatherBreezeInfo(void)
{
	return (TheSnowManager != NULL && TheSnowManager->isWeatherOn()) ? s_weatherBreeze : TheScriptEngine->getBreezeInfo();
}

void SnowManager::setWeatherWind(Real strength)
{
	const BreezeInfo &script = TheScriptEngine->getBreezeInfo();
	s_weatherBreeze = script;
	s_weatherBreeze.m_intensity = max(script.m_intensity, WEATHER_SWAY * strength);
	s_weatherBreeze.m_lean = max(script.m_lean, WEATHER_LEAN * strength);
	s_weatherBreeze.m_breezePeriod = min(script.m_breezePeriod, WEATHER_BREEZE_PERIOD);
	// the kind too: a tree buffer outlives its map, and the next map may blow harder
	s_weatherBreeze.m_breezeVersion = script.m_breezeVersion + WEATHER_BREEZE_VERSION + (Short)m_weatherKind;
	m_weatherWind.x = script.m_directionVec.x * WEATHER_SMOKE_DRIFT * strength;
	m_weatherWind.y = script.m_directionVec.y * WEATHER_SMOKE_DRIFT * strength;
	m_weatherWind.z = 0.0f;
}

/* Strength is the most of the colour a pixel takes before the haze's own cap of a half.  The colour
	takes the map's light channel by channel and the strength its brightness: a tan veil at full strength
	took the blue out of Dark Night's moonlight. */
void SnowManager::getWeatherHaze(float colour[3], Real *strength) const
{
	static const float COLOURS[WEATHER_KIND_COUNT][3] = {
		{ 0.0f, 0.0f, 0.0f }, { 0.50f, 0.54f, 0.60f }, { 0.74f, 0.58f, 0.38f }, { 0.82f, 0.86f, 0.92f } };
	static const Real STRENGTHS[WEATHER_KIND_COUNT] = { 0.0f, 0.24f, 0.70f, 0.28f };
	const GlobalData::TerrainLighting &sun = TheGlobalData->m_terrainLighting[TheGlobalData->m_timeOfDay][0];
	const Real red = min(sun.ambient.red + sun.diffuse.red, 1.0f);
	const Real green = min(sun.ambient.green + sun.diffuse.green, 1.0f);
	const Real blue = min(sun.ambient.blue + sun.diffuse.blue, 1.0f);
	colour[0] = COLOURS[m_weatherKind][0] * red;
	colour[1] = COLOURS[m_weatherKind][1] * green;
	colour[2] = COLOURS[m_weatherKind][2] * blue;
	*strength = STRENGTHS[m_weatherKind] * (0.299f * red + 0.587f * green + 0.114f * blue);
}

void SnowManager::setVisible(Bool showWeather)
{
	m_isVisible = showWeather;
}

void SnowManager::reset(void)
{
	m_isVisible = TRUE;	//default to showing if it's enabled via INI.
}

SnowManager::~SnowManager()
{
	delete [] m_startingHeights;
	m_startingHeights=NULL;
}

OVERRIDE<WeatherSetting> TheWeatherSetting = NULL;

/* A map's rules file can carry a Weather block.  A box under one unit made updateIniSettings take
	 rand() modulo zero, a zero density or velocity divided by zero, and the snow drawn each frame is
	 about (box * density) squared flakes, so a big box or density hung the frame.  Shipped data uses
	 boxes of 100 and 200, density 1 to 4 and velocity 2.5 to 25; the limits leave room around that. */
static const INI::RealRange SnowBoxRange = { 1.0f, 400.0f };
static const INI::RealRange SnowDensityRange = { 0.05f, 4.0f };
static const INI::RealRange SnowVelocityRange = { 0.1f, 500.0f };

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
const FieldParse WeatherSetting::m_weatherSettingFieldParseTable[] = 
{
	{ "SnowTexture",							INI::parseAsciiString,NULL,			offsetof( WeatherSetting, m_snowTexture ) },
	{ "SnowFrequencyScaleX",					INI::parseReal,NULL,			offsetof( WeatherSetting, m_snowFrequencyScaleX ) },
	{ "SnowFrequencyScaleY",					INI::parseReal,NULL,			offsetof( WeatherSetting, m_snowFrequencyScaleY ) },
	{ "SnowAmplitude",							INI::parseReal,NULL,			offsetof( WeatherSetting, m_snowAmplitude ) },
	{ "SnowPointSize",							INI::parseReal,NULL,			offsetof( WeatherSetting, m_snowPointSize ) },
	{ "SnowMaxPointSize",						INI::parseReal,NULL,			offsetof( WeatherSetting, m_snowMaxPointSize ) },
	{ "SnowMinPointSize",						INI::parseReal,NULL,			offsetof( WeatherSetting, m_snowMinPointSize ) },
	{ "SnowQuadSize",							INI::parseReal,NULL,			offsetof( WeatherSetting, m_snowQuadSize ) },
	{ "SnowBoxDimensions",						INI::parseRealInRange,&SnowBoxRange,			offsetof( WeatherSetting, m_snowBoxDimensions ) },
	{ "SnowBoxDensity",							INI::parseRealInRange,&SnowDensityRange,			offsetof( WeatherSetting, m_snowBoxDensity ) },
	{ "SnowVelocity",							INI::parseRealInRange,&SnowVelocityRange,			offsetof( WeatherSetting, m_snowVelocity ) },
	{ "SnowPointSprites",						INI::parseBool,NULL,			offsetof( WeatherSetting, m_usePointSprites ) },
	{ "SnowEnabled",							INI::parseBool,NULL,			offsetof( WeatherSetting, m_snowEnabled ) },
	{ 0, 0, 0, 0 },
};

//-------------------------------------------------------------------------------------------------
void INI::parseWeatherDefinition( INI *ini )
{
	if (TheWeatherSetting == NULL) {
		TheWeatherSetting = newInstance(WeatherSetting);
	} else if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES) {
		WeatherSetting* ws = (WeatherSetting*) (TheWeatherSetting.getNonOverloadedPointer());
		WeatherSetting* wsOverride = newInstance(WeatherSetting);
		*wsOverride = *ws;
		// the copy took the base's next-override pointer too; hung off the end of a chain that already
		// has one (map.ini and solo.ini both with a Weather block) it pointed back into the chain, and
		// every walk of it spun for ever
		wsOverride->setNextOverride(NULL);

		// Mark that it is an override.
		wsOverride->markAsOverride();

		ws->friend_getFinalOverride()->setNextOverride(wsOverride);
	} else {
		throw INI_INVALID_DATA;
	}

	WeatherSetting* weatherSet = (WeatherSetting*) (TheWeatherSetting.getNonOverloadedPointer());
	weatherSet = (WeatherSetting*) (weatherSet->friend_getFinalOverride());
	// parse the data
	ini->initFromINI( weatherSet, TheWeatherSetting->getFieldParse() );

	if (TheSnowManager)
		TheSnowManager->updateIniSettings();

	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES) {
		// Check to see if we overrode any textures.
		// If we did, then we need to replace them in the model.
	}
}
