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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DControlBar.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day
// Desc: Control bar callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////
#include "Common/GlobalData.h"
#include "Common/Radar.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/GameWindow.h"
#include "W3DDevice/GameClient/W3DGameWindow.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"
#include "GameClient/VideoPlayer.h"
#include "GameLogic/GameLogic.h"
#include "Common/LogicThread.h"
#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------
/** LOGIC-THREAD.md phase 3, step 3b.  The window repaint inside the overlap window asks the players
	* what to paint: the radar, the power bar, the general's rank bar, whether the player is still in.
	* The tick changes all of it beside the render, so the sync point takes it here and the painters
	* read this from inside the window. */
//-------------------------------------------------------------------------------------------------
namespace
{
	struct HudReads
	{
		Bool hasRadar;				///< the local player's
		Bool active;					///< the local player's
		Int rankProgress;			///< the local player's
		Int production;				///< the power shown: the observed player's, or the local one's
		Int consumption;
	};
	HudReads s_hudReads;

	Player *powerPlayer( void )
	{
		if( TheControlBar->isObserverControlBarOn() )
			return TheControlBar->getObserverLookAtPlayer();
		return ThePlayerList->getLocalPlayer();
	}
}

void W3DControlBar_takeReadsForRender( void )
{
	// no tick runs beside a render outside a match, and before the first one there is no local player
	if( !TheGameLogic->isInGame() )
		return;
	Player *local = ThePlayerList->getLocalPlayer();
	s_hudReads.hasRadar = local->hasRadar();
	s_hudReads.active = local->isPlayerActive();
	s_hudReads.rankProgress = local->getRankProgressPercent();
	const Player *shown = powerPlayer();
	s_hudReads.production = shown ? shown->getEnergy()->getProduction() : 0;
	s_hudReads.consumption = shown ? shown->getEnergy()->getConsumption() : 0;
}

static Bool hasRadarForDraw( const Player *player )
{
	return LogicThread::isInsideWindow() ? s_hudReads.hasRadar : player->hasRadar();
}

static Bool isActiveForDraw( const Player *player )
{
	return LogicThread::isInsideWindow() ? s_hudReads.active : player->isPlayerActive();
}

static void readPowerForDraw( const Player *player, Int &production, Int &consumption )
{
	if( LogicThread::isInsideWindow() )
	{
		production = s_hudReads.production;
		consumption = s_hudReads.consumption;
		return;
	}
	production = player->getEnergy()->getProduction();
	consumption = player->getEnergy()->getConsumption();
}

//-------------------------------------------------------------------------------------------------
void W3DCameoMovieDraw( GameWindow *window, WinInstanceData *instData )
{
	// draw video buffer
	VideoBuffer *video = TheInGameUI->cameoVideoBuffer();
	if ( video )
	{
		ICoord2D pos, size;
		window->winGetScreenPosition( &pos.x, &pos.y );
		window->winGetSize( &size.x, &size.y );

		TheDisplay->drawVideoBuffer( video, pos.x, pos.y, pos.x + size.x, pos.y + size.y );
	}
}  // end W3DLeftHUDDraw


//-------------------------------------------------------------------------------------------------
void W3DLeftHUDDraw( GameWindow *window, WinInstanceData *instData )
{
	// draw the default stuff
//
	Player *player = ThePlayerList->getLocalPlayer();
	// draw video buffer
	VideoBuffer *video = TheInGameUI->videoBuffer();
	if ( video )
	{
		ICoord2D pos, size;
		window->winGetScreenPosition( &pos.x, &pos.y );
		window->winGetSize( &size.x, &size.y );

		// the window takes the map's shape (ControlBar::fitRadarToMap) and the movie keeps its own,
		// centred, with the frame's glass in the bands
		const Real scale = MIN( (Real)size.x / video->width(), (Real)size.y / video->height() );
		const Int movieWidth = REAL_TO_INT( video->width() * scale );
		const Int movieHeight = REAL_TO_INT( video->height() * scale );
		const Int movieX = pos.x + ( size.x - movieWidth ) / 2;
		const Int movieY = pos.y + ( size.y - movieHeight ) / 2;
		TheDisplay->drawVideoBuffer( video, movieX, movieY, movieX + movieWidth, movieY + movieHeight );
	}
	else if( TheRadar->isRadarForced() || ( TheRadar->isRadarHidden() == false && hasRadarForDraw( player ) ) )
	{
		ICoord2D pos, size;
		//W3DGameWinDefaultDraw( window, instData );
		// window position and size on the display
		window->winGetScreenPosition( &pos.x, &pos.y );
		window->winGetSize( &size.x, &size.y );
		//TheDisplay->drawOpenRect(pos.x, pos.y, size.x,size.y, 1,GameMakeColor(100,100,255,255));
		// draw the radar on the screen now
		TheRadar->draw( pos.x + 1, pos.y + 1, size.x - 2, size.y - 2 );

	}  // end else if


}  // end W3DLeftHUDDraw

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRightHUDDraw( GameWindow *window, WinInstanceData *instData )
{

	// draw the default stuff
	if( BitTest(window->winGetStatus(), WIN_STATUS_IMAGE ))
		W3DGameWinDefaultDraw( window, instData );
	
}  // end W3DRightHUDDraw

Real logN(Real value, Real logBase)
{
	return (Real)log10(value)/ log10(logBase);
}

//-------------------------------------------------------------------------------------------------
/** Temporary power meter drawing ... there are some problems with this meter, that is
	* what to do when the player doesn't have power, builds a building before a power plant
	* etc, but I don't want to spend time on this since it's not a task right now and I'm
	* sure it will be in the future.  This is just down and dirty to see SOMETHING  (CD) */
//-------------------------------------------------------------------------------------------------
void W3DPowerDraw( GameWindow *window, WinInstanceData *instData )
{
	//static const Image *endBarYellow = TheMappedImageCollection->findImageByName("PowerBarYellowEndR");
	//static const Image *beginBarYellow = TheMappedImageCollection->findImageByName("PowerBarYellowEndL");
	static const Image *centerBarYellow = TheMappedImageCollection->findImageByName("PowerPointY");
	//static const Image *endBarRed = TheMappedImageCollection->findImageByName("PowerBarRedEndR");
	//static const Image *beginBarRed = TheMappedImageCollection->findImageByName("PowerBarRedEndL");
	static const Image *centerBarRed = TheMappedImageCollection->findImageByName("PowerPointR");
	//static const Image *endBarGreen = TheMappedImageCollection->findImageByName("PowerBarGreenEndR");
	//static const Image *beginBarGreen = TheMappedImageCollection->findImageByName("PowerBarGreenEndL");
	static const Image *centerBarGreen = TheMappedImageCollection->findImageByName("PowerPointG");
	//const Image *endBar = NULL;
	//const Image *beginBar = NULL;
	const Image *centerBar = NULL;
	static const Image *slider = TheMappedImageCollection->findImageByName("PowerBarSlider");
	Player *player = NULL;
	if(TheControlBar->isObserverControlBarOn())
	{
		player = TheControlBar->getObserverLookAtPlayer();
	}
	else
		player = ThePlayerList->getLocalPlayer();
	


	if(!player || !TheGlobalData)
		return;
	Int consumption, production;
	readPowerForDraw( player, production, consumption );

	ICoord2D pos, size;
	window->winGetScreenPosition( &pos.x, &pos.y );
	window->winGetSize( &size.x, &size.y );

	static Real pixelsPerInterval = size.x / TheGlobalData->m_powerBarIntervals;
	Int delta = TheGlobalData->m_powerBarYellowRange;
	
	if((consumption > production - delta) && (consumption <= production))
	{
		// 6 and 1 is Green, 6 and 2 is yellow, 6 and 6 is yellow
		//endBar = endBarYellow;
		//beginBar = beginBarYellow;
		centerBar = centerBarYellow;
	}
	else if( consumption > production)
	{
	//	endBar = endBarRed;
//		beginBar = beginBarRed;
		centerBar = centerBarRed;
	}
	else
	{
	//	endBar = endBarGreen;
//		beginBar = beginBarGreen;
		centerBar = centerBarGreen;
	}
	//slider = TheMappedImageCollection->findImageByName("PowerBarSlider");
	if( !slider || !centerBar)
		return;

	Int range;
	range = logN(production, TheGlobalData->m_powerBarBase) * (size.x / TheGlobalData->m_powerBarIntervals);
	if(range >= size.x)
		range = size.x;
//	if(range < endBar->getImageWidth() + beginBar->getImageWidth())
//		range = endBar->getImageWidth() + beginBar->getImageWidth();



//	// get image sizes for the ends
//	ICoord2D leftSize, rightSize, start, end;
//	leftSize.x = beginBar->getImageWidth();
//	leftSize.y = beginBar->getImageHeight();
//	rightSize.x = endBar->getImageWidth();
//	rightSize.y = endBar->getImageHeight();
//
//	// get two key points used in the end drawing
//	ICoord2D leftEnd, rightStart;
//	leftEnd.x = pos.x + leftSize.x;
//	leftEnd.y = pos.y + size.y;
//	rightStart.x = pos.x + range - rightSize.x;
//	rightStart.y = pos.y;

	// draw the center repeating bar
	Int centerWidth, pieces;

	// get width we have to draw our repeating center in
	centerWidth = range; //rightStart.x - leftEnd.x;
	
	if( centerWidth > 0)
	{
		
		// how many whole repeating pieces will fit in that width
		pieces = centerWidth / centerBar->getImageWidth();
		ICoord2D start, end;
		// draw the pieces
		start.x = pos.x;
		start.y = pos.y;
		end.y = start.y + size.y;// + yOffset;
		for( Int i = 0; i < pieces; i++ )
		{

			end.x = start.x + centerBar->getImageWidth();
			TheWindowManager->winDrawImage( centerBar, 
																			start.x, start.y,
																			end.x, end.y );
			start.x += centerBar->getImageWidth();

		}  // end for i

		// we will draw the image but clip the parts we don't want to show
		IRegion2D reg;
		reg.lo.x = start.x;
		reg.lo.y = start.y;
		reg.hi.x = pos.x + size.x;//rightStart.x;
		reg.hi.y = pos.y + size.y;
		centerWidth = pos.x +size.x  - start.x;
		if( centerWidth > 0)
		{
			TheDisplay->setClipRegion(&reg);
			end.x = start.x + centerBar->getImageWidth();
			TheWindowManager->winDrawImage( centerBar,
																			start.x, start.y,
																			end.x, end.y );
			TheDisplay->enableClipping(FALSE);
		}

//		// draw left end
//		start.x = pos.x;
//		start.y = pos.y;
//		end = leftEnd;
//		TheWindowManager->winDrawImage(beginBar, start.x, start.y, end.x, end.y);
//
//		// draw right end
//		start = rightStart;
//		end.x = start.x + rightSize.x;
//		end.y = start.y + size.y;
//		TheWindowManager->winDrawImage(endBar, start.x, start.y, end.x, end.y);
	}
	Int posXstart;
	Int posXend;
	Real consumptionForNeedle = (consumption == 1) ? 1.5f : INT_TO_REAL(consumption);//Log(1) == 0, but we need to show something for 1 power used.
	range = logN(consumptionForNeedle, TheGlobalData->m_powerBarBase) * (size.x / TheGlobalData->m_powerBarIntervals);
	if(centerWidth <= 0 && range <= 0)
		return;
	if(range >= size.x)
	{
		posXstart = pos.x + size.x - slider->getImageWidth();
		posXend = pos.x + size.x;
	}
	else
	{
		posXstart = pos.x + range - slider->getImageWidth()/2;
		posXend = pos.x + range + slider->getImageWidth()/2;
	}
	if(posXstart <=pos.x)
	{
		posXstart	 = pos.x;
		posXend	= pos.x + slider->getImageWidth();
	}
	TheWindowManager->winDrawImage(slider, posXstart, pos.y + size.y - slider->getImageHeight(), posXend, pos.y + size.y);
}

void W3DPowerDrawA( GameWindow *window, WinInstanceData *instData )
{
	static const Image *endBarYellow = TheMappedImageCollection->findImageByName("PowerBarYellowEndR");
	static const Image *beginBarYellow = TheMappedImageCollection->findImageByName("PowerBarYellowEndL");
	static const Image *centerBarYellow = TheMappedImageCollection->findImageByName("PowerBarYellow");
	static const Image *endBarRed = TheMappedImageCollection->findImageByName("PowerBarRedEndR");
	static const Image *beginBarRed = TheMappedImageCollection->findImageByName("PowerBarRedEndL");
	static const Image *centerBarRed = TheMappedImageCollection->findImageByName("PowerBarRed");
	static const Image *endBarGreen = TheMappedImageCollection->findImageByName("PowerBarGreenEndR");
	static const Image *beginBarGreen = TheMappedImageCollection->findImageByName("PowerBarGreenEndL");
	static const Image *centerBarGreen = TheMappedImageCollection->findImageByName("PowerBarGreen");
	const Image *endBar = NULL;
	const Image *beginBar = NULL;
	const Image *centerBar = NULL;
	static const Image *slider = TheMappedImageCollection->findImageByName("PowerBarSlider");
	Player *player = NULL;
	if(TheControlBar->isObserverControlBarOn())
	{
		player = TheControlBar->getObserverLookAtPlayer();
	}
	else
		player = ThePlayerList->getLocalPlayer();
	


	if(!player || !TheGlobalData)
		return;
	Int consumption, production;
	readPowerForDraw( player, production, consumption );

	ICoord2D pos, size;
	window->winGetScreenPosition( &pos.x, &pos.y );
	window->winGetSize( &size.x, &size.y );

	static Real pixelsPerInterval = size.x / TheGlobalData->m_powerBarIntervals;
	Int delta = TheGlobalData->m_powerBarYellowRange;
	
	if((consumption > production - delta) && (consumption <= production))
	{
		// 6 and 1 is Green, 6 and 2 is yellow, 6 and 6 is yellow
		endBar = endBarYellow;
		beginBar = beginBarYellow;
		centerBar = centerBarYellow;
	}
	else if( consumption > production)
	{
		endBar = endBarRed;
		beginBar = beginBarRed;
		centerBar = centerBarRed;
	}
	else
	{
		endBar = endBarGreen;
		beginBar = beginBarGreen;
		centerBar = centerBarGreen;
	}
	//slider = TheMappedImageCollection->findImageByName("PowerBarSlider");
	if( !slider || !endBar || !beginBar || !centerBar)
		return;

	Int range;
	range = logN(production, TheGlobalData->m_powerBarBase) * (size.x / TheGlobalData->m_powerBarIntervals);
	if(range >= size.x)
		range = size.x;
	if(range < endBar->getImageWidth() + beginBar->getImageWidth())
		range = endBar->getImageWidth() + beginBar->getImageWidth();



	// get image sizes for the ends
	ICoord2D leftSize, rightSize, start, end;
	leftSize.x = beginBar->getImageWidth();
	leftSize.y = beginBar->getImageHeight();
	rightSize.x = endBar->getImageWidth();
	rightSize.y = endBar->getImageHeight();

	// get two key points used in the end drawing
	ICoord2D leftEnd, rightStart;
	leftEnd.x = pos.x + leftSize.x;
	leftEnd.y = pos.y + size.y;
	rightStart.x = pos.x + range - rightSize.x;
	rightStart.y = pos.y;

	// draw the center repeating bar
	Int centerWidth, pieces;

	// get width we have to draw our repeating center in
	centerWidth = rightStart.x - leftEnd.x;
	
	if( centerWidth <= 0)
	{
		// draw left end
		start.x = pos.x;
		start.y = pos.y;
		end.y = leftEnd.y;
		end.x = pos.x + range/2;
		TheWindowManager->winDrawImage(beginBar, start.x, start.y, end.x, end.y);

		// draw right end
		start.y = rightStart.y;
		start.x = end.x;
		end.x = pos.x + range;
		end.y = start.y + size.y;
		TheWindowManager->winDrawImage(endBar, start.x, start.y, end.x, end.y);
	}
	else
	{
		
		// how many whole repeating pieces will fit in that width
		pieces = centerWidth / centerBar->getImageWidth();

		// draw the pieces
		start.x = leftEnd.x;
		start.y = pos.y;
		end.y = start.y + size.y; //centerImage->getImageHeight() + yOffset;
		for( Int i = 0; i < pieces; i++ )
		{

			end.x = start.x + centerBar->getImageWidth();
			TheWindowManager->winDrawImage( centerBar, 
																			start.x, start.y,
																			end.x, end.y );
			start.x += centerBar->getImageWidth();

		}  // end for i

		// we will draw the image but clip the parts we don't want to show
		IRegion2D reg;
		reg.lo.x = start.x;
		reg.lo.y = start.y;
		reg.hi.x = rightStart.x;
		reg.hi.y = end.y;
		centerWidth = rightStart.x - start.x;
		if( centerWidth > 0)
		{
			TheDisplay->setClipRegion(&reg);
			end.x = start.x + centerBar->getImageWidth();
			TheWindowManager->winDrawImage( centerBar,
																			start.x, start.y,
																			end.x, end.y );
			TheDisplay->enableClipping(FALSE);
		}

		// draw left end
		start.x = pos.x;
		start.y = pos.y;
		end = leftEnd;
		TheWindowManager->winDrawImage(beginBar, start.x, start.y, end.x, end.y);

		// draw right end
		start = rightStart;
		end.x = start.x + rightSize.x;
		end.y = start.y + size.y;
		TheWindowManager->winDrawImage(endBar, start.x, start.y, end.x, end.y);
	}
	Int posXstart;
	Int posXend;
	Real consumptionForNeedle = (consumption == 1) ? 1.5f : INT_TO_REAL(consumption);//Log(1) == 0, but we need to show something for 1 power used.
	range = logN(consumptionForNeedle, TheGlobalData->m_powerBarBase) * (size.x / TheGlobalData->m_powerBarIntervals);
	if(range >= size.x)
	{
		posXstart = pos.x + size.x - slider->getImageWidth();
		posXend = pos.x + size.x;
	}
	else
	{
		posXstart = pos.x + range - slider->getImageWidth()/2;
		posXend = pos.x + range + slider->getImageWidth()/2;
	}
	if(posXstart <=pos.x)
	{
		posXstart	 = pos.x;
		posXend	= pos.x + slider->getImageWidth();
	}
	TheWindowManager->winDrawImage(slider, posXstart, pos.y + size.y - slider->getImageHeight(), posXend, pos.y + size.y);
}

void W3DCommandBarGridDraw( GameWindow *window, WinInstanceData *instData )
{
	if( BitTest(window->winGetStatus(), WIN_STATUS_IMAGE ))
	{
		W3DGameWinDefaultDraw( window, instData );
		return;
	}

	ICoord2D pos, size;
	window->winGetScreenPosition( &pos.x, &pos.y );
	window->winGetSize( &size.x, &size.y );
		
	Color color = TheControlBar->getBorderColor();
	window->winSetEnabledBorderColor(0, color);
	W3DGameWinDefaultDraw( window, instData );

	TheDisplay->drawLine(pos.x, pos.y + size.y * .33, pos.x + size.x, pos.y+ size.y*.33, 1,color);
	TheDisplay->drawLine(pos.x, pos.y + size.y * .66, pos.x + size.x, pos.y+ size.y*.66, 1,color);
	TheDisplay->drawLine(pos.x + size.x * .33, pos.y, pos.x + size.x *.33, pos.y +size.y, 1,color);
	TheDisplay->drawLine(pos.x + size.x  * .66, pos.y, pos.x + size.x *.66, pos.y + size.y, 1,color);

	


}

void W3DCommandBarGenExpDraw( GameWindow *window, WinInstanceData *instData )
{
	Player *player = ThePlayerList->getLocalPlayer();
	if(!isActiveForDraw( player ))
		return;
	static const Image *endBar = TheMappedImageCollection->findImageByName("GenExpBarTop1");
	static const Image *beginBar = TheMappedImageCollection->findImageByName("GenExpBarBottom1");
	static const Image *centerBar = TheMappedImageCollection->findImageByName("GenExpBar1");
	const Int progress = LogicThread::isInsideWindow() ? s_hudReads.rankProgress : player->getRankProgressPercent();

	if(progress <= 0)
		return;

	ICoord2D pos, size;
	window->winGetScreenPosition( &pos.x, &pos.y );
	window->winGetSize( &size.x, &size.y );



	if( !endBar || !beginBar || !centerBar)
		return;

	Int range;
	range = size.y * progress / 100;


	// get image sizes for the ends
	ICoord2D topSize, bottomSize, start, end;
	bottomSize.x = beginBar->getImageWidth();
	bottomSize.y = beginBar->getImageHeight();
	topSize.x = endBar->getImageWidth();
	topSize.y = endBar->getImageHeight();

	// get two key points used in the end drawing
	ICoord2D bottomEnd, topStart;
	bottomEnd.x = pos.x + size.x;
	bottomEnd.y = pos.y + size.y - bottomSize.y;
	topStart.x = pos.x;
	topStart.y = pos.y +size.y - range - topSize.y;

	// draw the center repeating bar
	Int centerWidth, pieces;

	// get width we have to draw our repeating center in
	centerWidth = bottomEnd.y - topStart.y;
	
	if( centerWidth <= 0)
	{
		// draw left end
		start.x = pos.x;
		start.y = pos.y + size.y - bottomSize.y;
		end.y = pos.y + size.y;
		end.x = pos.x + size.x;
		TheWindowManager->winDrawImage(beginBar, start.x, start.y, end.x, end.y);

		// draw right end
		start.y = pos.y + size.y - bottomSize.y - topSize.y;
		start.x = pos.x;
		end.x = pos.x + size.x;
		end.y = start.y + topSize.y;
		TheWindowManager->winDrawImage(endBar, start.x, start.y, end.x, end.y);
	}
	else
	{
		
		// how many whole repeating pieces will fit in that width
		pieces = centerWidth / centerBar->getImageHeight();

		// draw the pieces
		start.x = pos.x;
		start.y = topStart.y;
		end.x = start.x + size.x; //centerImage->getImageHeight() + yOffset;
		for( Int i = 0; i < pieces; i++ )
		{

			end.y = start.y + centerBar->getImageHeight();
			TheWindowManager->winDrawImage( centerBar, 
																			start.x, start.y,
																			end.x, end.y );
			start.y += centerBar->getImageHeight();

		}  // end for i

		// we will draw the image but clip the parts we don't want to show
		IRegion2D reg;
		reg.lo.x = start.x;
		reg.lo.y = start.y;
		reg.hi.x = bottomEnd.x;
		reg.hi.y = bottomEnd.y;
		centerWidth = bottomEnd.y - start.y;
		if( centerWidth > 0)
		{
			TheDisplay->setClipRegion(&reg);
			end.y = start.y + centerBar->getImageHeight();
			TheWindowManager->winDrawImage( centerBar,
																			start.x, start.y,
																			end.x, end.y );
			TheDisplay->enableClipping(FALSE);
		}

		// draw left end
		end.x = pos.x + size.x;
		end.y = pos.y + size.y;
		start.x = pos.x;
		start.y = bottomEnd.y;
		TheWindowManager->winDrawImage(beginBar, start.x, start.y, end.x, end.y);

		// draw right end
		start.x = pos.x;
		start.y = pos.y +size.y - range;
		end.x = pos.x + size.x;
		end.y = pos.y +size.y - range - topSize.y;
		TheWindowManager->winDrawImage(endBar, start.x, start.y, end.x, end.y);
	}

}

void W3DCommandBarTopDraw( GameWindow *window, WinInstanceData *instData )
{
	GameWindow *win = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonGeneral"));
	if(!win || win->winIsHidden() || !isActiveForDraw( ThePlayerList->getLocalPlayer() ))
		return;
	
//	TheControlBar->drawTransitionHandler();

//	ICoord2D pos, size, imageStart, imageEnd;
//	win->winGetScreenPosition(&pos.x, &pos.y);
//	win->winGetSize(&size.x, &size.y);
////	const Image *image =TheControlBar->getStarImage();
//	imageStart.x = pos.x	+ size.x/2 - image->getImageWidth()/2;
//	imageStart.y = pos.y	+ size.y/2 - image->getImageHeight()/2;
//	imageEnd.x = imageStart.x + image->getImageWidth();
//	imageEnd.y = imageStart.y + image->getImageHeight();
//	TheDisplay->drawImage(TheControlBar->getStarImage(), imageStart.x, imageStart.y, imageEnd.x, imageEnd.y);
}


//-------------------------------------------------------------------------------------------------
/** The Image for one plate.  These are not in any MappedImage INI - the whole targa is the image
	* and nothing else uses it - so the first draw builds one and hands it to the mapped image
	* collection, which owns it from then on. */
//-------------------------------------------------------------------------------------------------
static const Image *plateImage( const ControlBarPlate *plate )
{
	if( TheMappedImageCollection == NULL )
		return NULL;

	AsciiString name( plate->filename );
	const Image *found = TheMappedImageCollection->findImageByName( name );
	if( found )
		return found;

	Image *image = newInstance( Image );
	image->setName( name );
	image->setFilename( name );

	//
	// Nothing else uses this targa, so there is no page to look the image up in - but there is
	// padding.  The painting is artW by artH texels in a texture rounded up to the next power of
	// two, because the loader hands D3DXCreateTextureFromFileEx D3DX_DEFAULT for both dimensions and
	// that *stretches* a non-power-of-two image into the bigger texture.  That stretch left the
	// texture's last column holding a vertically mirrored copy of the painting, and the last pixel
	// column of every quad drawn from it sampled that column: a one-pixel wire down the right edge
	// of a plate, and where the mirrored row happened to be opaque, a lit speck standing out in the
	// battlefield.  Padded to a power of two there is no resize and no mirrored column.
	//
	// So the UV is the painting's own corner of the texture, half a texel in at each edge, which is
	// the mapping that puts the quad's first and last pixel centres on the first and last texel
	// centres.
	//
	Real potW = 1.0f, potH = 1.0f;
	while( potW < (Real)plate->artW ) potW *= 2.0f;
	while( potH < (Real)plate->artH ) potH *= 2.0f;

	Region2D uv;
	uv.lo.x = 0.5f / potW;
	uv.lo.y = 0.5f / potH;
	uv.hi.x = ( (Real)plate->artW - 0.5f ) / potW;
	uv.hi.y = ( (Real)plate->artH - 0.5f ) / potH;
	image->setUV( &uv );

	ICoord2D size;
	size.x = plate->design.width();
	size.y = plate->design.height();
	image->setTextureWidth( size.x );
	image->setTextureHeight( size.y );
	image->setImageSize( &size );

	TheMappedImageCollection->addImage( image );
	return image;
}

//-------------------------------------------------------------------------------------------------
/** Each panel wears a plate of its own now - see ControlBarPlateForSide in ControlBar.cpp for what
	* a plate is and why the scheme's one-piece painting is not drawn at all. */
//-------------------------------------------------------------------------------------------------
void W3DCommandBarBackgroundDraw( GameWindow *window, WinInstanceData *instData )
{
	ControlBarSchemeManager *man = TheControlBar ? TheControlBar->getControlBarSchemeManager() : NULL;
	if( man == NULL || TheDisplay == NULL )
		return;

	//
	// The plates are drawn from the design rectangles rather than from a window, so on their own
	// they stand still while the bar moves: minimising it drops the frame a tenth of the screen and
	// the intro slides the frame up from below, and both left the painting hanging where it was.
	// This window is a child of that frame, so the frame's screen position now against where
	// layoutPanels put it is exactly how far the whole bar has travelled.
	//
	ICoord2D offset;
	offset.x = offset.y = 0;
	GameWindow *frame = window ? window->winGetParent() : NULL;
	if( frame )
	{
		ICoord2D now;
		frame->winGetScreenPosition( &now.x, &now.y );
		offset.x = now.x - TheControlBar->getPanelOrigin()->x;
		offset.y = now.y - TheControlBar->getPanelOrigin()->y;
	}

	// the bar is ControlBar Pro's painting, not our plates
	if( man->isCurrentSchemePainted() )
	{
		man->drawPainting( offset, FALSE );
		return;
	}

	for( Int p = 0; p < ControlBar::CB_PANEL_COUNT; p++ )
	{
		const ControlBarPlate *plate = ControlBarPlateForSide( man->getCurrentSide(), p );
		if( plate == NULL )
			plate = ControlBarPlateForSide( man->getCurrentArtTwinSide(), p );
		if( plate == NULL )
			continue;

		IRegion2D rect;
		if( ControlBarPanelDesignToScreen( p, &plate->design, TheDisplay->getWidth(),
																			 TheDisplay->getHeight(), &rect ) == FALSE )
			continue;

		const Image *image = plateImage( plate );
		if( image )
			TheDisplay->drawImage( image, rect.lo.x + offset.x, rect.lo.y + offset.y, rect.hi.x + offset.x, rect.hi.y + offset.y );
	}
}


void W3DCommandBarForegroundDraw( GameWindow *window, WinInstanceData *instData )
{
	// the scheme's foreground layers, moved with the bar the same way as W3DCommandBarBackgroundDraw's
	ControlBarSchemeManager *man = TheControlBar ? TheControlBar->getControlBarSchemeManager() : NULL;
	if( man == NULL || TheDisplay == NULL || !man->isCurrentSchemePainted() )
		return;

	ICoord2D offset;
	offset.x = offset.y = 0;
	GameWindow *frame = window ? window->winGetParent() : NULL;
	if( frame )
	{
		ICoord2D now;
		frame->winGetScreenPosition( &now.x, &now.y );
		offset.x = now.x - TheControlBar->getPanelOrigin()->x;
		offset.y = now.y - TheControlBar->getPanelOrigin()->y;
	}
	man->drawPainting( offset, TRUE );
}

void W3DNoDraw( GameWindow *window, WinInstanceData *instData )
{

	// draw the default stuff
//	W3DGameWinDefaultDraw( window, instData );
	
}  // end W3DRightHUDDraw

void drawSkinnyBorder( Int x, Int y, Int width, Int height);
void W3DDrawMapPreview( GameWindow *window, WinInstanceData *instData)
{
	MapMetaData *mmData = (MapMetaData *)window->winGetUserData();
	Int pixelX, pixelY, width, height;
	// if the local player does not have a radar then we can't draw anything
	window->winGetScreenPosition(&pixelX, &pixelY);
	window->winGetSize(&width, &height);
	if(!mmData)
	{
		W3DGameWinDefaultDraw( window, instData );
		drawSkinnyBorder(pixelX - 1, pixelY - 1, width + 2, height + 2);
		return;
	}
	
	
	
		
	//
	// given a upper left corner at pixelX|Y and a width and height to draw into, figure out
	// where we should start and end the image so that the final drawn image has the
	// same ratio as the map and isn't stretched or distorted
	//
	ICoord2D ul, lr;
	findDrawPositions( pixelX, pixelY, width, height, mmData->m_extent, &ul, &lr );

//	Int scaledWidth = lr.x - ul.x;
//	Int scaledHeight = lr.y - ul.y;

	// draw black border areas where we need map
	Color fillColor = GameMakeColor( 0, 0, 0, 255 );
	Color lineColor = GameMakeColor( 50, 50, 50, 255 );
	
	if( mmData->m_extent.width()/width >= mmData->m_extent.height()/height )
	{
		
		// draw horizontal bars at top and bottom
		TheDisplay->drawFillRect( pixelX, pixelY, width, ul.y - pixelY - 1, fillColor );
		TheDisplay->drawFillRect( pixelX, lr.y + 1, width, pixelY + height - lr.y - 1, fillColor);
		TheDisplay->drawLine(pixelX, ul.y, pixelX + width, ul.y, 1, lineColor);
		TheDisplay->drawLine(pixelX, lr.y + 1, pixelX + width, lr.y + 1, 1, lineColor);

	}  // end if
	else
	{

		// draw vertical bars to the left and right
		TheDisplay->drawFillRect( pixelX, pixelY, ul.x - pixelX - 1, height, fillColor );
		TheDisplay->drawFillRect( lr.x + 1, pixelY, width - (lr.x - pixelX) - 1, height, fillColor );
		TheDisplay->drawLine(ul.x, pixelY, ul.x, pixelY + height, 1, lineColor);
		TheDisplay->drawLine(lr.x + 1, pixelY, lr.x + 1, pixelY + height, 1, lineColor);

	}  // end else

	if(!BitTest(window->winGetStatus(), WIN_STATUS_IMAGE) || !window->winGetEnabledImage(0))
		TheDisplay->drawFillRect(ul.x, ul.y, lr.x -ul.x, lr.y-ul.y, lineColor);
	else
		TheDisplay->drawImage(window->winGetEnabledImage(0) , ul.x, ul.y, lr.x, lr.y );

	const Image *image = TheMappedImageCollection->findImageByName("TecBuilding");
	ICoord2DList::iterator it = TheSupplyAndTechImageLocations.m_techPosList.begin();
	if(image)
		while(it != TheSupplyAndTechImageLocations.m_techPosList.end())
		{
			TheDisplay->drawImage(image, pixelX + it->x, pixelY + it->y, pixelX + it->x + SUPPLY_TECH_SIZE, pixelY + it->y +SUPPLY_TECH_SIZE);
			it++;
		}

	image = TheMappedImageCollection->findImageByName("Cash");
	it = TheSupplyAndTechImageLocations.m_supplyPosList.begin();
	if(image)
		while(it != TheSupplyAndTechImageLocations.m_supplyPosList.end())
		{
			TheDisplay->drawImage(image, pixelX + it->x, pixelY + it->y, pixelX + it->x + SUPPLY_TECH_SIZE, pixelY + it->y +SUPPLY_TECH_SIZE);
			it++;
		}
	drawSkinnyBorder(pixelX - 1, pixelY - 1, width + 2, height + 2);
}

void drawSkinnyBorder( Int x, Int y, Int width, Int height)
{

	enum
	{
		BORDER_CORNER_SIZE	= 5,
		BORDER_LINE_SIZE		= 5,
	};
	Int Offset = 2;
	Int OffsetLower = 5;

	// save original x, y
	Int originalX = x;
	Int originalY = y;
	Int maxX = x + width;
	Int maxY = y + height;
	Int x2, y2;			// used for simultaneous drawing of line pairs
	Int size = 5;
	Int halfSize = size / 2;
	const Image *image1, *image2;
	// Draw Horizontal Lines
	// All border pieces are based on a 10 pixel offset from the centerline
	y = originalY - Offset;
	y2 = maxY - OffsetLower;
	x2 = maxX - (OffsetLower + BORDER_LINE_SIZE);
	image1 = TheMappedImageCollection->findImageByName("FrameT");
	image2 = TheMappedImageCollection->findImageByName("FrameB");
	for( x=(originalX + 3); x <= x2; x += BORDER_LINE_SIZE )
	{

		TheDisplay->drawImage( image1,
													 x, y, x + size, y + size );
		TheDisplay->drawImage( image2,
													 x, y2, x + size, y2 + size );

	}

	x2 = maxX - 5;//BORDER_CORNER_SIZE;

	// x == place to draw remainder if any
	if( (x2 - x) >= (BORDER_LINE_SIZE / 2) )
	{
		
		//Blit Half piece
		TheDisplay->drawImage( image1,
													 x, y, x + halfSize, y + size );
		TheDisplay->drawImage( image2,
													 x, y2, x + halfSize, y2 + size );

		x += (BORDER_LINE_SIZE / 2);

	}

	// x2 - x ... must now be less than a half piece
	// check for equals and if not blit an adjusted half piece border pieces have
	// a two pixel repeat so we will blit one pixel over if necessary to line up
	// the art, but we'll cover-up the overlap with the corners
	if( x < x2 )
	{
		x -= ((BORDER_LINE_SIZE / 2) - (((x2 - x) + 1) & ~1));

		//Blit Half piece
		TheDisplay->drawImage(image1,
													 x, y, x + halfSize, y + size );
		TheDisplay->drawImage( image2,
													 x, y2, x + halfSize, y2 + size );

	}

	// Draw Vertical Lines
	// All border pieces are based on a 10 pixel offset from the centerline
	image1 = TheMappedImageCollection->findImageByName("FrameL");
	image2 = TheMappedImageCollection->findImageByName("FrameR");

	x = originalX - Offset;
	x2 = maxX - OffsetLower;
	y2 = maxY - (OffsetLower + BORDER_LINE_SIZE);

	for( y=(originalY + 3); y <= y2; y += BORDER_LINE_SIZE )
	{

		TheDisplay->drawImage( image1,
													 x, y, x + size, y + size );
		TheDisplay->drawImage( image2,
													 x2, y, x2 + size, y + size );

	}

	y2 = maxY - OffsetLower;//BORDER_CORNER_SIZE;

	// y == place to draw remainder if any
	if( (y2 - y) >= (BORDER_LINE_SIZE / 2) )
	{

		//Blit Half piece
		TheDisplay->drawImage( image1,
													 x, y, x + size, y + halfSize );
		TheDisplay->drawImage( image2,
													 x2, y, x2 + size, y + halfSize );

		y += (BORDER_LINE_SIZE / 2);
	}

	// y2 - y ... must now be less than a half piece
	// check for equals and if not blit an adjusted half piece border pieces have
	// a two pixel repeat so we will blit one pixel over if necessary to line up
	// the art, but we'll cover-up the overlap with the corners
	if( y < y2 )
	{
		y -= ((BORDER_LINE_SIZE / 2) - (((y2 - y) + 1) & ~1));

		//Blit Half piece
		TheDisplay->drawImage( image1,
													 x, y, x + size, y + halfSize );
		TheDisplay->drawImage( image2,
													 x2, y, x2 + size, y + halfSize );

	}

	// Draw Corners
	x = originalX - 2;//BORDER_CORNER_SIZE ;
	y = originalY - 2;//BORDER_CORNER_SIZE;
	image1 = TheMappedImageCollection->findImageByName("FrameCornerUL");
	TheDisplay->drawImage( image1,
												 x, y, x + size, y + size );
	x = maxX - 5;//BORDER_CORNER_SIZE;
	y = originalY - 2;//BORDER_CORNER_SIZE;
	image1 = TheMappedImageCollection->findImageByName("FrameCornerUR");
	TheDisplay->drawImage(image1,
												 x, y, x + size, y + size );
	x = originalX - 2;//BORDER_CORNER_SIZE;
	y = maxY - 5;//BORDER_CORNER_SIZE;
	image1 = TheMappedImageCollection->findImageByName("FrameCornerLL");
	TheDisplay->drawImage( image1,
												 x, y, x + size, y + size );
	x = maxX - 5;//BORDER_CORNER_SIZE;
	y = maxY - 5;//BORDER_CORNER_SIZE;
	image1 = TheMappedImageCollection->findImageByName("FrameCornerLR");
	TheDisplay->drawImage(image1,
												 x, y, x + size, y + size );


}


//-------------------------------------------------------------------------------------------------
/** The build card (ControlBarPopupDescription.wnd): the scheme's nine pieces round the window, then
	* what BuildTooltipFigures holds beside the .wnd's texts, laid out as Tools/hud_command_bar.py's
	* TIP_TEXT at 1080: the money icon and price, the clock and build time, right-aligned in x -186..-46 of
	* the header (y 5..27); then the figures, a cell each, icon and value, in x 8..-18, y 31..57. */
//-------------------------------------------------------------------------------------------------
void W3DCommandBarHelpPopupDraw( GameWindow *window, WinInstanceData *instData )
{
	const ControlBarTooltipImages &art = TheControlBar->getControlBarSchemeManager()->getTooltipImages();
	IRegion2D card;
	window->winGetScreenPosition( &card.lo.x, &card.lo.y );
	window->winGetSize( &card.hi.x, &card.hi.y );
	card.hi.x += card.lo.x;
	card.hi.y += card.lo.y;
	// the card is 512 wide at 1080 and the layout fits it (LAYOUT_FIT), so its width is its scale
	const Real k = ( card.hi.x - card.lo.x ) / 512.0f;
	const BuildTooltipFigures &figures = ControlBarBuildTooltipFigures();
	// a card with no figures (a power, a generic window) takes the top row without the figures' well
	const Image *pieces[ ControlBarScheme::FRAME_PIECE_COUNT ];
	for( Int piece = 0; piece < ControlBarScheme::FRAME_PIECE_COUNT; ++piece )
		pieces[ piece ] = art.card[ piece ];
	if( figures.cellCount == 0 && art.head[ 0 ] )
		for( Int piece = ControlBarScheme::FRAME_TL; piece <= ControlBarScheme::FRAME_TR; ++piece )
			pieces[ piece ] = art.head[ piece ];
	ControlBarDrawNinePieceBox( pieces, card, k );

	auto px = [k]( Int at1080 ) { return REAL_TO_INT( at1080 * k ); };
	const Int iconSize = px( 16 );
	const Color white = GameMakeColor( 254, 254, 254, 255 );
	const Color dim = GameMakeColor( 200, 204, 212, 255 );
	const Color shadow = GameMakeColor( 0, 0, 0, 255 );

	static NameKeyType nameID = NAMEKEY( "ControlBarPopupDescription.wnd:StaticTextName" );
	GameFont *font = TheWindowManager->winGetWindowFromId( window, nameID )->winGetFont();
	GameFont *bold = TheFontLibrary->getFont( font->nameString, font->pointSize, TRUE );

	// one string a slot, so each keeps the texture of its text from frame to frame
	enum { SLOT_COST, SLOT_TIME, SLOT_CELL };
	static DisplayString *theStrings[ SLOT_CELL + BuildTooltipFigures::MAX_CELLS ] = { NULL };
	auto text = [&]( Int slot, const UnicodeString &value, GameFont *withFont )
	{
		if( theStrings[ slot ] == NULL )
			theStrings[ slot ] = TheDisplayStringManager->newDisplayString();
		theStrings[ slot ]->setFont( withFont );
		theStrings[ slot ]->setText( value );
		return theStrings[ slot ];
	};
	// an icon (none for NULL) and its text after a gap, centred between top and bottom, the text cut at
	// right: a long armour name stops at its cell's edge
	auto figure = [&]( const Image *icon, DisplayString *value, Int gap, Int x, Int top, Int bottom, Int right, Color color )
	{
		const Int y = ( top + bottom - iconSize ) / 2;
		if( icon )
		{
			TheDisplay->drawImage( icon, x, y, x + iconSize, y + iconSize );
			x += iconSize + gap;
		}
		Int width, height;
		value->getSize( &width, &height );
		IRegion2D clip;
		clip.lo.x = x;
		clip.lo.y = top;
		clip.hi.x = right;
		clip.hi.y = bottom;
		value->setClipRegion( &clip );
		value->draw( x, ( top + bottom - height ) / 2, color, shadow );
	};
	auto widthOf = [&]( const Image *icon, DisplayString *value, Int gap )
	{
		return ( icon ? iconSize + gap : 0 ) + value->getWidth();
	};

	const Int headerTop = card.lo.y + px( 5 ), headerBottom = card.lo.y + px( 27 );
	const Image *money = figures.costIsMoney ? art.icon[ ControlBarTooltipImages::ICON_MONEY ] : NULL;
	const Image *clock = art.icon[ ControlBarTooltipImages::ICON_TIME ];
	DisplayString *cost = figures.cost.isEmpty() ? NULL : text( SLOT_COST, figures.cost, bold );
	DisplayString *time = figures.time.isEmpty() ? NULL : text( SLOT_TIME, figures.time, font );
	Int x = card.hi.x - px( 46 ) - ( cost ? widthOf( money, cost, px( 4 ) ) : 0 ) - ( cost && time ? px( 12 ) : 0 )
					- ( time ? widthOf( clock, time, px( 4 ) ) : 0 );
	if( cost )
	{
		figure( money, cost, px( 4 ), x, headerTop, headerBottom, card.hi.x, white );
		x += widthOf( money, cost, px( 4 ) ) + px( 12 );
	}
	if( time )
		figure( clock, time, px( 4 ), x, headerTop, headerBottom, card.hi.x, dim );

	if( figures.cellCount == 0 )
		return;
	// each cell as wide as its icon and text, the room left over spread as even gaps after them; a row
	// whose cells will not fit at all (a long armour name on a narrow screen) takes even cells, cut
	const Int rowLeft = card.lo.x + px( 10 ), rowRight = card.hi.x - px( 18 );
	const Int rowTop = card.lo.y + px( 31 ), rowBottom = card.lo.y + px( 57 );
	DisplayString *values[ BuildTooltipFigures::MAX_CELLS ];
	Int widths[ BuildTooltipFigures::MAX_CELLS ];
	Int total = 0;
	for( Int cell = 0; cell < figures.cellCount; ++cell )
	{
		values[ cell ] = text( SLOT_CELL + cell, figures.cellValue[ cell ], font );
		widths[ cell ] = widthOf( art.icon[ figures.cellIcon[ cell ] ], values[ cell ], px( 5 ) );
		total += widths[ cell ];
	}
	const Int room = rowRight - rowLeft;
	const Bool fits = total <= room;
	const Int gap = fits ? ( room - total ) / figures.cellCount : 0;
	x = rowLeft;
	for( Int cell = 0; cell < figures.cellCount; ++cell )
	{
		const Int span = fits ? widths[ cell ] + gap : room / figures.cellCount;
		figure( art.icon[ figures.cellIcon[ cell ] ], values[ cell ], px( 5 ), x, rowTop, rowBottom, x + span - ( fits ? 0 : px( 2 ) ), white );
		x += span;
	}
}

