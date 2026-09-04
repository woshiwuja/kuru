#pragma once
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <vector>

// Drains the SDL queue once per frame and keeps the result around, so plugins
// read input instead of competing with the window loop for the same events.
// Interpreting it is their job, not this one's.
struct EventManager {
	bool  quit        = false;
	float mouseDeltaX = 0.0f;
	float mouseDeltaY = 0.0f;
	float wheel       = 0.0f;
	bool  middleDown  = false;
	const bool *keys  = nullptr; // SDL's keyboard state, valid after pump()
	// This frame's raw events, kept because a UI backend wants every one of
	// them and the queue can only be drained once.
	std::vector<SDL_Event> events;

	void pump();
	[[nodiscard]] bool down(SDL_Scancode key) const;
};
