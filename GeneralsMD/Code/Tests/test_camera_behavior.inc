// Exercise upstream camera input and GUI routing with lightweight subsystem fixtures.
#include "GameClient/LookAtXlat.h"
#include "GameClient/CameraBoundary.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/Keyboard.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "Common/StatsCollector.h"
#include "Common/PlayerList.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/GameLogic.h"

namespace
{
class CameraTestKeyboard : public Keyboard
{
public:
	Bool getCapsState() { return FALSE; }
	void getKey(KeyboardIO *key) { key->key = KEY_NONE; }
	void modifiers(Int flags) { m_modifiers = flags; }
};

class CameraTestMouse : public Mouse
{
public:
	Int cursorChanges = 0, captures = 0, releases = 0;
	void initCursorResources() {}
	void setCursor(MouseCursor cursor) { m_currentCursor = cursor; ++cursorChanges; }
	void capture() { ++captures; }
	void releaseCapture() { ++releases; }
	UnsignedByte getMouseEvent(MouseIO*, Bool) { return 0; }
};

class CameraTestUI : public InGameUI
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

class CameraTestView : public View
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
	Int zoomAnchors = 0;
	ICoord2D zoomAnchor = {0, 0};
	void lookAt(const Coord3D *pos) { ++lookAtCalls; setPosition(pos); }
	void anchorZoomAt(const ICoord2D *pixel) { zoomAnchor = *pixel; ++zoomAnchors; }
	void forceCameraConstraintRecalc() { ++constraintRecalcs; }
	// what the opening zoom asked for: -1 the map's default, -2 as far out as the wheel goes
	void setZoomToHeight(Real height) { setHeightAboveGround(height); }
	void setZoomToDefault() { setHeightAboveGround(-1.0f); }
	void setZoomToMax() { setHeightAboveGround(-2.0f); }
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

struct CameraInputFixture
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
	CameraTestUI *ui;
	CameraTestView view;
	CameraTestKeyboard keyboard;
	CameraTestMouse mouse;
	LookAtTranslator camera;

	CameraInputFixture()
	{
		global = NEW GlobalData;
		TheWritableGlobalData = global;
		// Like bootOnce's subsystems, keep one UI until process exit. Its production destructor
		// requires a fully built control bar and a device, which this input test does not create.
		static CameraTestUI *testUI = NEW CameraTestUI;
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
		// the tests below are Reforged's mouse unless they switch to Classic themselves
		global->m_mouseStyle = MOUSE_STYLE_REFORGED;
	}
	~CameraInputFixture()
	{
		camera.resetModes();
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
}

TEST(camera_margin_allows_map_corners_but_bounds_large_pans)
{
	const Region3D maps[] = {
		{{0, 0, 0}, {3000, 2000, 100}},
		{{-500, 700, -20}, {-450, 730, 80}} // small map with a nonzero origin
	};
	for (Int m = 0; m < 2; ++m)
	{
		const Region3D& map = maps[m];
		const Region2D boundary = cameraBoundaryFromMap(map, 200.0f);
		for (Int x = 0; x < 2; ++x)
			for (Int y = 0; y < 2; ++y)
			{
				Coord3D corner = {x ? map.hi.x : map.lo.x, y ? map.hi.y : map.lo.y, 42.0f};
				Coord3D allowed = constrainCameraPosition(corner, boundary);
				CHECK(allowed == corner); // every map corner must remain reachable
				corner.x += x ? 100.0f : -100.0f;
				corner.y += y ? 100.0f : -100.0f;
				allowed = constrainCameraPosition(corner, boundary);
				CHECK(allowed == corner); // there is useful room outside both edges
				Coord3D outsideMap = {x ? 1e7f : -1e7f, y ? 1e7f : -1e7f, corner.z};
				Coord3D limited = constrainCameraPosition(outsideMap, boundary);
				CHECK_NEAR(limited.x, (x ? map.hi.x + 200.0f : map.lo.x - 200.0f), 0.00001f);
				CHECK_NEAR(limited.y, (y ? map.hi.y + 200.0f : map.lo.y - 200.0f), 0.00001f);
				CHECK_EQ(limited.z, corner.z);
				Coord3D inward = limited;
				inward.x += x ? -10.0f : 10.0f;
				inward.y += y ? -10.0f : 10.0f;
				allowed = constrainCameraPosition(inward, boundary);
				CHECK(allowed == inward); // reaching a corner must not trap the camera
			}
	}
}

TEST(camera_margin_preferences_load_and_rebuild_constraints)
{
	CHECK(bootOnce());
	CameraInputFixture f;
	UserPreferences pref;
	pref[AsciiString("CameraBoundaryMargin")] = AsciiString("300");
	loadOptionsFromPreferences(pref);
	CHECK(f.global->m_useCameraConstraints);
	CHECK_EQ(f.global->m_cameraBoundaryMargin, 300);
	CHECK_EQ(f.view.constraintRecalcs, 1);

	// a pan puts the margin back
	GameMessage *key = newInstance(GameMessage)(GameMessage::MSG_RAW_KEY_DOWN);
	key->appendIntegerArgument(KEY_RIGHT);
	key->appendIntegerArgument(KEY_STATE_DOWN);
	f.camera.translateGameMessage(key);
	key->deleteInstance();
	CHECK(f.ui->scrolling);
	CHECK_EQ(f.view.constraintRecalcs, 2);
}

TEST(camera_wheel_anchors_zoom_to_cursor_only_with_the_box_ticked)
{
	// Pull request 5 asked for zoom to cursor to go; it stayed as a menu box instead.
	const OptionDef *def = findOptionDef("WheelZoomToCursor");
	CHECK(def != NULL);
	if (def == NULL)
		return;
	CHECK(def->widgetName != NULL && def->widgetName[0] != '\0');
	CHECK_EQ(def->apply, APPLY_LIVE);

	CHECK(bootOnce());
	CameraInputFixture f;
	const ICoord2D cursor = {320, 240};
	const Real zoomInOneNotch = 1.0f;
	const auto spinWheel = [&f, &cursor, zoomInOneNotch]()
	{
		GameMessage *message = newInstance(GameMessage)(GameMessage::MSG_RAW_MOUSE_WHEEL);
		message->appendPixelArgument(cursor);
		message->appendRealArgument(zoomInOneNotch);
		f.camera.translateGameMessage(message);
		message->deleteInstance();
	};

	// off by default: the wheel zooms on the middle of the screen, as the game shipped
	CHECK(!f.global->m_zoomToCursor);
	spinWheel();
	CHECK_EQ(f.view.zoomAnchors, 0);

	f.global->m_zoomToCursor = TRUE;
	spinWheel();
	CHECK_EQ(f.view.zoomAnchors, 1);
	CHECK_EQ(f.view.zoomAnchor.x, cursor.x);
	CHECK_EQ(f.view.zoomAnchor.y, cursor.y);

	// Classic honours the box too
	f.global->m_mouseStyle = MOUSE_STYLE_CLASSIC;
	spinWheel();
	CHECK_EQ(f.view.zoomAnchors, 2);
	f.global->m_zoomToCursor = FALSE;
	spinWheel();
	CHECK_EQ(f.view.zoomAnchors, 2);
}

TEST(ctrl_wheel_zooms_five_notches_for_one)
{
	CHECK(bootOnce());
	CameraInputFixture f;
	const auto heightAfterOneNotch = [&f]()
	{
		f.view.setHeightAboveGround(1000.0f);
		GameMessage *message = newInstance(GameMessage)(GameMessage::MSG_RAW_MOUSE_WHEEL);
		const ICoord2D cursor = {320, 240};
		message->appendPixelArgument(cursor);
		message->appendRealArgument(1.0f);
		f.camera.translateGameMessage(message);
		message->deleteInstance();
		return f.view.getHeightAboveGround();
	};

	const Real plain = 1000.0f - heightAfterOneNotch();
	CHECK(plain > 0.0f);
	f.keyboard.modifiers(KEY_STATE_LCONTROL);
	const Real held = 1000.0f - heightAfterOneNotch();
	CHECK_NEAR(held, 5.0f * plain, 0.01f);
	// Shift is the row placement's key and changes nothing here
	f.keyboard.modifiers(KEY_STATE_LSHIFT);
	CHECK_NEAR(1000.0f - heightAfterOneNotch(), plain, 0.01f);

	// Classic's Ctrl is force fire, so its notch is the plain one, Ctrl or not, as fast as Reforged's
	f.global->m_mouseStyle = MOUSE_STYLE_CLASSIC;
	f.keyboard.modifiers(KEY_STATE_LCONTROL);
	CHECK_NEAR(1000.0f - heightAfterOneNotch(), plain, 0.01f);

	// Zoom Speed scales the notch in both interfaces, and the scroll speed does not touch it
	f.keyboard.modifiers(0);
	CHECK_EQ(f.global->m_zoomSpeed, 100);
	f.global->m_zoomSpeed = 200;
	CHECK_NEAR(1000.0f - heightAfterOneNotch(), 2.0f * plain, 0.01f);
	f.global->m_zoomSpeed = 100;
	f.global->m_keyboardScrollFactor = 1.0f;
	CHECK_NEAR(1000.0f - heightAfterOneNotch(), plain, 0.01f);
}

TEST(alt_wheel_and_alt_middle_drag_tilt_between_20_and_60_degrees)
{
	CHECK(bootOnce());
	CameraInputFixture f;
	GameClient *savedClient = TheGameClient;
	TheGameClient = getTestGameClient();
	f.global->m_windowed = TRUE;
	f.global->m_edgeScrollInWindowedMode = FALSE;
	f.global->m_cameraPitch = 37.5f;
	const Real DEGREES = PI / 180.0f;
	const auto elevation = [&f, DEGREES]() { return f.global->m_cameraPitch - f.view.getPitch() / DEGREES; };
	const auto wheel = [&f](Real notches)
	{
		GameMessage *message = newInstance(GameMessage)(GameMessage::MSG_RAW_MOUSE_WHEEL);
		const ICoord2D cursor = {320, 240};
		message->appendPixelArgument(cursor);
		message->appendRealArgument(notches);
		f.camera.translateGameMessage(message);
		message->deleteInstance();
	};
	const auto mouse = [&f](GameMessage::Type type, Int x, Int y)
	{
		GameMessage *message = newInstance(GameMessage)(type);
		const ICoord2D pixel = {x, y};
		message->appendPixelArgument(pixel);
		f.camera.translateGameMessage(message);
		message->deleteInstance();
	};

	f.view.setPitch(0.0f);
	f.view.setHeightAboveGround(1000.0f);
	f.keyboard.modifiers(KEY_STATE_LALT);
	// a notch forward lays the camera 2 degrees flatter and does not zoom
	wheel(1.0f);
	CHECK_NEAR(elevation(), 35.5f, 0.001f);
	CHECK_NEAR(f.view.getHeightAboveGround(), 1000.0f, 0.001f);
	wheel(50.0f);
	CHECK_NEAR(elevation(), 20.0f, 0.001f);
	wheel(-50.0f);
	CHECK_NEAR(elevation(), 60.0f, 0.001f);

	// Alt+middle drag tilts instead of turning, inside the same limits
	f.view.setPitch(0.0f);
	const Real heading = f.view.getAngle();
	mouse(GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN, 300, 300);
	mouse(GameMessage::MSG_RAW_MOUSE_POSITION, 340, 310);
	CHECK(f.camera.isMovingCamera());
	CHECK_NEAR(elevation(), 37.5f + 0.1f / DEGREES, 0.001f);
	CHECK_EQ(f.view.getAngle(), heading);
	mouse(GameMessage::MSG_RAW_MOUSE_POSITION, 340, 900);
	CHECK_NEAR(elevation(), 60.0f, 0.001f);
	mouse(GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_UP, 340, 900);
	CHECK(!f.camera.isMovingCamera());

	// without Alt the wheel zooms again and leaves the tilt alone
	f.keyboard.modifiers(0);
	wheel(1.0f);
	CHECK_NEAR(elevation(), 60.0f, 0.001f);
	CHECK(f.view.getHeightAboveGround() < 1000.0f);
	TheGameClient = savedClient;
}

TEST(right_drag_follows_the_scroll_speed)
{
	CHECK(bootOnce());
	CameraInputFixture f;
	GameClient *savedClient = TheGameClient;
	TheGameClient = getTestGameClient();
	f.global->m_windowed = TRUE;
	f.global->m_edgeScrollInWindowedMode = FALSE;
	f.mouse.m_dragTolerance = 4;
	f.view.setHeightAboveGround(f.global->m_maxCameraHeight);
	const auto mouse = [&f](GameMessage::Type type, Int x, Int y)
	{
		GameMessage *message = newInstance(GameMessage)(type);
		const ICoord2D pixel = {x, y};
		message->appendPixelArgument(pixel);
		f.camera.translateGameMessage(message);
		message->deleteInstance();
	};
	const auto dragStep = [&f, &mouse](Real speed)
	{
		f.global->m_keyboardScrollFactor = speed;
		mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300);
		mouse(GameMessage::MSG_RAW_MOUSE_POSITION, 340, 300);
		GameMessage *tick = newInstance(GameMessage)(GameMessage::MSG_FRAME_TICK);
		f.camera.translateGameMessage(tick);
		tick->deleteInstance();
		mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 340, 300);
		return f.view.last.x;
	};

	const Real normal = dragStep(f.global->m_keyboardDefaultScrollFactor);
	CHECK(normal > 0.0f);
	// the drag's own pull doubled with the slider; before, only the small floor under it moved
	const Real doubled = dragStep(2.0f * f.global->m_keyboardDefaultScrollFactor);
	CHECK(doubled > 1.9f * normal);
	TheGameClient = savedClient;
}

TEST(edge_scroll_ramps_from_the_band_to_the_old_three_pixels)
{
	// three percent of the height, and a tiny window keeps the three pixels it always had
	CHECK_EQ(EdgeScroll_bandForHeight(1080), 32);
	CHECK_EQ(EdgeScroll_bandForHeight(1440), 43);
	CHECK_EQ(EdgeScroll_bandForHeight(60), 3);

	const Int band = EdgeScroll_bandForHeight(1080);
	// outside the band nothing; on the last three pixels what the game always gave
	CHECK_EQ(EdgeScroll_strength(band, band), 0.0f);
	CHECK_EQ(EdgeScroll_strength(500, band), 0.0f);
	for (Int distance = 0; distance < 3; ++distance)
		CHECK_EQ(EdgeScroll_strength(distance, band), 1.0f);
	// entering the band barely moves the map, and every pixel deeper moves it faster
	CHECK(EdgeScroll_strength(band - 1, band) > 0.0f);
	CHECK(EdgeScroll_strength(band - 1, band) < 0.05f);
	for (Int distance = 1; distance < band; ++distance)
		CHECK(EdgeScroll_strength(distance, band) <= EdgeScroll_strength(distance - 1, band));

	// over a window the band is the three pixels alone: full speed on them, nothing a pixel in
	CHECK_EQ(EdgeScroll_strength(2, 3), 1.0f);
	CHECK_EQ(EdgeScroll_strength(3, 3), 0.0f);
}

TEST(arrow_keys_scroll_the_camera_and_w_a_s_d_stay_grid_keys)
{
	CHECK(bootOnce());
	CameraInputFixture f;
	const auto press = [&f](UnsignedByte key, Bool down)
	{
		GameMessage *message = newInstance(GameMessage)(down ? GameMessage::MSG_RAW_KEY_DOWN : GameMessage::MSG_RAW_KEY_UP);
		message->appendIntegerArgument(key);
		message->appendIntegerArgument(down ? KEY_STATE_DOWN : KEY_STATE_UP);
		f.camera.translateGameMessage(message);
		message->deleteInstance();
	};

	// D is a command bar key and scrolls nothing; the arrow keys scroll behind the scroll arrows.
	press(KEY_D, TRUE);
	CHECK(!f.ui->scrolling);
	press(KEY_D, FALSE);
	press(KEY_RIGHT, TRUE);
	CHECK(f.ui->scrolling);
	CHECK_EQ(f.mouse.getMouseCursor(), Mouse::SCROLL);
	// letting go of a D that never scrolled leaves the arrow key's scroll running
	press(KEY_D, FALSE);
	CHECK(f.ui->scrolling);
	press(KEY_RIGHT, FALSE);
	CHECK(!f.ui->scrolling);
}

TEST(a_match_opens_300_over_the_ground_in_both_interfaces)
{
	CHECK(bootOnce());
	CameraInputFixture f;
	CHECK(!f.global->m_startAtMaxZoom);
	for (Int style = 0; style < 2; ++style)
	{
		f.global->m_mouseStyle = style ? MOUSE_STYLE_CLASSIC : MOUSE_STYLE_REFORGED;
		f.global->m_startAtMaxZoom = FALSE;
		f.global->m_classicGraphics = FALSE;
		f.view.setZoomToStart(FALSE);
		CHECK_NEAR(f.view.getHeightAboveGround(), 300.0f, 0.001f);
		// Classic Graphics opens on the map's framing, as EA's did
		f.global->m_classicGraphics = TRUE;
		f.view.setZoomToStart(FALSE);
		CHECK_NEAR(f.view.getHeightAboveGround(), -1.0f, 0.001f);
		// the shell map behind the menus keeps its own framing
		f.view.setZoomToStart(TRUE);
		CHECK_NEAR(f.view.getHeightAboveGround(), -1.0f, 0.001f);
		// the box ticked opens it as far out as the wheel goes, Classic too
		f.global->m_startAtMaxZoom = TRUE;
		f.view.setZoomToStart(FALSE);
		CHECK_NEAR(f.view.getHeightAboveGround(), -2.0f, 0.001f);
	}
}

TEST(scroll_speed_follows_the_zoom_in_both_interfaces)
{
	CHECK(bootOnce());
	CameraInputFixture f;
	const auto stepAt = [&f](Real height)
	{
		f.view.setHeightAboveGround(height);
		GameMessage *message = newInstance(GameMessage)(GameMessage::MSG_FRAME_TICK);
		f.camera.translateGameMessage(message);
		message->deleteInstance();
		return f.view.last.x;
	};
	GameMessage *key = newInstance(GameMessage)(GameMessage::MSG_RAW_KEY_DOWN);
	key->appendIntegerArgument(KEY_RIGHT);
	key->appendIntegerArgument(KEY_STATE_DOWN);
	f.camera.translateGameMessage(key);
	key->deleteInstance();
	CHECK(f.ui->scrolling);

	const Real maxHeight = f.global->m_maxCameraHeight;
	for (Int style = 0; style < 2; ++style)
	{
		f.global->m_mouseStyle = style ? MOUSE_STYLE_CLASSIC : MOUSE_STYLE_REFORGED;
		f.global->m_keyboardScrollFactor = 0.5f;
		// where a map opens, the step the arrow keys always took: SCROLL_AMT 100 times the speed
		const Real opening = stepAt(maxHeight);
		CHECK_NEAR(opening, f.global->m_horizontalScrollSpeedFactor * 100.0f * 0.5f, 0.001f);
		// from the farthest zoom it is 2.9 times that, so the ground crosses the screen as fast
		CHECK_NEAR(stepAt(maxHeight * 2.9f), 2.9f * opening, 0.001f);
		// and the scroll speed still sets the pace at every height
		f.global->m_keyboardScrollFactor = 1.0f;
		CHECK_NEAR(stepAt(maxHeight * 2.9f), 2.0f * 2.9f * opening, 0.001f);
		CHECK_NEAR(stepAt(maxHeight * 0.3f), 2.0f * 0.3f * opening, 0.001f);
	}
}

TEST(right_drag_pans_past_the_drag_tolerance_and_middle_drag_turns)
{
	CHECK(bootOnce());
	CameraInputFixture f;
	GameClient *savedClient = TheGameClient;
	TheGameClient = getTestGameClient();
	// no edge scrolling, so the pointer positions below mean only what the buttons make of them
	f.global->m_windowed = TRUE;
	f.global->m_edgeScrollInWindowedMode = FALSE;
	f.global->m_snapCameraRotateTo45 = FALSE;
	f.mouse.m_dragTolerance = 4;
	const auto mouse = [&f](GameMessage::Type type, Int x, Int y)
	{
		GameMessage *message = newInstance(GameMessage)(type);
		const ICoord2D pixel = {x, y};
		message->appendPixelArgument(pixel);
		f.camera.translateGameMessage(message);
		message->deleteInstance();
	};

	// a right click that wobbles inside the tolerance stays an order and moves nothing
	mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300);
	mouse(GameMessage::MSG_RAW_MOUSE_POSITION, 303, 302);
	CHECK(!f.ui->scrolling);
	CHECK(!f.camera.isRightDragPanning());
	mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 303, 302);
	CHECK(!f.ui->scrolling);

	// past the tolerance the same button pans, anchored where it went down, until it comes up
	mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300);
	mouse(GameMessage::MSG_RAW_MOUSE_POSITION, 340, 300);
	CHECK(f.ui->scrolling);
	CHECK(f.camera.isRightDragPanning());
	CHECK(f.camera.getScrollAnchor() != NULL && f.camera.getScrollAnchor()->x == 300);
	mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 340, 300);
	CHECK(!f.ui->scrolling);
	CHECK(!f.camera.isRightDragPanning());

	// a middle drag turns the camera and pans nothing, with no modifier held
	const Real before = f.view.getAngle();
	mouse(GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN, 300, 300);
	mouse(GameMessage::MSG_RAW_MOUSE_POSITION, 350, 300);
	CHECK(!f.ui->scrolling);
	CHECK(f.camera.isMovingCamera());
	CHECK(f.view.getAngle() != before);
	mouse(GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_UP, 350, 300);
	CHECK(!f.camera.isMovingCamera());

	// Shift flips the 45 degree steps for the drag: with them off, the held key lands it on an eighth,
	// and with them on it turns freely.  0.1 off an eighth is no eighth, so each check can fail.
	f.view.setAngle( 0.1f );
	f.keyboard.modifiers(KEY_STATE_LSHIFT);
	mouse(GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN, 300, 300);
	mouse(GameMessage::MSG_RAW_MOUSE_POSITION, 370, 300);
	CHECK_NEAR(f.view.getAngle(), PI / 4.0f, 0.0001f);
	mouse(GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_UP, 370, 300);
	f.global->m_snapCameraRotateTo45 = TRUE;
	f.view.setAngle( 0.1f );
	mouse(GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN, 300, 300);
	mouse(GameMessage::MSG_RAW_MOUSE_POSITION, 310, 300);
	CHECK_NEAR(f.view.getAngle(), 0.2f, 0.0001f);
	mouse(GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_UP, 310, 300);
	f.keyboard.modifiers(0);
	f.global->m_snapCameraRotateTo45 = FALSE;

	// Classic's right drag is the same retail scroll, started by the press itself as 1.04 started it
	f.global->m_mouseStyle = MOUSE_STYLE_CLASSIC;
	mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300);
	CHECK(f.ui->scrolling);
	CHECK(f.camera.isRightDragPanning());
	mouse(GameMessage::MSG_RAW_MOUSE_POSITION, 340, 300);
	CHECK(f.camera.isRightDragPanning());
	mouse(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 340, 300);
	CHECK(!f.camera.isRightDragPanning());

	TheGameClient = savedClient;
}
