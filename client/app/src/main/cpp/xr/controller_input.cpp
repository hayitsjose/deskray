#include "xr/controller_input.h"

#include <Limelight.h>

#include <cmath>
#include <cstring>

#include "stream/mouse.h"
#include "stream/stream_state.h"

namespace {

constexpr float kTriggerPress = 0.6f;    // hysteresis: press above, release below
constexpr float kTriggerRelease = 0.4f;
constexpr float kStickDeadzone = 0.2f;
constexpr double kScrollNotchesPerSec = 24.0;  // at full deflection; squared curve below
constexpr float kTakeBackUv = 0.03f;           // controller move that reclaims the cursor
constexpr XrDuration kClickFreezeNs = 80'000'000;  // 80 ms

XrVector3f forward(const XrQuaternionf& q) {
  // Rotate (0, 0, -1) by q: the negated third column of the rotation matrix.
  return {-2.0f * (q.x * q.z + q.w * q.y), -2.0f * (q.y * q.z - q.w * q.x),
          -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))};
}

}  // namespace

bool ControllerInput::createAction(const char* name, XrActionType type, XrAction* out) {
  XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
  ci.actionType = type;
  std::strncpy(ci.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
  std::strncpy(ci.localizedActionName, name, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
  ci.countSubactionPaths = 2;
  ci.subactionPaths = handPath_;
  XR_CHECK(xrCreateAction(actionSet_, &ci, out));
  return true;
}

bool ControllerInput::init(XrInstance instance, XrSession session) {
  instance_ = instance;
  session_ = session;
  XR_CHECK(xrStringToPath(instance, "/user/hand/left", &handPath_[kLeft]));
  XR_CHECK(xrStringToPath(instance, "/user/hand/right", &handPath_[kRight]));

  XrActionSetCreateInfo sci{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::strcpy(sci.actionSetName, "desktop");
  std::strcpy(sci.localizedActionSetName, "Desktop");
  XR_CHECK(xrCreateActionSet(instance, &sci, &actionSet_));

  if (!createAction("aim", XR_ACTION_TYPE_POSE_INPUT, &aim_) ||
      !createAction("trigger", XR_ACTION_TYPE_FLOAT_INPUT, &trigger_) ||
      !createAction("primary", XR_ACTION_TYPE_BOOLEAN_INPUT, &primary_) ||
      !createAction("thumb_click", XR_ACTION_TYPE_BOOLEAN_INPUT, &thumbClick_) ||
      !createAction("thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, &thumbstick_) ||
      !createAction("haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, &haptic_))
    return false;

  struct Binding { XrAction action; const char* path; };
  const Binding bindings[] = {
      {aim_, "/user/hand/left/input/aim/pose"},
      {aim_, "/user/hand/right/input/aim/pose"},
      {trigger_, "/user/hand/left/input/trigger/value"},
      {trigger_, "/user/hand/right/input/trigger/value"},
      {primary_, "/user/hand/left/input/x/click"},
      {primary_, "/user/hand/right/input/a/click"},
      {thumbClick_, "/user/hand/left/input/thumbstick/click"},
      {thumbClick_, "/user/hand/right/input/thumbstick/click"},
      {thumbstick_, "/user/hand/left/input/thumbstick"},
      {thumbstick_, "/user/hand/right/input/thumbstick"},
      {haptic_, "/user/hand/left/output/haptic"},
      {haptic_, "/user/hand/right/output/haptic"},
  };
  XrActionSuggestedBinding suggested[sizeof(bindings) / sizeof(bindings[0])];
  for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
    suggested[i].action = bindings[i].action;
    XR_CHECK(xrStringToPath(instance, bindings[i].path, &suggested[i].binding));
  }
  XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  XR_CHECK(xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &sb.interactionProfile));
  sb.countSuggestedBindings = sizeof(suggested) / sizeof(suggested[0]);
  sb.suggestedBindings = suggested;
  XR_CHECK(xrSuggestInteractionProfileBindings(instance, &sb));

  for (int h = 0; h < 2; ++h) {
    XrActionSpaceCreateInfo asi{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    asi.action = aim_;
    asi.subactionPath = handPath_[h];
    asi.poseInActionSpace.orientation.w = 1.0f;
    XR_CHECK(xrCreateActionSpace(session, &asi, &hands_[h].aimSpace));
  }

  XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  ai.countActionSets = 1;
  ai.actionSets = &actionSet_;
  XR_CHECK(xrAttachSessionActionSets(session, &ai));
  LOGI("controller input ready");
  return true;
}

void ControllerInput::destroy() {
  for (auto& h : hands_)
    if (h.aimSpace != XR_NULL_HANDLE) xrDestroySpace(h.aimSpace);
  if (actionSet_ != XR_NULL_HANDLE) xrDestroyActionSet(actionSet_);  // destroys its actions
  *this = ControllerInput{};
}

bool ControllerInput::hitScreen(const XrPosef& aim, const ScreenGeometry& s, float* u, float* v) const {
  // Ray origin relative to the cylinder axis; intersect with x^2 + z^2 = r^2.
  const float ox = aim.position.x - s.center.x;
  const float oy = aim.position.y - s.center.y;
  const float oz = aim.position.z - s.center.z;
  const XrVector3f d = forward(aim.orientation);
  const float a = d.x * d.x + d.z * d.z;
  if (a < 1e-6f) return false;  // pointing straight up or down
  const float b = 2.0f * (ox * d.x + oz * d.z);
  const float c = ox * ox + oz * oz - s.radius * s.radius;
  const float disc = b * b - 4.0f * a * c;
  if (disc < 0.0f) return false;
  const float t = (-b + std::sqrt(disc)) / (2.0f * a);  // far root: we're inside the cylinder
  if (t <= 0.0f) return false;

  const float hx = ox + t * d.x, hy = oy + t * d.y, hz = oz + t * d.z;
  const float theta = std::atan2(hx, -hz);  // 0 straight ahead (-Z), positive to the right
  const float height = s.radius * s.centralAngle / s.aspectRatio;
  *u = theta / s.centralAngle + 0.5f;
  *v = 0.5f - hy / height;
  return *u >= 0.0f && *u <= 1.0f && *v >= 0.0f && *v <= 1.0f;
}

void ControllerInput::pulse(Hand hand) {
  XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
  hi.action = haptic_;
  hi.subactionPath = handPath_[hand];
  XrHapticVibration vib{XR_TYPE_HAPTIC_VIBRATION};
  vib.duration = 10'000'000;  // 10 ms
  vib.frequency = XR_FREQUENCY_UNSPECIFIED;
  vib.amplitude = 0.5f;
  xrApplyHapticFeedback(session_, &hi, reinterpret_cast<const XrHapticBaseHeader*>(&vib));
}

void ControllerInput::sendButton(bool down, int button, XrTime time) {
  LiSendMouseButtonEvent(down ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE, button);
  ++buttonsSent_;
  freezeUntil_ = time + kClickFreezeNs;
  pulse(active_);
}

void ControllerInput::update(XrSpace base, XrTime time, const ScreenGeometry& screen, int streamW, int streamH) {
  if (actionSet_ == XR_NULL_HANDLE) return;
  const XrActiveActionSet active{actionSet_, XR_NULL_PATH};
  XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
  si.countActiveActionSets = 1;
  si.activeActionSets = &active;
  if (XR_FAILED(xrSyncActions(session_, &si))) return;

  const double dt = lastTime_ ? (time - lastTime_) / 1e9 : 0.0;
  lastTime_ = time;
  const bool connected = streamConnected();

  // Triggers first: pulling one selects that hand.
  bool triggerNow[2];
  for (int h = 0; h < 2; ++h) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = trigger_;
    gi.subactionPath = handPath_[h];
    XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
    xrGetActionStateFloat(session_, &gi, &st);
    const bool wasDown = hands_[h].triggerDown;
    triggerNow[h] = st.isActive && (wasDown ? st.currentState > kTriggerRelease : st.currentState > kTriggerPress);
    if (triggerNow[h] && !wasDown && h != active_) {
      if (hands_[active_].triggerDown && connected) sendButton(false, BUTTON_LEFT, time);
      hands_[active_].triggerDown = false;
      active_ = static_cast<Hand>(h);
      filterU_.reset();
      filterV_.reset();
    }
  }
  HandState& hand = hands_[active_];

  // Pointer.
  XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
  const bool tracked = XR_SUCCEEDED(xrLocateSpace(hand.aimSpace, base, time, &loc)) &&
                       (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                       (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
  float u = 0, v = 0;
  const bool onScreen = tracked && hitScreen(loc.pose, screen, &u, &v);
  const uint64_t mouseGen = physicalMouseGeneration();
  if (mouseGen != lastMouseGen_) {
    lastMouseGen_ = mouseGen;
    mouseOwns_ = true;
    anchorU_ = u;
    anchorV_ = v;
  } else if (mouseOwns_ && onScreen &&
             (std::fabs(u - anchorU_) > kTakeBackUv || std::fabs(v - anchorV_) > kTakeBackUv ||
              (triggerNow[active_] && !hand.triggerDown))) {
    mouseOwns_ = false;
    filterU_.reset();
    filterV_.reset();
  }
  if (time >= nextStatsLog_) {
    LOGI("input: %s hand, tracked=%d on_screen=%d uv=(%.3f, %.3f) connected=%d sent pos=%d buttons=%d",
         active_ == kRight ? "right" : "left", tracked, onScreen, u, v, connected, positionsSent_, buttonsSent_);
    nextStatsLog_ = time + 2'000'000'000;
  }
  if (onScreen && !mouseOwns_) {
    const double fu = filterU_.filter(u, dt), fv = filterV_.filter(v, dt);
    const int x = static_cast<int>(fu * (streamW - 1) + 0.5);
    const int y = static_cast<int>(fv * (streamH - 1) + 0.5);
    if (connected && time >= freezeUntil_ && (x != lastX_ || y != lastY_)) {
      LiSendMousePositionEvent(static_cast<short>(x), static_cast<short>(y),
                               static_cast<short>(streamW), static_cast<short>(streamH));
      lastX_ = x;
      lastY_ = y;
      ++positionsSent_;
    }
  }

  if (!connected) {
    for (auto& h : hands_) h.triggerDown = h.primaryDown = h.thumbClickDown = false;
    return;
  }

  // Buttons on the active hand.
  if (triggerNow[active_] != hand.triggerDown) {
    hand.triggerDown = triggerNow[active_];
    sendButton(hand.triggerDown, BUTTON_LEFT, time);
  }
  auto readBool = [&](XrAction action) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = action;
    gi.subactionPath = handPath_[active_];
    XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
    xrGetActionStateBoolean(session_, &gi, &st);
    return st.isActive && st.currentState;
  };
  const bool primary = readBool(primary_);
  if (primary != hand.primaryDown) {
    hand.primaryDown = primary;
    sendButton(primary, BUTTON_RIGHT, time);
  }
  const bool thumbClick = readBool(thumbClick_);
  if (thumbClick != hand.thumbClickDown) {
    hand.thumbClickDown = thumbClick;
    sendButton(thumbClick, BUTTON_MIDDLE, time);
  }

  // Wheel: deflection sets the speed; whole 1/120 steps go out each frame.
  XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
  gi.action = thumbstick_;
  gi.subactionPath = handPath_[active_];
  XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
  xrGetActionStateVector2f(session_, &gi, &stick);
  // Squared response: fine control near the centre, full speed at the edge.
  auto axis = [](float value) {
    if (std::fabs(value) < kStickDeadzone) return 0.0;
    const double m = (std::fabs(value) - kStickDeadzone) / (1.0f - kStickDeadzone);
    return m * m * (value > 0 ? 1 : -1);
  };
  if (stick.isActive && dt > 0) {
    scrollAccum_ += axis(stick.currentState.y) * 120.0 * kScrollNotchesPerSec * dt;
    hscrollAccum_ += axis(stick.currentState.x) * 120.0 * kScrollNotchesPerSec * dt;
    const int s = static_cast<int>(scrollAccum_), hs = static_cast<int>(hscrollAccum_);
    if (s != 0) { LiSendHighResScrollEvent(static_cast<short>(s)); scrollAccum_ -= s; }
    if (hs != 0) { LiSendHighResHScrollEvent(static_cast<short>(hs)); hscrollAccum_ -= hs; }
  }
}
