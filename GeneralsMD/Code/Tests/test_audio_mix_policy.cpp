/*
 * The arithmetic of MilesAudioManager's battle mix (MilesAudioDevice/AudioMixPolicy.h): which sound
 * gives way when the voices run out, and how much louder a folded shot plays.  The manager itself and
 * its lists are measured in the -wav mix (experiments/proofs/2026-10-25/audio-mix).
 */
#include "test_harness.h"

#include "MilesAudioDevice/AudioMixPolicy.h"

using namespace AudioMixPolicy;

TEST(importance_puts_priority_first_and_the_quieter_sound_below_its_equal)
{
	CHECK(importance(2, 0.9f) < importance(3, 0.0f));
	CHECK(importance(2, 0.1f) < importance(2, 0.8f));
	CHECK_NEAR(importance(1, 3.0f), 2.0f, 1e-6f);	// loudness is held to 0..1
	CHECK_NEAR(importance(1, -1.0f), 1.0f, 1e-6f);
}

TEST(a_folded_shot_grows_a_step_each_and_stops_at_the_ceiling)
{
	CHECK_NEAR(coalescedGain(0, 0.12f, 1.5f), 1.0f, 1e-6f);
	CHECK_NEAR(coalescedGain(2, 0.12f, 1.5f), 1.24f, 1e-6f);
	CHECK_NEAR(coalescedGain(19, 0.12f, 1.5f), 1.5f, 1e-6f);
}
