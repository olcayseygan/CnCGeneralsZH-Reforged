// Drive minimap gestures, and the world's armed-key gestures, through the actual window and mouse
// message translators.
#include "GameClient/MetaEvent.h"
#include "GameClient/CommandXlat.h"
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
	// the centre of window pixel (90, 90) in the picture the radar draws, a pixel inside the 128
	// pixel window each side, over the 1280 map
	ICoord2D ul, lr;
	f.radar.findDrawPositions(1, 1, 126, 126, &ul, &lr);
	CHECK_NEAR(f.engine.view.getPosition()->x, (90.5f - ul.x) * 1280.0f / (lr.x - ul.x), 0.01f);
	CHECK_NEAR(f.engine.view.getPosition()->y, (lr.y - 90.5f) * 1280.0f / (lr.y - ul.y), 0.01f);
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

/* Classic's radar in both of 1.04's mouse setups: the look button moves the camera on the press, the
   camera follows the cursor while that button is held, and stops following on the release. */
TEST(minimap_classic_look_follows_the_held_button_in_both_mouse_setups)
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

		// the cursor is over the radar before it presses there, as it always is in the game
		f.event(messages, GameMessage::MSG_RAW_MOUSE_POSITION, 120, 220);
		f.event(messages, lookDown, 120, 220);
		CHECK(f.engine.view.lookAtCalls == 1);
		const Coord3D pressed = *f.engine.view.getPosition();
		f.event(messages, lookDrag, 150, 250);
		CHECK(f.engine.view.lookAtCalls == 2);
		CHECK(f.engine.view.getPosition()->x != pressed.x || f.engine.view.getPosition()->y != pressed.y);
		f.event(messages, lookUp, 150, 250);
		CHECK(messages.commands.getFirstMessage() == NULL);
		f.event(messages, lookDrag, 180, 280);
		CHECK(f.engine.view.lookAtCalls == 2);
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

namespace
{
class OrderButtonUI : public MinimapInputUI
{
public:
	void select(Int count) { m_selectCount = count; }
	void reset() { clearAttackMoveToMode(); setForceAttackMode(FALSE); clearFormationDrag(); cancelAttackCircle(); m_selectCount = 0; }
	void placeBuildAvailable(const ThingTemplate*, Drawable*) {}
	void deselectAllDrawables() {}
};

OrderButtonUI *orderButtonUI()
{
	// kept until process exit, as the other input tests keep theirs
	static OrderButtonUI *ui = NEW OrderButtonUI;
	return ui;
}

// the three mice: Classic, Classic under EA's Alternate Mouse Setup, Reforged
const Int ORDER_BUTTON_STYLES[3] = { MOUSE_STYLE_CLASSIC, MOUSE_STYLE_CLASSIC, MOUSE_STYLE_REFORGED };
const Bool ORDER_BUTTON_ALTERNATE[3] = { FALSE, TRUE, FALSE };

GameMessageDisposition sendRawMouse(GameMessageTranslator &translator, GameMessage::Type type, Int x, Int y, Int time)
{
	GameMessage *message = newInstance(GameMessage)(type);
	const ICoord2D pixel = {x, y};
	message->appendPixelArgument(pixel);
	message->appendIntegerArgument(0);
	message->appendIntegerArgument(time);
	const GameMessageDisposition disp = translator.translateGameMessage(message);
	message->deleteInstance();
	return disp;
}

GameMessageDisposition sendClick(GameMessageTranslator &translator, GameMessage::Type type, Int x, Int y)
{
	GameMessage *message = newInstance(GameMessage)(type);
	IRegion2D region;
	region.lo.x = region.hi.x = x;
	region.lo.y = region.hi.y = y;
	message->appendPixelRegionArgument(region);
	message->appendIntegerArgument(0);
	const GameMessageDisposition disp = translator.translateGameMessage(message);
	message->deleteInstance();
	return disp;
}
}

/* Attack move, guard and the Ctrl hold are aimed with the left button in every mouse, the drag as well
   as the click.  Their lines and circle used to follow the button that orders, so under the Alternate
   Mouse Setup an attack move line or a Ctrl circle took the right button and the left one drew a box.
   Shift's plain move line still follows the button that orders. */
TEST(armed_keys_and_ctrl_drag_with_the_left_button_in_every_mouse_setup)
{
	CHECK(bootOnce());
	MinimapEngineFixture f;
	OrderButtonUI *ui = orderButtonUI();
	TheInGameUI = ui;
	SelectionTranslator *savedSelection = TheSelectionTranslator;
	f.mouse.m_dragTolerance = 25;
	f.mouse.m_dragTolerance3D = 25;
	f.mouse.m_dragToleranceMS = 250;
	{
		SelectionTranslator selection;
		for (Int i = 0; i < 3; ++i)
		{
			f.global->m_mouseStyle = ORDER_BUTTON_STYLES[i];
			f.global->m_useAlternateMouse = ORDER_BUTTON_ALTERNATE[i];
			f.keyboard.modifiers(0);

			// nothing armed: a line, Shift's move line, belongs to the button that orders
			ui->reset();
			ui->select(1);
			CHECK(ui->isOrderDragButton(FALSE) == f.global->leftButtonOrders());
			CHECK(ui->isOrderDragButton(TRUE) == f.global->rightButtonOrders());

			// attack move, then guard: the left press latches the line, so the click behind the release
			// that ends the drag is eaten instead of selecting
			for (Int key = 0; key < 2; ++key)
			{
				ui->reset();
				ui->select(1);
				if (key == 0)
					ui->toggleAttackMoveToMode();
				else
					ui->toggleGuardArmed();
				CHECK(ui->isOrderDragButton(FALSE));
				CHECK(!ui->isOrderDragButton(TRUE));

				sendRawMouse(selection, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 300, 300, 1000);
				const ICoord2D from = {300, 300}, to = {420, 300};
				ui->addFormationDragPoint(from);
				ui->addFormationDragPoint(to);
				sendRawMouse(selection, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 420, 300, 1200);
				CHECK(sendClick(selection, GameMessage::MSG_MOUSE_LEFT_CLICK, 420, 300) == DESTROY_MESSAGE);
			}

			// the Ctrl hold: the right press never starts the attack circle, the left one does
			ui->reset();
			ui->select(1);
			ui->setForceAttackMode(TRUE);
			f.keyboard.modifiers(KEY_STATE_LCONTROL);
			sendRawMouse(selection, GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300, 1000);
			CHECK(!ui->isAttackCircling());
			sendRawMouse(selection, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 300, 300, 1000);
			CHECK(ui->isAttackCircling());
			ui->cancelAttackCircle();

			// Ctrl+Shift draws the attack line, on the left as well
			f.keyboard.modifiers(KEY_STATE_LCONTROL | KEY_STATE_LSHIFT);
			CHECK(ui->isForceLineHeld());
			CHECK(ui->isOrderDragButton(FALSE));
			CHECK(!ui->isOrderDragButton(TRUE));
		}
	}
	f.keyboard.modifiers(0);
	ui->reset();
	TheSelectionTranslator = savedSelection;
}

/* Where the right button gives the orders, a right click with attack move, guard or Ctrl puts the key
   down and orders nothing, the way a right click takes back any command waiting for its target.  A
   right drag pans and leaves the key armed. */
TEST(right_click_puts_an_armed_key_down_without_an_order_where_the_right_button_orders)
{
	CHECK(bootOnce());
	MinimapEngineFixture f;
	MinimapMessages messages;
	OrderButtonUI *ui = orderButtonUI();
	TheInGameUI = ui;
	f.mouse.m_dragTolerance = 25;
	f.mouse.m_dragTolerance3D = 25;
	f.mouse.m_dragToleranceMS = 250;
	{
		CommandTranslator command;
		const auto rightClick = [&command](Int dx)
		{
			sendRawMouse(command, GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300, 1000);
			sendRawMouse(command, GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 300 + dx, 300, 1100);
			return sendClick(command, GameMessage::MSG_MOUSE_RIGHT_CLICK, 300 + dx, 300);
		};
		for (Int i = 1; i < 3; ++i)
		{
			f.global->m_mouseStyle = ORDER_BUTTON_STYLES[i];
			f.global->m_useAlternateMouse = ORDER_BUTTON_ALTERNATE[i];
			for (Int key = 0; key < 2; ++key)
			{
				ui->reset();
				ui->select(1);
				if (key == 0)
					ui->toggleAttackMoveToMode();
				else
					ui->toggleGuardArmed();
				CHECK(rightClick(60) == DESTROY_MESSAGE);
				CHECK(ui->isOrderKeyArmed());
				CHECK(rightClick(0) == DESTROY_MESSAGE);
				CHECK(!ui->isOrderKeyArmed());
			}

			ui->reset();
			ui->select(1);
			ui->setForceAttackMode(TRUE);
			CHECK(rightClick(0) == DESTROY_MESSAGE);
			CHECK(messages.stream.getFirstMessage() == NULL);
		}
	}
	ui->reset();
}

/* The radar follows the world: with a key armed the left press is the order and the right one looks,
   in the mice where the right button orders as well as in Classic's default. */
TEST(minimap_armed_key_orders_on_the_left_and_looks_on_the_right_in_every_mouse_setup)
{
	CHECK(bootOnce());
	GameClient *savedClient = TheGameClient;
	TheGameClient = getTestGameClient();
	for (Int i = 0; i < 3; ++i)
	{
		MinimapInputFixture f;
		MinimapMessages messages;
		messages.stream.attachTranslator(NEW WindowTranslator, 10);
		TheWritableGlobalData->m_mouseStyle = ORDER_BUTTON_STYLES[i];
		TheWritableGlobalData->m_useAlternateMouse = ORDER_BUTTON_ALTERNATE[i];
		MinimapTestUI *ui = (MinimapTestUI*)TheInGameUI;
		ui->selected.push_back(NULL);
		ui->clearAttackMoveToMode();
		ui->toggleAttackMoveToMode();

		f.event(messages, GameMessage::MSG_RAW_MOUSE_POSITION, 120, 220);
		f.event(messages, GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 120, 220);
		f.event(messages, GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 120, 220);
		CHECK(f.engine.view.lookAtCalls == 1);
		CHECK(ui->isOrderKeyArmed());

		f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 120, 220);
		f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 120, 220);
		CHECK(f.engine.view.lookAtCalls == 1);
		CHECK(!ui->isOrderKeyArmed());
		ui->clearAttackMoveToMode();
	}
	TheGameClient = savedClient;
}

// The wheel over the radar zooms the minimap about the cursor and stops there, and a click after it
// goes through the same zoom: the pixel the wheel turned over still looks at the spot it did, and a
// pixel beside it now reaches half as far.
TEST(minimap_wheel_zooms_about_the_cursor_and_clicks_follow_the_zoom)
{
	CHECK(bootOnce());
	MinimapInputFixture f;
	MinimapMessages messages;
	messages.stream.attachTranslator(NEW WindowTranslator, 10);

	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 140, 250);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 140, 250);
	const Coord3D before = *f.engine.view.getPosition();
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 150, 250);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 150, 250);
	const Real stepBefore = f.engine.view.getPosition()->x - before.x;

	for (Int notch = 0; notch < 2; ++notch)
	{
		GameMessage *wheel = messages.stream.appendMessage(GameMessage::MSG_RAW_MOUSE_WHEEL);
		ICoord2D at = {140, 250};
		wheel->appendPixelArgument(at);
		wheel->appendRealArgument(1.0f);
		wheel->appendIntegerArgument(0);
		messages.stream.propagateMessages();
	}
	CHECK_NEAR(f.radar.getZoom(), 2.0f, 0.001f);
	// eaten by the radar, so the camera's own wheel zoom never saw it
	CHECK(messages.stream.getFirstMessage() == NULL);

	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 140, 250);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 140, 250);
	CHECK_NEAR(f.engine.view.getPosition()->x, before.x, 0.01f);
	CHECK_NEAR(f.engine.view.getPosition()->y, before.y, 0.01f);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 150, 250);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 150, 250);
	CHECK_NEAR(f.engine.view.getPosition()->x - before.x, stepBefore * 0.5f, 0.01f);
}
