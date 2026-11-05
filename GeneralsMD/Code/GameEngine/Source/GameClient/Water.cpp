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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Water.cpp ////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, December 2001
// Desc:   Map water settings
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/Water.h"
#include "Common/INI.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "Platform/ModernWaterConfig.h"


// GLOBALS ////////////////////////////////////////////////////////////////////////////////////////
WaterSetting WaterSettings[ TIME_OF_DAY_COUNT ];
OVERRIDE<WaterTransparencySetting> TheWaterTransparency = NULL;

namespace {
struct ModernWaterSettings {
	ModernWaterConfig::Rules rules = ModernWaterConfig::Defaults();
	ModernWaterConfig::Material material;
	ModernWaterConfig::Color mapColor;
	std::string mapTexture;
	bool resolved = false;
};

ModernWaterSettings &modernWaterSettings()
{
	static ModernWaterSettings settings;
	return settings;
}
}

void loadModernWaterSettings()
{
	ModernWaterSettings &settings = modernWaterSettings();
	settings.rules = ModernWaterConfig::Defaults();
	settings.resolved = false;
	const char *path = "Data\\INI\\ModernWater.ini";
	File *file = TheFileSystem->openFile(path, File::READ | File::BINARY);
	if (!file) {
		return;
	}
	const Int size = file->size();
	if (size < 0 || size > 1024 * 1024) {
		file->close();
		return;
	}
	std::string contents(size, '\0');
	const bool complete = size == 0 || file->read(&contents[0], size) == size;
	file->close();
	std::string error;
	if (!complete || !ModernWaterConfig::Parse(contents, settings.rules, error)) {
		return;
	}
}

void getModernWaterMaterial(Real color[4], Real parameters[4], Real optics[4])
{
	ModernWaterSettings &settings = modernWaterSettings();
	const RGBColor &authored = TheWaterTransparency->m_standingWaterColor;
	const ModernWaterConfig::Color mapColor = ModernWaterConfig::ByteColor(authored.red, authored.green, authored.blue);
	const std::string mapTexture = TheWaterTransparency->m_standingWaterTexture.str();
	// Compare the effective values, not the override object's address: save restoration and
	// match reset can reuse an allocation. A new map must never inherit a prior material.
	if (!settings.resolved || settings.mapColor.r != mapColor.r || settings.mapColor.g != mapColor.g ||
		settings.mapColor.b != mapColor.b || settings.mapTexture != mapTexture) {
		settings.mapColor = mapColor;
		settings.mapTexture = mapTexture;
		settings.material = ModernWaterConfig::Resolve(settings.rules, mapColor, mapTexture);
		settings.resolved = true;
		DEBUG_LOG(("ModernWater: map color %d,%d,%d texture %s -> %s\n", mapColor.r, mapColor.g,
			mapColor.b, mapTexture.c_str(), settings.material.ruleName.empty() ? "authored tint" : settings.material.ruleName.c_str()));
	}
	const ModernWaterConfig::Material &material = settings.material;
	color[0] = material.color.r / 255.0f;
	color[1] = material.color.g / 255.0f;
	color[2] = material.color.b / 255.0f;
	color[3] = material.brightness;
	parameters[0] = material.tintAmount;
	parameters[1] = material.pollution;
	parameters[2] = material.bubbles ? 1.0f : 0.0f;
	parameters[3] = 0.0f;
	if (optics) {
		optics[0] = material.underwaterClarity;
		optics[1] = optics[2] = optics[3] = 0.0f;
	}
}

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
const FieldParse WaterSetting::m_waterSettingFieldParseTable[] = 
{

	{ "SkyTexture",									INI::parseAsciiString,			NULL, offsetof( WaterSetting, m_skyTextureFile ) },
	{ "WaterTexture",								INI::parseAsciiString,			NULL, offsetof( WaterSetting, m_waterTextureFile ) },
  { "Vertex00Color",							INI::parseRGBAColorInt,			NULL, offsetof( WaterSetting, m_vertex00Diffuse ) },
	{ "Vertex10Color",							INI::parseRGBAColorInt,			NULL, offsetof( WaterSetting, m_vertex10Diffuse ) },
	{ "Vertex01Color",							INI::parseRGBAColorInt,			NULL, offsetof( WaterSetting, m_vertex01Diffuse ) },
  { "Vertex11Color",							INI::parseRGBAColorInt,			NULL, offsetof( WaterSetting, m_vertex11Diffuse ) },
	{ "DiffuseColor",								INI::parseRGBAColorInt,			NULL, offsetof( WaterSetting, m_waterDiffuseColor ) },
  { "TransparentDiffuseColor",		INI::parseRGBAColorInt,			NULL, offsetof( WaterSetting, m_transparentWaterDiffuse ) },
  { "UScrollPerMS",								INI::parseReal,							NULL, offsetof( WaterSetting, m_uScrollPerMs ) },
  { "VScrollPerMS",								INI::parseReal,							NULL, offsetof( WaterSetting, m_vScrollPerMs ) },
	{ "SkyTexelsPerUnit",						INI::parseReal,							NULL, offsetof( WaterSetting, m_skyTexelsPerUnit ) },
  { "WaterRepeatCount",						INI::parseInt,							NULL, offsetof( WaterSetting, m_waterRepeatCount ) },

	{ NULL,													NULL,												NULL, 0 },

};

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
const FieldParse WaterTransparencySetting::m_waterTransparencySettingFieldParseTable[] = 
{

	{ "TransparentWaterDepth",			INI::parseReal,				NULL,			offsetof( WaterTransparencySetting, m_transparentWaterDepth ) },
	{ "TransparentWaterMinOpacity",	INI::parseReal,				NULL,			offsetof( WaterTransparencySetting, m_minWaterOpacity ) },
	{ "StandingWaterColor",	INI::parseRGBColor,			NULL,			offsetof( WaterTransparencySetting, m_standingWaterColor ) },
	{ "StandingWaterTexture",INI::parseAsciiString,		NULL,			offsetof( WaterTransparencySetting, m_standingWaterTexture ) },
	{ "AdditiveBlending", INI::parseBool,				NULL,			offsetof( WaterTransparencySetting, m_additiveBlend) },
	{ "RadarWaterColor", INI::parseRGBColor,			NULL,			offsetof( WaterTransparencySetting, m_radarColor) },
	{ "SkyboxTextureN",							INI::parseAsciiString,NULL,			offsetof( WaterTransparencySetting, m_skyboxTextureN ) },
	{ "SkyboxTextureE",							INI::parseAsciiString,NULL,			offsetof( WaterTransparencySetting, m_skyboxTextureE ) },
	{ "SkyboxTextureS",							INI::parseAsciiString,NULL,			offsetof( WaterTransparencySetting, m_skyboxTextureS ) },
	{ "SkyboxTextureW",							INI::parseAsciiString,NULL,			offsetof( WaterTransparencySetting, m_skyboxTextureW ) },
	{ "SkyboxTextureT",							INI::parseAsciiString,NULL,			offsetof( WaterTransparencySetting, m_skyboxTextureT ) },


	{ 0, 0, 0, 0 },
};


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WaterSetting::WaterSetting( void )
{

	m_skyTextureFile.clear();
	m_waterTextureFile.clear();
	m_waterRepeatCount = 0;
	m_skyTexelsPerUnit = 0.0f;

	m_vertex00Diffuse.red = 0;
	m_vertex00Diffuse.green = 0; 
	m_vertex00Diffuse.blue = 0; 
	m_vertex00Diffuse.alpha = 0;

	m_vertex01Diffuse.red = 0; 
	m_vertex01Diffuse.green = 0; 
	m_vertex01Diffuse.blue = 0; 
	m_vertex01Diffuse.alpha = 0;

	m_vertex10Diffuse.red = 0; 
	m_vertex10Diffuse.green = 0; 
	m_vertex10Diffuse.blue = 0; 
	m_vertex10Diffuse.alpha = 0;

	m_vertex11Diffuse.red = 0; 
	m_vertex11Diffuse.green = 0; 
	m_vertex11Diffuse.blue = 0; 
	m_vertex11Diffuse.alpha = 0;

	m_waterDiffuseColor.red = 0;
	m_waterDiffuseColor.green = 0;
	m_waterDiffuseColor.blue = 0;
	m_waterDiffuseColor.alpha = 0;

	m_transparentWaterDiffuse.red = 0;
	m_transparentWaterDiffuse.green = 0;
	m_transparentWaterDiffuse.blue = 0;
	m_transparentWaterDiffuse.alpha = 0;

	m_uScrollPerMs = 0.0f;
	m_vScrollPerMs = 0.0f;

}  // end WaterSetting

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WaterSetting::~WaterSetting( void )
{

}  // end WaterSetting


