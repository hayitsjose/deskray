#pragma once
#include "util/one_euro.h"
#include "xr/xr_common.h"

// Where the curved screen is, in the base space: a vertical cylinder through
// `center`, the image spanning `centralAngle` radians of arc.
struct ScreenGeometry {
  XrVector3f center;
  float radius;
  float centralAngle;
  float aspectRatio;  // width / height
};

// Touch controllers as a desktop mouse. The aim ray of the active hand is an
// absolute pointer on the screen; trigger = left button, A/X = right,
// thumbstick click = middle, thumbstick = wheel. Pulling either trigger makes
// that hand the active one.
class ControllerInput {
 public:
  bool init(XrInstance instance, XrSession session);
  void destroy();

  // Once per frame while the session is FOCUSED (actions only sync then).
  void update(XrSpace base, XrTime time, const ScreenGeometry& screen, int streamW, int streamH);

 private:
  enum Hand { kLeft = 0, kRight = 1 };

  struct HandState {
    XrSpace aimSpace = XR_NULL_HANDLE;
    bool triggerDown = false;
    bool primaryDown = false;
    bool thumbClickDown = false;
  };

  bool createAction(const char* name, XrActionType type, XrAction* out);
  bool hitScreen(const XrPosef& aim, const ScreenGeometry& s, float* u, float* v) const;
  void pulse(Hand hand);
  void sendButton(bool down, int button, XrTime time);

  XrInstance instance_ = XR_NULL_HANDLE;
  XrSession session_ = XR_NULL_HANDLE;
  XrActionSet actionSet_ = XR_NULL_HANDLE;
  XrAction aim_ = XR_NULL_HANDLE;
  XrAction trigger_ = XR_NULL_HANDLE;
  XrAction primary_ = XR_NULL_HANDLE;     // A (right) / X (left)
  XrAction thumbClick_ = XR_NULL_HANDLE;
  XrAction thumbstick_ = XR_NULL_HANDLE;
  XrAction haptic_ = XR_NULL_HANDLE;
  XrPath handPath_[2] = {XR_NULL_PATH, XR_NULL_PATH};
  HandState hands_[2];
  Hand active_ = kRight;

  OneEuroFilter filterU_{1.0, 20.0};
  OneEuroFilter filterV_{1.0, 20.0};
  XrTime lastTime_ = 0;
  XrTime freezeUntil_ = 0;  // hold the cursor still around clicks
  int lastX_ = -1, lastY_ = -1;
  double scrollAccum_ = 0.0, hscrollAccum_ = 0.0;

  // A physical mouse/touchpad owns the cursor until the controller is moved
  // deliberately (or its trigger pulled), so the ray doesn't yank it back.
  uint64_t lastMouseGen_ = 0;
  bool mouseOwns_ = false;
  float anchorU_ = 0.0f, anchorV_ = 0.0f;

  // Diagnostics, logged every 2 s.
  XrTime nextStatsLog_ = 0;
  int positionsSent_ = 0, buttonsSent_ = 0;
};
