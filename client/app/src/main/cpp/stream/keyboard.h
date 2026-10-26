#pragma once
#include <android/input.h>

// Forwards a physical (Bluetooth) keyboard event to the host. Returns true when
// the key was sent, so the caller marks it handled; system keys (Back, volume)
// and anything unmapped fall through to Horizon OS.
bool forwardKeyEvent(const AInputEvent* event);
