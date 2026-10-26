#pragma once
#include <vector>

#include "xr/egl.h"
#include "xr/xr_common.h"

struct android_app;

// Owns the OpenXR instance/session and the scene: passthrough plus one curved
// screen, showing the stream's surface swapchain when video is flowing and a
// GL test pattern otherwise.
class XrApp {
 public:
  bool init(android_app* app);
  void shutdown();

  // Drains the OpenXR event queue. Sets *exitRequested when the runtime wants
  // the app gone (session exiting, instance lost).
  void pollEvents(bool* exitRequested);
  bool sessionRunning() const { return sessionRunning_; }
  void renderFrame();

 private:
  struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<GLuint> framebuffers;  // one per swapchain image
  };

  bool createInstance(android_app* app);
  bool createSession();
  bool createPanelSwapchain();
  bool createVideoSwapchain();
  bool createPassthrough();
  void requestRefreshRate(float hz);
  void handleSessionState(XrSessionState state, bool* exitRequested);
  void drawPanel();

  Egl egl_;
  XrInstance instance_ = XR_NULL_HANDLE;
  XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
  XrSession session_ = XR_NULL_HANDLE;
  XrSpace localSpace_ = XR_NULL_HANDLE;
  XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
  bool sessionRunning_ = false;

  bool hasCylinder_ = false;
  bool hasPassthrough_ = false;
  bool hasRefreshRate_ = false;
  bool hasSurfaceSwapchain_ = false;
  bool hasImageLayout_ = false;

  Swapchain panel_;  // GL test pattern
  Swapchain video_;  // compositor-owned surface MediaCodec decodes into
  JavaVM* vm_ = nullptr;

  XrPassthroughFB passthrough_ = XR_NULL_HANDLE;
  XrPassthroughLayerFB passthroughLayer_ = XR_NULL_HANDLE;
  PFN_xrCreatePassthroughFB xrCreatePassthroughFB_ = nullptr;
  PFN_xrDestroyPassthroughFB xrDestroyPassthroughFB_ = nullptr;
  PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB_ = nullptr;
  PFN_xrDestroyPassthroughLayerFB xrDestroyPassthroughLayerFB_ = nullptr;
};
