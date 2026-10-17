// Drive minimap gestures through the actual window and mouse message translators.
#include "GameClient/MetaEvent.h"
#include "GameClient/Keyboard.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/WindowXlat.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/ControlBar.h"
#include "GameClient/SelectionXlat.h"
#include "Common/Radar.h"
#include "Common/StatsCollector.h"
#include "Common/PlayerList.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/GameLogic.h"

namespace
{
class MinimapInputKeyboard : public Keyboard
{
public:
	Bool getCapsState() { return FALSE; }
	void getKey(KeyboardIO *key) { key->key = KEY_NONE; }
	void modifiers(Int flags) { m_modifiers = flags; }
};

class MinimapInputMouse : public Mouse
{
public:
	Int cursorChanges = 0, captures = 0, releases = 0;
	void initCursorResources() {}
	void setCursor(MouseCursor cursor) { m_currentCursor = cursor; ++cursorChanges; }
	void capture() { ++captures; }
	void releaseCapture() { ++releases; }
	UnsignedByte getMouseEvent(MouseIO*, Bool) { return 0; }
};

class MinimapInputUI : public InGameUI
{
public:
	void draw() {}
	View *createView() { return NULL; }
	Bool scrolling = FALSE, selecting = FALSE, enabled = TRUE;
	void setScrolling(Bool value, Bool moveCursor = TRUE) { InGameUI::setScrolling(value, moveCursor); scrolling = value; }
	Bool isScrolling() { return scrolling; }
	Bool isSelecting() { return selecting; }
	Bool getInputEnabled() { return enabled; }
};

class MinimapInputView : public View
{
public:
	using View::getPosition;
	using View::setPosition;
	Coord2D last = {0, 0};
	Int samples = 0;
	Int constraintRecalcs = 0;
	Bool terrainHit = FALSE;
	Coord3D terrainPoint = {0, 0, 0};
	Int terrainPicks = 0;
	Int lookAtCalls = 0;
	void lookAt(const Coord3D *pos) { ++lookAtCalls; setPosition(pos); }
	void forceCameraConstraintRecalc() { ++constraintRecalcs; }
	void scrollBy(Coord2D *delta) { last = *delta; ++samples; }
	Drawable *pickDrawable(const ICoord2D*, Bool, PickType) { return NULL; }
	Int iterateDrawablesInRegion(IRegion2D*, Bool (*)(Drawable*, void*), void*) { return 0; }
	void forceRedraw() {}
	const Coord3D& get3DCameraPosition() const { return m_pos; }
	WorldToScreenReturn worldToScreenTriReturn(const Coord3D*, ICoord2D*) { return WTS_INSIDE_FRUSTUM; }
	void screenToWorld(const ICoord2D*, Coord3D*) {}
	Bool screenToTerrain(const ICoord2D*, Coord3D *world)
	{ ++terrainPicks; *world = terrainPoint; return terrainHit; }
	void screenToWorldAtZ(const ICoord2D*, Coord3D *world, Real z)
	{ *world = terrainPoint; world->z = z; }
	void drawView() {}
	void updateView() {}
	void setGuardBandBias(const Coord2D*) {}
};

struct MinimapEngineFixture
{
	GlobalData *savedGlobal = TheWritableGlobalData;
	InGameUI *savedUI = TheInGameUI;
	View *savedView = TheTacticalView;
	Keyboard *savedKeyboard = TheKeyboard;
	Mouse *savedMouse = TheMouse;
	Shell *savedShell = TheShell;
	GameWindowManager *savedWindows = TheWindowManager;
	StatsCollector *savedStats = TheStatsCollector;
	PlayerList *savedPlayers = ThePlayerList;
	RankInfoStore *savedRanks = TheRankInfoStore;
	GameLogic *savedLogic = TheGameLogic;
	GlobalData *global;
	MinimapInputUI *ui;
	MinimapInputView view;
	MinimapInputKeyboard keyboard;
	MinimapInputMouse mouse;

	MinimapEngineFixture()
	{
		global = NEW GlobalData;
		TheWritableGlobalData = global;
		// Like bootOnce's subsystems, keep one UI until process exit. Its production destructor
		// requires a fully built control bar and a device, which this input test does not create.
		static MinimapInputUI *testUI = NEW MinimapInputUI;
		ui = testUI;
		ui->scrolling = ui->selecting = FALSE;
		ui->enabled = TRUE;
		TheInGameUI = ui;
		TheTacticalView = &view;
		TheKeyboard = &keyboard;
		TheMouse = &mouse;
		TheShell = NULL;
		TheWindowManager = NULL;
		TheStatsCollector = NULL;
		static RankInfoStore *ranks = NEW RankInfoStore;
		TheRankInfoStore = ranks;
		static PlayerList *players = NEW PlayerList;
		ThePlayerList = players;
		static GameLogic *logic = NEW GameLogic;
		TheGameLogic = logic;
		global->m_saveCameraInReplay = FALSE;
	}
	~MinimapEngineFixture()
	{
		TheInGameUI = savedUI;
		TheTacticalView = savedView;
		TheKeyboard = savedKeyboard;
		TheMouse = savedMouse;
		TheShell = savedShell;
		TheWindowManager = savedWindows;
		TheStatsCollector = savedStats;
		ThePlayerList = savedPlayers;
		TheRankInfoStore = savedRanks;
		TheGameLogic = savedLogic;
		delete global;					// while it is the current one: a Debug build's ~GlobalData reads TheWritableGlobalData
		TheWritableGlobalData = savedGlobal;
	}
};

class MinimapWindow : public GameWindow
{
	MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE(MinimapWindow, "MinimapWindow", 16, 16)
public:
	void winDrawBorder() {}
	void winSetFont(GameFont*) {}
};
MinimapWindow::~MinimapWindow() {}

class MinimapWindows : public GameWindowManager
{
public:
	GameWindow *allocateNewWindow() { return newInstance(MinimapWindow)(); }
	GameFont *winFindFont(AsciiString, Int, Bool) { return NULL; }
	GameWinDrawFunc getPushButtonImageDrawFunc() { return NULL; }
	GameWinDrawFunc getPushButtonDrawFunc() { return NULL; }
	GameWinDrawFunc getCheckBoxImageDrawFunc() { return NULL; }
	GameWinDrawFunc getCheckBoxDrawFunc() { return NULL; }
	GameWinDrawFunc getRadioButtonImageDrawFunc() { return NULL; }
	GameWinDrawFunc getRadioButtonDrawFunc() { return NULL; }
	GameWinDrawFunc getTabControlImageDrawFunc() { return NULL; }
	GameWinDrawFunc getTabControlDrawFunc() { return NULL; }
	GameWinDrawFunc getListBoxImageDrawFunc() { return NULL; }
	GameWinDrawFunc getListBoxDrawFunc() { return NULL; }
	GameWinDrawFunc getComboBoxImageDrawFunc() { return NULL; }
	GameWinDrawFunc getComboBoxDrawFunc() { return NULL; }
	GameWinDrawFunc getHorizontalSliderImageDrawFunc() { return NULL; }
	GameWinDrawFunc getHorizontalSliderDrawFunc() { return NULL; }
	GameWinDrawFunc getVerticalSliderImageDrawFunc() { return NULL; }
	GameWinDrawFunc getVerticalSliderDrawFunc() { return NULL; }
	GameWinDrawFunc getProgressBarImageDrawFunc() { return NULL; }
	GameWinDrawFunc getProgressBarDrawFunc() { return NULL; }
	GameWinDrawFunc getStaticTextImageDrawFunc() { return NULL; }
	GameWinDrawFunc getStaticTextDrawFunc() { return NULL; }
	GameWinDrawFunc getTextEntryImageDrawFunc() { return NULL; }
	GameWinDrawFunc getTextEntryDrawFunc() { return NULL; }
};

struct MinimapMessages
{
	MessageStream stream;
	CommandList commands;
	MessageStream *savedStream = TheMessageStream;
	CommandList *savedCommands = TheCommandList;
	MinimapMessages()
	{
		TheMessageStream = &stream;
		TheCommandList = &commands;
		stream.attachTranslator(NEW MetaEventTranslator, 20);
	}
	~MinimapMessages()
	{
		TheMessageStream = savedStream;
		TheCommandList = savedCommands;
	}
};

class MinimapTestRadar : public Radar
{
public:
	void configure(GameWindow *window, Real width = 1280, Real height = 1280)
	{
		m_radarWindow = window;
		m_mapExtent.lo.zero();
		m_mapExtent.hi.x = width;
		m_mapExtent.hi.y = height;
		m_xSample = width / RADAR_CELL_WIDTH;
		m_ySample = height / RADAR_CELL_HEIGHT;
		forceOn(TRUE);
	}
	void draw(Int, Int, Int, Int) {}
	void clearShroud() {}
	void setShroudLevel(Int, Int, CellShroudStatus) {}
};

class MinimapTestTerrain : public TerrainLogic
{
public:
	Real getGroundHeight(Real, Real, Coord3D* = NULL) const { return 25; }
};

class MinimapTestUI : public MinimapInputUI
{
public:
	DrawableList selected;
	const CommandButton *command = NULL;
	const DrawableList *getAllSelectedLocalDrawables() { return &selected; }
	const CommandButton *getGUICommand() const { return command; }
};

struct MinimapInputFixture
{
	MinimapEngineFixture engine;
	MinimapWindows windows;
	MinimapTestRadar radar;
	MinimapTestTerrain terrain;
	Radar *savedRadar = TheRadar;
	TerrainLogic *savedTerrain = TheTerrainLogic;
	GameWindow *window;
	MinimapInputFixture()
	{
		TheWindowManager = &windows;
		TheRadar = &radar;
		TheTerrainLogic = &terrain;
		window = windows.winCreate(NULL, WIN_STATUS_ENABLED, 100, 200, 128, 128, NULL);
		window->winSetInputFunc(LeftHUDInput);
		radar.configure(window);
		static MinimapTestUI *ui = NEW MinimapTestUI;
		ui->selected.clear();
		ui->command = NULL;
		ui->scrolling = ui->selecting = FALSE;
		ui->enabled = TRUE;
		TheInGameUI = ui;
		// Reforged's radar: left looks, right orders.  Classic swaps them, and says so in its own test
		savedStyle = TheGlobalData->m_mouseStyle;
		savedAlternateMouse = TheGlobalData->m_useAlternateMouse;
		TheWritableGlobalData->m_mouseStyle = MOUSE_STYLE_REFORGED;
		TheWritableGlobalData->m_useAlternateMouse = FALSE;
	}
	Int savedStyle;
	Bool savedAlternateMouse;
	~MinimapInputFixture()
	{
		TheWritableGlobalData->m_mouseStyle = savedStyle;
		TheWritableGlobalData->m_useAlternateMouse = savedAlternateMouse;
		windows.winDestroyAll();
		TheRadar = savedRadar;
		TheTerrainLogic = savedTerrain;
	}
	void event(MinimapMessages &messages, GameMessage::Type type, Int x, Int y)
	{
		GameMessage *message = messages.stream.appendMessage(type);
		ICoord2D pixel = {x, y};
		message->appendPixelArgument(pixel);
		if(type == GameMessage::MSG_RAW_MOUSE_LEFT_DRAG || type == GameMessage::MSG_RAW_MOUSE_RIGHT_DRAG)
		{
			ICoord2D delta = {1, 1};
			message->appendPixelArgument(delta);
		}
		else
			message->appendIntegerArgument(0);
		message->appendIntegerArgument(100);
		messages.stream.propagateMessages();
	}
};
}

TEST(minimap_drag_tracks_held_left_click_and_stops_on_release_with_units_selected)
{
	CHECK(bootOnce());
	MinimapInputFixture f;
	MinimapMessages messages;
	messages.stream.attachTranslator(NEW WindowTranslator, 10);
	// A selected unit must not turn left dragging into move orders.
	((MinimapTestUI*)TheInGameUI)->selected.push_back(NULL);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 120, 220);
	CHECK(f.engine.view.lookAtCalls == 1);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 150, 250);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 190, 290);
	CHECK(f.engine.view.lookAtCalls == 3);
	CHECK(f.engine.view.getPosition()->x == 900);
	CHECK(f.engine.view.getPosition()->y == 380);
	CHECK(f.engine.view.getPosition()->z == 25);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 190, 290);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_POSITION, 130, 230);
	CHECK(f.engine.view.lookAtCalls == 3);
	CHECK(f.windows.winGetGrabWindow() == NULL);
	CHECK(messages.commands.getFirstMessage() == NULL);
}

/* Classic's radar is the game as shipped: with units selected the right button looks and drags. */
TEST(minimap_classic_right_button_looks_with_units_selected)
{
	CHECK(bootOnce());
	MinimapInputFixture f;
	MinimapMessages messages;
	messages.stream.attachTranslator(NEW WindowTranslator, 10);
	TheWritableGlobalData->m_mouseStyle = MOUSE_STYLE_CLASSIC;
	((MinimapTestUI*)TheInGameUI)->selected.push_back(NULL);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 120, 220);
	CHECK(f.engine.view.lookAtCalls == 1);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 120, 220);
	CHECK(messages.commands.getFirstMessage() == NULL);
}

/* EA's Alternate Mouse Setup put the radar's look back on the left button, the world's select button.
   Reforged's radar looks on the left whether the setting is on or not. */
TEST(minimap_alternate_mouse_looks_on_the_left_in_classic_and_reforged_ignores_it)
{
	CHECK(bootOnce());
	const Int styles[2] = { MOUSE_STYLE_CLASSIC, MOUSE_STYLE_REFORGED };
	for (Int i = 0; i < 2; ++i)
	{
		MinimapInputFixture f;
		MinimapMessages messages;
		messages.stream.attachTranslator(NEW WindowTranslator, 10);
		TheWritableGlobalData->m_mouseStyle = styles[i];
		TheWritableGlobalData->m_useAlternateMouse = TRUE;
		((MinimapTestUI*)TheInGameUI)->selected.push_back(NULL);
		f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 120, 220);
		CHECK(f.engine.view.lookAtCalls == 1);
		f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 120, 220);
		CHECK(messages.commands.getFirstMessage() == NULL);
	}
}

/* Off the radar the drag pins to the picture's edge, so the camera waits at the map's edge instead of
   wherever the last move inside left it. A release outside never reaches the radar, and the next drag
   that starts on the battlefield and crosses the radar must still leave the camera alone. */
TEST(minimap_drag_pins_to_the_edge_off_the_radar_and_a_world_drag_after_it_leaves_the_camera)
{
	CHECK(bootOnce());
	MinimapInputFixture f;
	MinimapMessages messages;
	messages.stream.attachTranslator(NEW WindowTranslator, 10);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 120, 220);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 300, 400);
	CHECK(f.engine.view.lookAtCalls == 2);
	// the radar's bottom right is the map's high x and low y: the picture is drawn with y upside down
	CHECK(f.engine.view.getPosition()->x > 1200);
	CHECK(f.engine.view.getPosition()->y < 20);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 170, 270);
	CHECK(f.engine.view.lookAtCalls == 3);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 300, 400);
	CHECK(f.windows.winGetGrabWindow() == NULL);
	CHECK(messages.commands.getFirstMessage() == NULL);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 300, 400);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 150, 250);
	CHECK(f.engine.view.lookAtCalls == 3);
}

/* Arrow-key scrolling locks the tactical view's mouse input; a press that the radar already holds keeps
   its drag anyway. Above a letterboxed map the drag pins to the top of the picture. */
TEST(minimap_drag_works_during_keyboard_scroll_and_pins_to_a_letterboxed_picture)
{
	CHECK(bootOnce());
	MinimapInputFixture f;
	MinimapMessages messages;
	messages.stream.attachTranslator(NEW WindowTranslator, 10);
	f.radar.configure(f.window, 1280, 640);
	f.engine.view.setMouseLock(TRUE);
	TheInGameUI->setScrolling(TRUE);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 120, 240);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 170, 260);
	CHECK(f.engine.view.lookAtCalls == 2);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 170, 210);
	CHECK(f.engine.view.lookAtCalls == 3);
	// the top of the picture is the map's far y edge, 640 on this map
	CHECK(f.engine.view.getPosition()->y > 600);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 300, 400);
	CHECK(f.windows.winGetGrabWindow() == NULL);
}

/* Classic's radar in both of its mouse setups: the look button moves the camera on the press, and held
   it keeps the camera under the cursor (ControlBarCallback.cpp, LeftHUDInput). */
TEST(minimap_classic_looks_on_the_press_and_follows_the_drag_in_both_mouse_setups)
{
	CHECK(bootOnce());
	for (Int alternate = 0; alternate < 2; ++alternate)
	{
		MinimapInputFixture f;
		MinimapMessages messages;
		messages.stream.attachTranslator(NEW WindowTranslator, 10);
		TheWritableGlobalData->m_mouseStyle = MOUSE_STYLE_CLASSIC;
		TheWritableGlobalData->m_useAlternateMouse = alternate ? TRUE : FALSE;
		((MinimapTestUI*)TheInGameUI)->selected.push_back(NULL);

		const GameMessage::Type lookDown = alternate ? GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN : GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN;
		const GameMessage::Type lookDrag = alternate ? GameMessage::MSG_RAW_MOUSE_LEFT_DRAG : GameMessage::MSG_RAW_MOUSE_RIGHT_DRAG;
		const GameMessage::Type lookUp = alternate ? GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP : GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP;

		// the cursor is over the radar before the press, as a player's is; a right press does not
		// hold the radar, so entering it after the press would end the look
		f.event(messages, GameMessage::MSG_RAW_MOUSE_POSITION, 120, 220);
		f.event(messages, lookDown, 120, 220);
		CHECK(f.engine.view.lookAtCalls == 1);
		f.event(messages, lookDrag, 150, 250);
		CHECK(f.engine.view.lookAtCalls == 2);
		f.event(messages, lookUp, 150, 250);
		CHECK(messages.commands.getFirstMessage() == NULL);
	}
}

namespace
{
class ClassicRightClickUI : public MinimapInputUI
{
public:
	const CommandButton *command = NULL;
	ObjectID placeSource = INVALID_ID;
	Int deselects = 0;
	const CommandButton *getGUICommand() const { return command; }
	void setGUICommand(const CommandButton *c) { command = c; }
	const ObjectID getPendingPlaceSourceObjectID() { return placeSource; }
	void deselectAllDrawables() { ++deselects; }
};
}

/* 1.04's right release in Classic: only a click counts, measured on screen, in time and by how far the
   camera rode, since the press scrolls as well.  A click takes back a waiting command and is eaten, so
   the alternate mouse's order does not follow it; otherwise it deselects, which under the alternate
   mouse it does only with a dozer's structure on the cursor. */
TEST(classic_right_click_deselects_or_takes_back_a_command_as_1_04_did)
{
	CHECK(bootOnce());
	MinimapEngineFixture f;
	static ClassicRightClickUI *ui = NEW ClassicRightClickUI;
	TheInGameUI = ui;
	SelectionTranslator *savedSelection = TheSelectionTranslator;
	f.mouse.m_dragTolerance = 25;
	f.mouse.m_dragTolerance3D = 25;
	f.mouse.m_dragToleranceMS = 250;
	f.global->m_mouseStyle = MOUSE_STYLE_CLASSIC;
	CommandButton *guard = newInstance(CommandButton);
	guard->friend_setCommandType(GUI_COMMAND_GUARD);
	{
		SelectionTranslator selection;
		const auto mouse = [&selection](GameMessage::Type type, Int x, Int y, Int time)
		{
			GameMessage *message = newInstance(GameMessage)(type);
			const ICoord2D pixel = {x, y};
			message->appendPixelArgument(pixel);
			message->appendIntegerArgument(0);
			message->appendIntegerArgument(time);
			const GameMessageDisposition disp = selection.translateGameMessage(message);
			message->deleteInstance();
			return disp;
		};
		const auto click = [&mouse](Int dx, Int heldMs)
		{
			mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300, 1000);
			return mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 300 + dx, 300, 1000 + heldMs);
		};

		for (Int alternate = 0; alternate < 2; ++alternate)
		{
			f.global->m_useAlternateMouse = alternate ? TRUE : FALSE;
			ui->deselects = 0;
			ui->command = NULL;
			ui->placeSource = INVALID_ID;

			// a click with nothing waiting deselects in the default mouse and not in the alternate one
			CHECK(click(10, 100) == KEEP_MESSAGE);
			CHECK_EQ(ui->deselects, alternate ? 0 : 1);

			// a drag or a held press is no click at all
			click(60, 100);
			click(0, 400);
			CHECK_EQ(ui->deselects, alternate ? 0 : 1);

			// nor is a press the camera rode away from
			mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300, 1000);
			Coord3D moved = *f.view.getPosition();
			moved.x += 100.0f;
			f.view.setPosition(&moved);
			mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 300, 300, 1100);
			CHECK_EQ(ui->deselects, alternate ? 0 : 1);

			// a waiting command is taken back, the units stay, and the release is eaten
			ui->command = guard;
			CHECK(click(0, 100) == DESTROY_MESSAGE);
			CHECK(ui->command == NULL);
			CHECK_EQ(ui->deselects, alternate ? 0 : 1);

			// a dozer's structure on the cursor deselects the dozer in both
			ui->placeSource = (ObjectID)7;
			click(0, 100);
			CHECK_EQ(ui->deselects, alternate ? 1 : 2);
		}
	}
	guard->deleteInstance();
	TheSelectionTranslator = savedSelection;
}
