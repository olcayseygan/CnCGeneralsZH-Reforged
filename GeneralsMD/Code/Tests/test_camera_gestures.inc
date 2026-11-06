// Continuous local camera input: production displacement/zoom paths and GUI ownership.
// Included after test_camera_behavior.inc and test_production_input.inc for their fixtures.
#include "Common/LogicThread.h"
#include "../Libraries/Source/WWVegas/WW3D2/camera.h"

namespace
{
class GestureCameraUI : public CameraTestUI
{
public:
	Bool placing = FALSE, anchored = FALSE;
	const ThingTemplate *getPendingPlaceType() { return placing ? reinterpret_cast<const ThingTemplate *>(this) : NULL; }
	Bool isPlacementAnchored() { return anchored; }
};

class GestureCameraView : public CameraTestView
{
public:
	Bool scripted = FALSE, frozen = FALSE, freeCamera = FALSE;
	Bool clampPan = FALSE, picksInside = TRUE;
	Int constraintApplications = 0, redraws = 0;
	Bool isCameraMoving() { return scripted; }
	Bool isTimeFrozen() { return frozen; }
	Bool isFreeCamera() const { return freeCamera; }
	void forceRedraw() { ++redraws; }
	void setHeightAboveGround(Real value)
	{
		// The real W3D setter clamps the same requested height before easing the render camera.
		View::setHeightAboveGround(value < 90.0f ? 90.0f : (value > 900.0f ? 900.0f : value));
	}
	void screenToWorldAtZ(const ICoord2D *pixel, Coord3D *world, Real z)
	{
		picksInside = picksInside && pixel->x >= m_originX && pixel->x < m_originX + m_width
			&& pixel->y >= m_originY && pixel->y < m_originY + m_height;
		const Real x = (Real)(pixel->x - m_originX - m_width / 2) * getHeightAboveGround() / m_width;
		const Real y = (Real)(pixel->y - m_originY - m_height / 2) * getHeightAboveGround() / m_width;
		// The view supplies the projected basis. The 2:1 vertical footprint is a tilted camera;
		// rotating it tests both screen axes without a GPU or depending on a camera test double's pan.
		world->x = m_pos.x + x * cosf(m_angle) - 2.0f * y * sinf(m_angle);
		world->y = m_pos.y + x * sinf(m_angle) + 2.0f * y * cosf(m_angle);
		world->z = z;
	}
	void applyCameraConstraint()
	{
		++constraintApplications;
		if (clampPan) {
			m_pos.x = m_pos.x < -100.0f ? -100.0f : (m_pos.x > 100.0f ? 100.0f : m_pos.x);
			m_pos.y = m_pos.y < -100.0f ? -100.0f : (m_pos.y > 100.0f ? 100.0f : m_pos.y);
		}
	}
};

class PerspectiveGestureCameraView : public GestureCameraView
{
	CameraClass camera;
public:
	void aimCamera()
	{
		camera.Set_Aspect_Ratio((Real)m_width / m_height);
		camera.Set_View_Plane(50.0f * PI / 180.0f);
		const Vector3 target(m_pos.x, m_pos.y, 0.0f);
		const Real height = getHeightAboveGround();
		const Vector3 eye = target + Vector3(height * sinf(m_angle), -height * cosf(m_angle), height);
		Matrix3D transform;
		transform.Look_At(eye, target, 0.0f);
		camera.Set_Transform(transform);
	}
	void screenToWorldAtZ(const ICoord2D *pixel, Coord3D *world, Real z)
	{
		const Vector2 screen(2.0f * (pixel->x - m_originX) / m_width - 1.0f,
			1.0f - 2.0f * (pixel->y - m_originY) / m_height);
		Vector3 onPlane;
		camera.Un_Project(onPlane, screen);
		const Vector3 eye = camera.Get_Position(), direction = onPlane - eye;
		const Vector3 point = eye + direction * ((z - eye.Z) / direction.Z);
		world->x = point.X; world->y = point.Y; world->z = z;
	}
};

class TwoDimensionalGestureView : public GestureCameraView
{
public:
	void lookAt(const Coord3D *position) { ++lookAtCalls; View::lookAt(position); }
};

struct CameraGestureFixture
{
	CameraInputFixture engine;
	GestureCameraView view;
	Int savedMode;
	ObserverCameraMode savedObserver;
	CameraGestureFixture() : savedMode(TheGameLogic->getGameMode()), savedObserver(TheObserverCamera.getMode())
	{
		TheTacticalView = &view;
		// As in CameraInputFixture, the UI destructor needs a real device, so keep its test instance.
		static GestureCameraUI *ui = NEW GestureCameraUI;
		ui->placing = ui->anchored = ui->selecting = ui->scrolling = FALSE;
		ui->enabled = TRUE;
		engine.ui = ui;
		TheInGameUI = ui;
		view.setWidth(800);
		view.setHeight(600);
		view.setOrigin(0, 0);
		view.setHeightAboveGround(400.0f);
		const Coord3D initial = {0, 0, 0};
		view.setPosition(&initial);
		TheGameLogic->setGameMode(GAME_SKIRMISH);
		engine.global->m_disableCameraMovement = FALSE;
		engine.global->m_directorRecord = FALSE;
		engine.global->m_cinemaScript.clear();
	}
	~CameraGestureFixture()
	{
		TheGameLogic->setGameMode(savedMode);
		TheObserverCamera.setMode(savedObserver);
		TheTacticalView = &engine.view;
	}
	Bool apply(Real x, Real y, Real scale = 1.0f)
	{
		const ICoord2D pixel = {400, 300};
		return engine.camera.applyCameraGesture(pixel, x, y, scale);
	}
};
}

TEST(camera_gesture_pan_keeps_subpixels_direction_rotation_and_retina_scale)
{
	CHECK(bootOnce());
	CameraGestureFixture f;
	const Coord3D initial = {0, 0, 0};
	f.view.setPosition(&initial);
	CHECK(f.apply(0.25f, 0.5f));
	CHECK_NEAR(f.view.getPosition()->x, -0.125f, 0.0001f);
	CHECK_NEAR(f.view.getPosition()->y, -0.5f, 0.0001f);
	CHECK_EQ(f.view.samples, 0); // No call to the time-scaled scrollBy API.
	CHECK(!f.engine.camera.isMovingCamera());
	CHECK(!f.engine.ui->scrolling);
	CHECK(!f.view.isMouseLocked());
	CHECK_EQ(f.engine.mouse.captures, 0);

	f.view.setPosition(&initial);
	CHECK(f.apply(12.5f, -5.0f));
	const Coord3D whole = *f.view.getPosition();
	f.view.setPosition(&initial);
	for (Int event = 0; event < 100; ++event) CHECK(f.apply(0.125f, -0.05f));
	CHECK_NEAR(f.view.getPosition()->x, whole.x, 0.001f);
	CHECK_NEAR(f.view.getPosition()->y, whole.y, 0.001f);

	// A Retina backing doubles game pixels and the converted native-point displacement together.
	f.view.setPosition(&initial);
	f.view.setWidth(1600);
	f.view.setHeight(1200);
	CHECK(f.apply(25.0f, -10.0f));
	CHECK_NEAR(f.view.getPosition()->x, whole.x, 0.001f);
	CHECK_NEAR(f.view.getPosition()->y, whole.y, 0.001f);

	f.view.setPosition(&initial);
	f.view.setAngle(PI * 0.5f);
	CHECK(f.apply(2.0f, 0.0f));
	CHECK_NEAR(f.view.getPosition()->x, 0.0f, 0.001f);
	CHECK_NEAR(f.view.getPosition()->y, -0.5f, 0.001f);
	f.view.clampPan = TRUE;
	CHECK(f.apply(0.0f, 10000.0f));
	CHECK_NEAR(f.view.getPosition()->x, 100.0f, 0.001f);
	CHECK(f.view.constraintApplications > 0 && f.view.redraws > 0);
}

TEST(camera_gesture_pinch_multiplies_requested_height_and_uses_existing_zoom_bounds)
{
	CHECK(bootOnce());
	CameraGestureFixture f;
	CHECK(f.apply(0, 0, 1.25f));
	CHECK_NEAR(f.view.getHeightAboveGround(), 320.0f, 0.001f);
	CHECK(f.apply(0, 0, 0.5f));
	CHECK_NEAR(f.view.getHeightAboveGround(), 640.0f, 0.001f);
	f.view.setHeightAboveGround(400.0f);
	const Real halfScale = sqrtf(1.25f);
	CHECK(f.apply(0, 0, halfScale));
	CHECK(f.apply(0, 0, halfScale));
	CHECK_NEAR(f.view.getHeightAboveGround(), 320.0f, 0.001f);
	f.engine.global->m_zoomSpeed = 200;
	f.view.setHeightAboveGround(400.0f);
	CHECK(f.apply(0, 0, 1.25f));
	CHECK_NEAR(f.view.getHeightAboveGround(), 256.0f, 0.001f);
	CHECK(f.apply(0, 0, 100.0f));
	CHECK_NEAR(f.view.getHeightAboveGround(), 90.0f, 0.001f);
	CHECK(f.apply(0, 0, 0.01f));
	CHECK_NEAR(f.view.getHeightAboveGround(), 900.0f, 0.001f);
	f.engine.global->m_zoomToCursor = TRUE;
	CHECK(f.apply(0, 0, 1.1f));
	CHECK_EQ(f.view.zoomAnchors, 1);
	CHECK_EQ(f.view.zoomAnchor.x, 400);
	CHECK_EQ(f.view.zoomAnchor.y, 300);
	// Modifier keys retain their wheel/build-placement meaning; native pinch never sends a wheel.
	f.engine.keyboard.modifiers(KEY_STATE_LCONTROL | KEY_STATE_LALT);
	f.view.setHeightAboveGround(400.0f);
	f.engine.global->m_zoomSpeed = 100;
	CHECK(f.apply(0, 0, 1.25f));
	CHECK_NEAR(f.view.getHeightAboveGround(), 320.0f, 0.001f);
	CHECK_NEAR(f.view.getPitch(), 0.0f, 0.001f);
}

TEST(camera_gesture_pan_projects_through_a_real_perspective_camera_at_each_zoom)
{
	CHECK(bootOnce());
	CameraGestureFixture f;
	PerspectiveGestureCameraView view;
	view.setWidth(800); view.setHeight(600); view.setHeightAboveGround(400);
	const Coord3D initial = {0, 0, 0};
	view.setPosition(&initial);
	TheTacticalView = &view;
	CHECK(f.apply(10, 0));
	// A 45-degree eye 400 above the plane, 50-degree horizontal field, ten of 800 pixels.
	const Real distance = 400.0f * sqrtf(2.0f) * tanf(25.0f * PI / 180.0f) * 20.0f / 800.0f;
	CHECK_NEAR(view.getPosition()->x, -distance, 0.002f);
	CHECK_NEAR(view.getPosition()->y, 0.0f, 0.002f);
	view.setPosition(&initial);
	view.setHeightAboveGround(800);
	CHECK(f.apply(10, 0));
	CHECK_NEAR(view.getPosition()->x, -2.0f * distance, 0.004f);
	view.setPosition(&initial);
	view.setWidth(1600); view.setHeight(1200);
	CHECK(f.apply(20, 0));
	CHECK_NEAR(view.getPosition()->x, -2.0f * distance, 0.004f);
	view.setPosition(&initial);
	view.setAngle(PI * 0.5f);
	CHECK(f.apply(20, 0));
	CHECK_NEAR(view.getPosition()->x, 0.0f, 0.004f);
	CHECK_NEAR(view.getPosition()->y, -2.0f * distance, 0.004f);
	TheTacticalView = &f.view;
}

TEST(camera_gesture_pan_preserves_height_pitch_and_z_without_reaiming_the_view)
{
	CHECK(bootOnce());
	CameraGestureFixture f;
	const Coord3D elevated = {10, 20, 50};
	f.view.setPosition(&elevated);
	f.view.setTerrainHeightUnderCamera(-100);
	f.view.setPitch(0.2f);
	CHECK(f.apply(10, 20));
	CHECK_NEAR(f.view.getPosition()->x, 5.0f, 0.001f);
	CHECK_NEAR(f.view.getPosition()->y, 0.0f, 0.001f);
	CHECK_NEAR(f.view.getPosition()->z, 50.0f, 0.001f);
	CHECK_NEAR(f.view.getHeightAboveGround(), 400.0f, 0.001f);
	CHECK_NEAR(f.view.getPitch(), 0.2f, 0.001f);
	CHECK_EQ(f.view.lookAtCalls, 0); // An elevated point on negative terrain must not become an object-aim call.

	TwoDimensionalGestureView flat;
	flat.setWidth(800); flat.setHeight(600); flat.setHeightAboveGround(400);
	flat.setPosition(&elevated);
	CHECK(flat.panByScreenDelta(10, 20));
	CHECK_NEAR(flat.getPosition()->x, 5.0f, 0.001f);
	CHECK_NEAR(flat.getPosition()->y, 0.0f, 0.001f);
	CHECK_NEAR(flat.getPosition()->z, 50.0f, 0.001f);
	CHECK_EQ(flat.lookAtCalls, 0); // Base View::lookAt subtracts half the view; a pan is not a recenter.
}

TEST(camera_gesture_rejects_invalid_and_nonworld_input_without_partial_changes)
{
	CHECK(bootOnce());
	CameraGestureFixture f;
	const Coord3D original = *f.view.getPosition();
	CHECK(f.apply(0, 0)); // An ownership query changes no state.
	CHECK_EQ(f.view.lookAtCalls, 0);
	CHECK_EQ(f.view.redraws, 0);
	CHECK(!f.apply(std::numeric_limits<Real>::quiet_NaN(), 1));
	CHECK(!f.apply(1, 1, -1));
	CHECK(!f.apply(1, 1, 0));
	CHECK(!f.apply(1, 1, std::numeric_limits<Real>::infinity()));
	CHECK(!f.apply(1, 1, std::numeric_limits<Real>::denorm_min()));
	CHECK_EQ(f.view.lookAtCalls, 0);
	CHECK_NEAR(f.view.getPosition()->x, original.x, 0.0f);
	const ICoord2D outside = {900, 300};
	CHECK(!f.engine.camera.applyCameraGesture(outside, 1, 1, 1));
	f.view.setWidth(1);
	CHECK(!f.apply(1, 1));
	f.view.setWidth(800);
	f.view.setOrigin(100, 50);
	const ICoord2D left = {99, 300}, top = {400, 49}, right = {900, 300}, bottom = {400, 650};
	CHECK(!f.engine.camera.applyCameraGesture(left, 1, 1, 1));
	CHECK(!f.engine.camera.applyCameraGesture(top, 1, 1, 1));
	CHECK(!f.engine.camera.applyCameraGesture(right, 1, 1, 1));
	CHECK(!f.engine.camera.applyCameraGesture(bottom, 1, 1, 1));
	f.view.setOrigin(0, 0);
	f.view.setWidth(2); f.view.setHeight(2);
	const ICoord2D tiny = {0, 0};
	CHECK(f.engine.camera.applyCameraGesture(tiny, 0.01f, 0.01f, 1));
	CHECK(f.view.picksInside);
	f.view.setWidth(800); f.view.setHeight(600);
	const auto rejected = [&f]() { CHECK(!f.apply(1, 1, 1.1f)); };
	f.engine.ui->enabled = FALSE; rejected(); f.engine.ui->enabled = TRUE;
	f.engine.ui->selecting = TRUE; rejected(); f.engine.ui->selecting = FALSE;
	static_cast<GestureCameraUI *>(f.engine.ui)->placing = TRUE; rejected();
	static_cast<GestureCameraUI *>(f.engine.ui)->placing = FALSE;
	static_cast<GestureCameraUI *>(f.engine.ui)->anchored = TRUE; rejected();
	static_cast<GestureCameraUI *>(f.engine.ui)->anchored = FALSE;
	f.engine.global->m_disableCameraMovement = TRUE; rejected(); f.engine.global->m_disableCameraMovement = FALSE;
	f.engine.global->m_directorRecord = TRUE; rejected(); f.engine.global->m_directorRecord = FALSE;
	f.engine.global->m_cinemaScript = "test"; rejected(); f.engine.global->m_cinemaScript.clear();
	f.view.scripted = TRUE; rejected(); f.view.scripted = FALSE;
	f.view.frozen = TRUE; rejected(); f.view.frozen = FALSE;
	f.view.freeCamera = TRUE; rejected(); f.view.freeCamera = FALSE;
	f.view.setCameraLock((ObjectID)123); rejected(); f.view.setCameraLock(INVALID_ID);
	const Int overlap = LogicThread::g_windowOpen.exchange(1);
	rejected();
	LogicThread::g_windowOpen.store(overlap);
	GameMessage *press = newInstance(GameMessage)(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN);
	const ICoord2D cursor = {400, 300};
	press->appendPixelArgument(cursor);
	f.engine.camera.translateGameMessage(press);
	press->deleteInstance();
	rejected();
	f.engine.camera.resetModes();
	CHECK(f.apply(0, 0)); // Losing focus also cancels a press that never became a drag.
	TheGameLogic->setGameMode(GAME_SHELL); rejected();
	TheGameLogic->setGameMode(GAME_NONE); rejected();
	TheGameLogic->setGameMode(GAME_REPLAY);
	CHECK(f.apply(1, 1)); // A replay's manually controlled camera remains usable.
}

TEST(camera_gesture_gui_ownership_and_observer_takeover_are_local)
{
	CHECK(bootOnce());
	ProductionButtonFixture f;
	GestureCameraView view;
	view.setWidth(800); view.setHeight(600); view.setHeightAboveGround(400);
	const Coord3D initial = {0, 0, 0};
	view.setPosition(&initial);
	TheTacticalView = &view;
	const Int mode = TheGameLogic->getGameMode();
	TheGameLogic->setGameMode(GAME_SKIRMISH);
	const ICoord2D world = {400, 300}, button = {30, 30};
	const auto apply = [&](const ICoord2D &pixel) { return f.engine.camera.applyCameraGesture(pixel, 1, 1, 1); };
	CHECK(!apply(button));
	CHECK(apply(world));
	f.windows.winSetModal(f.parent);
	CHECK(f.windows.hasModalWindow());
	CHECK(!apply(world)); // Outside the modal's hit rectangle still belongs to the modal.
	f.windows.winUnsetModal(f.parent);
	f.windows.winCapture(f.parent); CHECK(!apply(world)); f.windows.winRelease(f.parent);
	f.windows.winSetGrabWindow(f.parent); CHECK(!apply(world)); f.windows.winSetGrabWindow(NULL);
	f.parent->winSetSystemFunc([](GameWindow *, UnsignedInt message, WindowMsgData, WindowMsgData data) {
		if (message == GWM_INPUT_FOCUS) *reinterpret_cast<Bool *>(data) = TRUE;
		return MSG_HANDLED;
	});
	f.windows.winSetFocus(f.parent);
	CHECK(f.windows.winGetFocus() == f.parent);
	CHECK(!apply(world));
	f.windows.winSetFocus(NULL);
	const ObserverCameraMode observer = TheObserverCamera.getMode();
	TheObserverCamera.setMode(OBSERVER_CAMERA_DIRECTOR);
	CHECK(f.engine.camera.applyCameraGesture(world, 0, 0, 1.1f));
	CHECK_EQ(TheObserverCamera.getMode(), OBSERVER_CAMERA_DIRECTOR);
	CHECK(apply(world));
	CHECK_EQ(TheObserverCamera.getMode(), OBSERVER_CAMERA_FREE);
	CHECK(!f.engine.camera.isMovingCamera());
	CHECK(!f.engine.ui->scrolling);
	TheObserverCamera.setMode(observer);
	TheGameLogic->setGameMode(mode);
	TheTacticalView = &f.engine.view;
}

TEST(camera_gesture_pan_preference_is_live_and_leaves_other_controls_unchanged)
{
	CHECK(bootOnce());
	CameraGestureFixture f;
	CHECK(f.engine.global->m_macTrackpadPan);
	const Int zoomSpeed = f.engine.global->m_zoomSpeed;
	const Real scrollSpeed = f.engine.global->m_keyboardScrollFactor;
	const Bool snap = f.engine.global->m_snapCameraRotateTo45;
	const Bool cursor = f.engine.global->m_zoomToCursor;
	const OptionDef *option = findOptionDef("MacTrackpadPan");
	CHECK(option != NULL && option->kind == OPTION_BOOL && option->apply == APPLY_LIVE);
	UserPreferences preferences;
	preferences[AsciiString("MacTrackpadPan")] = AsciiString("no");
	loadOptionsFromPreferences(preferences);
	CHECK(!f.engine.global->m_macTrackpadPan);
	CHECK_EQ(f.engine.global->m_zoomSpeed, zoomSpeed);
	CHECK_EQ(f.engine.global->m_keyboardScrollFactor, scrollSpeed);
	CHECK_EQ(f.engine.global->m_snapCameraRotateTo45, snap);
	CHECK_EQ(f.engine.global->m_zoomToCursor, cursor);
	preferences[AsciiString("MacTrackpadPan")] = AsciiString("yes");
	loadOptionsFromPreferences(preferences);
	CHECK(f.engine.global->m_macTrackpadPan);
	saveOptionsToPreferences(preferences);
	CHECK(preferences.getBool("MacTrackpadPan", FALSE));
}
