// Presentation of a scripted camera between its existing whole steps. No clock, camera state or
// completion flag is advanced here: the caller supplies the remainder of the clock it already uses.
#pragma once

// Only ordinary, wall-clock single-player rendering may outrun its client frame. Diagnostic
// pictures retain their existing simulation/picture-clock duplicate rule for the entire session.
struct CinematicAnimationClockMode
{
	bool singlePlayer = false;
	bool network = false;
	bool replay = false;
	bool headless = false;
	bool shell = false;
	bool loading = false;
	bool fast = false;
	bool video = false;
	bool capture = false;
	bool pictureClock = false;

	bool allowsRenderTime() const
	{
		return singlePlayer && !network && !replay && !headless && !shell
			&& !loading && !fast && !video && !capture && !pictureClock;
	}
};

// Snapshot only on the ordinary client/render path, never from a phase-4 extra picture.
CinematicAnimationClockMode CinematicCameraModeForClientUpdate();

inline bool CinematicCameraMovesAllowed(bool paused, bool debugFrozen, bool scriptFrozen)
{
	// A world/script freeze deliberately leaves authored camera motion alive. Pause and the
	// debugger stop it; animation has its separate explicit-freeze rule in W3DDisplay.
	(void)scriptFrozen;
	return !paused && !debugFrozen;
}

class CinematicCameraProgress
{
public:
	CinematicCameraProgress() { reset(); }
	void reset()
	{
		m_first = true;
		m_frozen = false;
		m_anchorStep = 0;
		m_origin = m_anchorProgress = m_progress = 0.0f;
	}

	float sample(int step, int count, float phase, bool frozen)
	{
		phase = clamp(phase);
		if (m_first)
		{
			m_first = false;
			m_origin = phase;
			m_anchorStep = step == 0 ? 0 : -1;
			m_anchorProgress = (float)step;
			m_progress = (float)step;
			m_frozen = frozen;
			return m_progress;
		}
		if (!frozen)
		{
			// The shared legacy clock keeps running while camera movement is paused. Rebase its
			// remaining interval on resume instead of importing the time spent paused into the pose.
			if (m_frozen)
			{
				m_origin = phase;
				m_anchorStep = step;
				m_anchorProgress = m_progress;
			}
			// An action can arrive part way through the shared step interval. It starts exactly at
			// its first pose, then uses the remaining part of that interval to reach the unchanged
			// first authoritative step. Subsequent intervals evaluate s+f without a buffered-step lag.
			float progress = (float)step + phase;
			if (step == m_anchorStep)
				progress = m_anchorProgress + ((float)step + 1.0f - m_anchorProgress)
					* (m_origin < 1.0f ? clamp((phase - m_origin) / (1.0f - m_origin)) : 0.0f);
			if (progress > (float)count) progress = (float)count;
			if (progress > m_progress) m_progress = progress;
		}
		m_frozen = frozen;
		return m_progress;
	}

	template<class Ease>
	static float value(float from, float to, int count, float progress, const Ease &ease)
	{
		if (progress <= 0.0f) return from;
		if (progress >= (float)count) return to;
		return from + (to - from) * ease(progress / (float)count);
	}

	static float clamp(float phase) { return phase < 0.0f ? 0.0f : (phase > 1.0f ? 1.0f : phase); }
	static float picturePhase(float picture, unsigned steppedFrame, int framesPerRetailFrame)
	{
		return steppedFrame == ~0u ? 0.0f : clamp(picture / (float)framesPerRetailFrame - (float)steppedFrame);
	}

private:
	bool m_first;
	bool m_frozen;
	int m_anchorStep;
	float m_origin, m_anchorProgress, m_progress;
};
