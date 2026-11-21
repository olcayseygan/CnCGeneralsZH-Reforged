// test_cinematic_camera.cpp: the scripted camera drawn between its whole steps (PR 260) and the
// animation clock on a repeated picture (PR 261).
// The sampler and the mode rule are header only; the W3DView half of the contributor's tests needs the
// device library, which no Windows test links, so what W3DView does with the sample is not run here.

#include "test_harness.h"

#include "W3DDevice/GameClient/W3DCinematicCamera.h"

static float linearEase(float t) { return t; }

TEST(cinematic_camera_draws_between_steps_only_in_a_plain_single_player_game)
{
	CinematicAnimationClockMode mode;
	CHECK(!mode.allowsRenderTime());
	mode.singlePlayer = true;
	CHECK(mode.allowsRenderTime());
	bool CinematicAnimationClockMode::*excluded[] = {
		&CinematicAnimationClockMode::network, &CinematicAnimationClockMode::replay,
		&CinematicAnimationClockMode::headless, &CinematicAnimationClockMode::shell,
		&CinematicAnimationClockMode::loading, &CinematicAnimationClockMode::fast,
		&CinematicAnimationClockMode::video, &CinematicAnimationClockMode::capture,
		&CinematicAnimationClockMode::pictureClock };
	for (auto flag : excluded)
	{
		mode.*flag = true;
		CHECK(!mode.allowsRenderTime());
		mode.*flag = false;
	}
	CHECK(mode.allowsRenderTime());
}

TEST(cinematic_camera_world_freeze_keeps_moving_while_pause_and_debug_hold)
{
	CHECK(CinematicCameraMovesAllowed(false, false, false));
	CHECK(CinematicCameraMovesAllowed(false, false, true));
	CHECK(!CinematicCameraMovesAllowed(true, false, false));
	CHECK(!CinematicCameraMovesAllowed(false, true, false));
}

TEST(cinematic_camera_starts_part_way_through_a_step_and_holds_on_pause)
{
	// a zoom from 1 to 2 over ten steps, started 60% of the way through the shared step interval
	CinematicCameraProgress progress;
	const int count = 10;
	CHECK_EQ(CinematicCameraProgress::value(1.0f, 2.0f, count, progress.sample(0, count, 0.6f, false), linearEase), 1.0f);
	CHECK_NEAR(CinematicCameraProgress::value(1.0f, 2.0f, count, progress.sample(0, count, 0.8f, false), linearEase), 1.05f, 1.0e-6f);
	const float held = progress.sample(0, count, 0.8f, false);
	const float pausedPhases[] = { 0.95f, 0.1f, 0.8f, 0.3f };
	for (float phase : pausedPhases)
		CHECK_EQ(progress.sample(0, count, phase, true), held);
	// resuming rebases on the clock as it stands, so the time spent paused is not jumped over
	CHECK_EQ(progress.sample(0, count, 0.8f, false), held);
	CHECK_NEAR(CinematicCameraProgress::value(1.0f, 2.0f, count, progress.sample(0, count, 0.9f, false), linearEase), 1.075f, 1.0e-6f);
	CHECK_NEAR(CinematicCameraProgress::value(1.0f, 2.0f, count, progress.sample(1, count, 0.0f, false), linearEase), 1.1f, 1.0e-6f);
	progress.reset();
	CHECK_EQ(progress.sample(3, count, 0.5f, false), 3.0f);
}

TEST(cinematic_camera_pause_resume_handles_a_new_step_and_completion)
{
	CinematicCameraProgress progress;
	progress.sample(0, 3, 0, false);
	CHECK_NEAR(progress.sample(0, 3, 0.8f, false), 0.8f, 1.0e-6f);
	progress.sample(0, 3, 0.1f, true);
	CHECK_NEAR(progress.sample(0, 3, 0.2f, false), 0.8f, 1.0e-6f);
	CHECK_NEAR(progress.sample(0, 3, 0.6f, false), 0.9f, 1.0e-6f);
	progress.sample(0, 3, 0.7f, true);
	CHECK_NEAR(progress.sample(1, 3, 0.8f, false), 0.9f, 1.0e-6f); // the step came while paused
	CHECK_NEAR(progress.sample(1, 3, 0.9f, false), 1.45f, 1.0e-6f);
	CHECK_NEAR(progress.sample(2, 3, 0.0f, false), 2.0f, 1.0e-6f);
	CHECK_EQ(progress.sample(3, 3, 0.9f, false), 3.0f); // never past the last step
}

TEST(cinematic_camera_moves_on_every_picture_at_any_refresh_and_never_backwards)
{
	const int renderRates[] = { 60, 120, 144, 240 };
	for (int renderRate : renderRates)
	{
		CinematicCameraProgress progress;
		const int count = 30;
		float previous = progress.sample(0, count, 0.0f, false);
		int movedBetweenSteps = 0, lastStep = 0;
		for (int picture = 1; picture <= count * renderRate / 30; ++picture)
		{
			const double clock = (double)picture * 30.0 / renderRate;
			const int step = (int)(clock + 1.0e-9) < count ? (int)(clock + 1.0e-9) : count;
			const float shown = progress.sample(step, count, (float)(clock - step), false);
			CHECK(shown >= previous);
			if (step < count)
				CHECK_NEAR(shown, (float)clock, 1.0e-4f);
			if (step == lastStep && shown > previous)
				++movedBetweenSteps;
			previous = shown;
			lastStep = step;
		}
		CHECK_EQ(previous, (float)count);
		CHECK(movedBetweenSteps > 0);
	}
}

TEST(cinematic_camera_value_eases_and_keeps_its_ends)
{
	auto square = [](float t) { return t * t; };
	CHECK_EQ(CinematicCameraProgress::value(1.0f, 3.0f, 4, -1.0f, square), 1.0f);
	CHECK_EQ(CinematicCameraProgress::value(1.0f, 3.0f, 4, 5.0f, square), 3.0f);
	CHECK_NEAR(CinematicCameraProgress::value(1.0f, 3.0f, 4, 2.0f, square), 1.5f, 1.0e-6f);
	// a two-turn sweep keeps its authored arc rather than the short way round
	CHECK_NEAR(CinematicCameraProgress::value(0.0f, 12.566371f, 10, 5.0f, linearEase), 6.2831855f, 1.0e-5f);
	CHECK_EQ(CinematicCameraProgress::picturePhase(5.0f, ~0u, 2), 0.0f);
	CHECK_NEAR(CinematicCameraProgress::picturePhase(5.0f, 2, 2), 0.5f, 1.0e-6f);
	CHECK_EQ(CinematicCameraProgress::picturePhase(9.0f, 2, 2), 1.0f);
}

// PR 261: W3D's animation clock on a picture that repeats the last client frame.
TEST(cinematic_camera_animation_clock_preserves_real_freezes_and_other_clock_modes)
{
	CinematicAnimationClockMode normal;
	normal.singlePlayer = true;
	CHECK(normal.allowsRenderTime());
	CHECK(!CinematicAnimationClockFrozen(false, true, normal));
	CHECK(CinematicAnimationClockFrozen(true, true, normal));
	CHECK(CinematicAnimationClockFrozen(true, false, normal));
	CHECK(!CinematicAnimationClockFrozen(false, false, normal));
	bool CinematicAnimationClockMode::*exclusions[] = {
		&CinematicAnimationClockMode::network, &CinematicAnimationClockMode::replay,
		&CinematicAnimationClockMode::headless, &CinematicAnimationClockMode::shell,
		&CinematicAnimationClockMode::loading, &CinematicAnimationClockMode::fast,
		&CinematicAnimationClockMode::video, &CinematicAnimationClockMode::capture,
		&CinematicAnimationClockMode::pictureClock };
	for (auto exclusion : exclusions)
	{
		CinematicAnimationClockMode mode = normal;
		mode.*exclusion = true;
		CHECK(CinematicAnimationClockFrozen(false, true, mode));
		CHECK(!CinematicAnimationClockFrozen(false, false, mode));
		CHECK(CinematicAnimationClockFrozen(true, false, mode));
	}
	CinematicAnimationClockMode inactive;
	CHECK(CinematicAnimationClockFrozen(false, true, inactive));
	CHECK(!CinematicAnimationClockFrozen(false, false, inactive));
}

// The contributor's version built the view matrix with WWMath's Matrix3D; both are pure translations, so
// the view-space centre is the sum of the two and this binary needs no library.
TEST(cinematic_camera_animation_clock_advances_repeated_client_frames_without_projection_pulses)
{
	const int LOGIC_RATE = 30;	// LOGICFRAMES_PER_SECOND today
	const int speeds[] = { 100, 125, 150, 200 };	// PLAYER_GAME_SPEED_PERCENTS
	const int renderRates[] = { 60, 120, 144 };
	const int logicRates[] = { 15, 20, LOGIC_RATE };
	const unsigned firstFrames[] = { 0u, 100u };
	for (int speed : speeds)
	for (int renderRate : renderRates)
	for (int logicRate : logicRates)
	for (unsigned firstFrame : firstFrames)
	{
		CinematicAnimationClockMode mode[2];
		mode[1].singlePlayer = true;
		unsigned lastFrame = ~0u, previousMs[2] = { 0, 0 }, sync[2] = { 0, 0 };
		double remainder[2] = { 0, 0 };
		int repeats = 0, legacyPulses = 0;
		for (int picture = 0; picture <= renderRate * 3; ++picture)
		{
			const unsigned frame = firstFrame + (unsigned)((long long)picture * logicRate / renderRate);
			const unsigned now = (unsigned)((long long)picture * 1000 / renderRate);
			const bool repeated = lastFrame == frame;
			const double rate = (double)logicRate / LOGIC_RATE * speed / 100.0;
			const unsigned cameraMs = (unsigned)(now * rate + 1.0e-8);
			if (repeated) ++repeats;
			for (int enabled = 0; enabled < 2; ++enabled)
			{
				// W3DDisplay::draw's accumulated wall time on either side of the decision
				if (!CinematicAnimationClockFrozen(false, repeated, mode[enabled]))
				{
					remainder[enabled] += (now - previousMs[enabled]) * rate;
					previousMs[enabled] = now;
					const unsigned delta = (unsigned)(remainder[enabled] + 1.0e-8);
					remainder[enabled] -= delta;
					sync[enabled] += delta;
				}
				// A rigid aircraft flies toward the eye, keeping pace vertically with a moving camera, so
				// its projected centre stays put. Holding only the aircraft on a repeated client frame
				// throws it backwards on the screen for that picture.
				const float centreY = -0.3f * sync[enabled] + 0.3f * cameraMs;
				const float centreZ = 200.0f - 0.02f * sync[enabled];
				const float screenY = 180.0f - 180.0f * centreY / centreZ;
				if (enabled)
				{
					CHECK_EQ(sync[enabled], cameraMs);
					CHECK_NEAR(screenY, 180.0f, 0.0002f);
				}
				else if (repeated && screenY < 179.0f)
					++legacyPulses;
			}
			lastFrame = frame;
		}
		CHECK_EQ(repeats > 0, renderRate > logicRate);
		CHECK_EQ(legacyPulses > 0, renderRate > logicRate);
		CHECK_EQ(sync[0], sync[1]); // the same elapsed time at the last common boundary
	}
}
