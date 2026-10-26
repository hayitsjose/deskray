#pragma once
#include <android/input.h>

#include <cstdint>

// Forwards a physical mouse or touchpad (e.g. a Bluetooth keyboard's built-in
// pad) to the host as relative motion, buttons and wheel. Returns true when
// the event was consumed.
bool forwardMotionEvent(const AInputEvent* event);

// Bumped on every physical mouse event, so the controller pointer can step
// aside instead of yanking the cursor back to the ray.
uint64_t physicalMouseGeneration();
