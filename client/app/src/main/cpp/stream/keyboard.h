#pragma once
#include <android/input.h>

// Forwards a physical (Bluetooth) keyboard event to the host. Returns true when
// the key was sent, so the caller marks it handled; system keys (Back, volume)
// and anything unmapped fall through to Horizon OS.
bool forwardKeyEvent(const AInputEvent* event);

// Same, from Java KeyEvent fields (windowed mode); the action and meta-state
// values match AKEY_EVENT_ACTION_* and AMETA_*.
bool forwardKey(int32_t keyCode, int32_t action, int32_t metaState, int32_t repeatCount);
