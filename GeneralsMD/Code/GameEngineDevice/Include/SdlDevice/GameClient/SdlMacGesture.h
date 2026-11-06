// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#pragma once

#include <stdint.h>

struct SDL_Window;

enum class SdlGesturePhase { None, Begin, Update, End, Cancel };

// Window points and content movement: positive X is right, positive Y is down.
// Cocoa's scrollingDelta values already reflect the user's natural-scroll setting.
struct SdlMacScroll
{
	uint32_t windowID;
	float x, y, deltaX, deltaY;
	SdlGesturePhase phase, momentum;
	bool focused, inside, buttonsDown;
};

// Pin the original target across contact and its following momentum. A world gesture that
// becomes ineligible is swallowed until a new BEGIN, rather than turning into a wheel zoom.
class SdlScrollGestureTarget
{
public:
	enum class Decision { PassThrough, Apply, Consume };
	Decision advance(SdlGesturePhase phase, SdlGesturePhase momentum, bool eligible, bool moves) noexcept
	{
		if (phase == SdlGesturePhase::None && momentum == SdlGesturePhase::None)
			return Decision::PassThrough; // Precise wheels without a gesture lifecycle stay wheels.
		if (phase == SdlGesturePhase::Begin) {
			owner = eligible ? World : Interface;
			awaitingMomentum = false;
		} else if (owner == None) {
			return Decision::PassThrough; // Do not acquire half of a gesture.
		} else if (momentum == SdlGesturePhase::Begin) {
			awaitingMomentum = false;
		} else if (awaitingMomentum && phase == SdlGesturePhase::Update) {
			cancel(); // An update after contact ended needs a new BEGIN or momentum phase.
		}
		if (owner == World && (!eligible || phase == SdlGesturePhase::Cancel
			|| momentum == SdlGesturePhase::Cancel))
			owner = Cancelled;
		const Decision result = owner == Interface ? Decision::PassThrough
			: owner == World && moves ? Decision::Apply : Decision::Consume;
		if (phase == SdlGesturePhase::End || phase == SdlGesturePhase::Cancel)
			awaitingMomentum = true;
		if (momentum == SdlGesturePhase::End || momentum == SdlGesturePhase::Cancel)
			reset();
		return result;
	}
	void cancel() noexcept { if (owner == World) owner = Cancelled; }
	void reset() noexcept { owner = None; awaitingMomentum = false; }

private:
	enum Owner { None, Interface, World, Cancelled };
	Owner owner = None;
	bool awaitingMomentum = false;
};

// Bounded payload storage for ordered SDL user events. Codes carry a slot generation instead
// of an owning pointer, so flushed/stale events cannot refer to freed or reused payloads.
class SdlScrollGestureQueue
{
public:
	static constexpr unsigned Capacity = 256;
	struct Payload { SdlMacScroll scroll; uint64_t sequence; };
	bool push(const SdlMacScroll &scroll, uint64_t sequence, uint32_t &code) noexcept
	{
		for (unsigned checked = 0; checked < Capacity; ++checked) {
			const unsigned index = next;
			next = (next + 1) % Capacity;
			Slot &slot = slots[index];
			if (slot.used) continue;
			slot.payload = { scroll, sequence };
			slot.code = (generation << 8) | index;
			slot.used = true;
			generation = generation == 0x7FFFFFu ? 1 : generation + 1;
			code = slot.code;
			return true;
		}
		return false;
	}
	bool pop(uint32_t code, Payload &payload) noexcept
	{
		Slot &slot = slots[code & (Capacity - 1)];
		if (!slot.used || slot.code != code) return false;
		payload = slot.payload;
		slot.used = false;
		return true;
	}
	void clear() noexcept { for (Slot &slot : slots) slot.used = false; }

private:
	struct Slot { Payload payload; uint32_t code; bool used; };
	Slot slots[Capacity] = {};
	unsigned next = 0;
	uint32_t generation = 1;
};

struct SdlResolvedCameraGesture
{
	uint32_t windowID;
	int32_t x, y;
	float panX, panY, scale;
	bool pan;
};

// Game-pixel actions wait here until keyboard, mouse and UI messages have been translated.
// Refusal or overflow discards the remainder, never replaying it in a later UI/game context.
class SdlPendingCameraGestures
{
public:
	static constexpr unsigned Capacity = 256;
	bool push(const SdlResolvedCameraGesture &gesture) noexcept
	{
		if (count == Capacity) { clear(); return false; }
		gestures[(head + count) % Capacity] = gesture;
		++count;
		return true;
	}
	template<class Apply> bool drain(Apply apply)
	{
		while (count) {
			const SdlResolvedCameraGesture gesture = gestures[head];
			head = (head + 1) % Capacity;
			--count;
			if (!apply(gesture)) { clear(); return false; }
		}
		return true;
	}
	void clear() noexcept { head = count = 0; }

private:
	SdlResolvedCameraGesture gestures[Capacity] = {};
	unsigned head = 0, count = 0;
};

// Main-thread-only local monitor for the borrowed SDL window. The handler queues accepted
// gestures; it must not apply camera movement from inside AppKit's event pump.
using SdlMacScrollHandler = bool (*)(const SdlMacScroll &) noexcept;
bool SdlMacGesture_install(SDL_Window *window, SdlMacScrollHandler handler) noexcept;
void SdlMacGesture_remove() noexcept;
bool SdlMacGesture_buttonsDown() noexcept;
