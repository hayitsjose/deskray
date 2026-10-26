#include "stream/mouse.h"

#include <Limelight.h>

#include <atomic>
#include <cmath>

#include "stream/stream_state.h"
#include "util/log.h"

namespace {

std::atomic<uint64_t> gGeneration{0};
int32_t gButtons = 0;            // input thread only
float gRemX = 0.0f, gRemY = 0.0f;  // sub-pixel motion carried between events
bool gLoggedSource = false;

void syncButtons(int32_t now) {
  struct Map { int32_t android; int moonlight; };
  static constexpr Map kMap[] = {
      {AMOTION_EVENT_BUTTON_PRIMARY, BUTTON_LEFT},
      {AMOTION_EVENT_BUTTON_SECONDARY, BUTTON_RIGHT},
      {AMOTION_EVENT_BUTTON_TERTIARY, BUTTON_MIDDLE},
      {AMOTION_EVENT_BUTTON_BACK, BUTTON_X1},
      {AMOTION_EVENT_BUTTON_FORWARD, BUTTON_X2},
  };
  for (const auto& m : kMap) {
    const bool was = gButtons & m.android, is = now & m.android;
    if (was != is) LiSendMouseButtonEvent(is ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE, m.moonlight);
  }
  gButtons = now;
}

}  // namespace

uint64_t physicalMouseGeneration() { return gGeneration.load(); }

bool forwardMotionEvent(const AInputEvent* event) {
  const int32_t source = AInputEvent_getSource(event);
  const bool relative = (source & AINPUT_SOURCE_MOUSE_RELATIVE) == AINPUT_SOURCE_MOUSE_RELATIVE;
  const bool mouse = (source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE;
  if (!relative && !mouse) return false;
  if (!gLoggedSource) {
    LOGI("physical pointer: source 0x%x (%s)", source, relative ? "captured/relative" : "mouse");
    gLoggedSource = true;
  }
  if (!streamConnected()) return false;
  ++gGeneration;

  const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
  if (action == AMOTION_EVENT_ACTION_SCROLL) {
    const float v = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
    const float h = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0);
    if (v != 0.0f) LiSendHighResScrollEvent(static_cast<short>(std::lround(v * 120.0f)));
    if (h != 0.0f) LiSendHighResHScrollEvent(static_cast<short>(std::lround(h * 120.0f)));
    return true;
  }

  // Captured pointers report deltas in X/Y; an ordinary mouse reports the
  // system cursor's position there and the raw deltas in AXIS_RELATIVE_*.
  float dx, dy;
  if (relative) {
    dx = AMotionEvent_getX(event, 0);
    dy = AMotionEvent_getY(event, 0);
  } else {
    dx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RELATIVE_X, 0);
    dy = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RELATIVE_Y, 0);
  }
  gRemX += dx;
  gRemY += dy;
  const int ix = static_cast<int>(gRemX), iy = static_cast<int>(gRemY);
  if (ix != 0 || iy != 0) {
    LiSendMouseMoveEvent(static_cast<short>(ix), static_cast<short>(iy));
    gRemX -= ix;
    gRemY -= iy;
  }
  syncButtons(AMotionEvent_getButtonState(event));
  return true;
}
