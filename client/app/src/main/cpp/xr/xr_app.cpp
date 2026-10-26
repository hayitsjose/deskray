#include "xr/xr_app.h"

#include <android/native_window_jni.h>

#include <android_native_app_glue.h>

#include <cstring>
#include <string>

#include "stream/video_decoder.h"

namespace {

// Test panel: 16:9 at 1.6 m wide, 1.4 m away, slightly below eye level.
constexpr int32_t kPanelW = 1920;
constexpr int32_t kPanelH = 1080;
constexpr float kPanelWidthM = 1.6f;
constexpr float kPanelRadiusM = 1.4f;
constexpr float kPanelDropM = 0.1f;
// Video surface size; matches the stream resolution DevSession requests.
constexpr int32_t kVideoW = 1920;
constexpr int32_t kVideoH = 1080;

// Scissor-clear one rectangle. Rect origin is bottom-left (GL convention,
// which is also how OpenXR presents GLES swapchain images).
void fillRect(int x, int y, int w, int h, float r, float g, float b) {
  glScissor(x, y, w, h);
  glClearColor(r, g, b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

}  // namespace

bool XrApp::init(android_app* app) {
  vm_ = app->activity->vm;
  if (!createInstance(app)) return false;
  if (!egl_.init()) return false;
  if (!createSession()) return false;
  if (!createPanelSwapchain()) return false;
  if (hasSurfaceSwapchain_ && !createVideoSwapchain()) {
    LOGW("no video surface; streams will not be shown");
    hasSurfaceSwapchain_ = false;
  }
  if (!controllers_.init(instance_, session_)) LOGW("controller input unavailable");
  if (hasPassthrough_ && !createPassthrough()) {
    LOGW("passthrough unavailable; continuing with a black background");
    hasPassthrough_ = false;
  }
  return true;
}

bool XrApp::createInstance(android_app* app) {
  PFN_xrInitializeLoaderKHR initLoader = nullptr;
  XR_PROC(XR_NULL_HANDLE, "xrInitializeLoaderKHR", initLoader);
  XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
  loaderInfo.applicationVM = app->activity->vm;
  loaderInfo.applicationContext = app->activity->clazz;
  XR_CHECK(initLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)));

  uint32_t count = 0;
  XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr));
  std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
  XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data()));
  auto has = [&](const char* name) {
    for (const auto& p : props)
      if (std::strcmp(p.extensionName, name) == 0) return true;
    return false;
  };

  std::vector<const char*> exts = {
      XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
      XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
  };
  for (const char* required : exts) {
    if (!has(required)) {
      LOGE("runtime lacks required extension %s", required);
      return false;
    }
  }
  hasCylinder_ = has(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
  hasPassthrough_ = has(XR_FB_PASSTHROUGH_EXTENSION_NAME);
  hasRefreshRate_ = has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
  hasSurfaceSwapchain_ = has(XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
  hasImageLayout_ = has(XR_FB_COMPOSITION_LAYER_IMAGE_LAYOUT_EXTENSION_NAME);
  if (hasCylinder_) exts.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
  if (hasPassthrough_) exts.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
  if (hasRefreshRate_) exts.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
  if (hasSurfaceSwapchain_) exts.push_back(XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
  if (hasImageLayout_) exts.push_back(XR_FB_COMPOSITION_LAYER_IMAGE_LAYOUT_EXTENSION_NAME);
  LOGI("extensions: cylinder=%d passthrough=%d refresh_rate=%d android_surface_swapchain=%d image_layout=%d",
       hasCylinder_, hasPassthrough_, hasRefreshRate_, hasSurfaceSwapchain_, hasImageLayout_);

  XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
  androidInfo.applicationVM = app->activity->vm;
  androidInfo.applicationActivity = app->activity->clazz;

  XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
  ci.next = &androidInfo;
  std::strncpy(ci.applicationInfo.applicationName, "deskray", XR_MAX_APPLICATION_NAME_SIZE - 1);
  std::strncpy(ci.applicationInfo.engineName, "deskray", XR_MAX_ENGINE_NAME_SIZE - 1);
  ci.applicationInfo.applicationVersion = 1;
  ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
  ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
  ci.enabledExtensionNames = exts.data();
  XR_CHECK(xrCreateInstance(&ci, &instance_));

  XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
  XR_CHECK(xrGetInstanceProperties(instance_, &ip));
  LOGI("runtime: %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
       XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

  XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
  sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  XR_CHECK(xrGetSystem(instance_, &sgi, &systemId_));
  return true;
}

bool XrApp::createSession() {
  // Required before xrCreateSession, even though we don't act on the answer:
  // Quest's runtime supports any GLES 3.x context.
  PFN_xrGetOpenGLESGraphicsRequirementsKHR getReqs = nullptr;
  XR_PROC(instance_, "xrGetOpenGLESGraphicsRequirementsKHR", getReqs);
  XrGraphicsRequirementsOpenGLESKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
  XR_CHECK(getReqs(instance_, systemId_, &reqs));

  XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
  binding.display = egl_.display;
  binding.config = egl_.config;
  binding.context = egl_.context;

  XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
  sci.next = &binding;
  sci.systemId = systemId_;
  XR_CHECK(xrCreateSession(instance_, &sci, &session_));

  // LOCAL = seated, origin at the head pose when the app started; holding
  // the Meta button recenters it.
  XrReferenceSpaceCreateInfo rsi{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  rsi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  rsi.poseInReferenceSpace.orientation.w = 1.0f;
  XR_CHECK(xrCreateReferenceSpace(session_, &rsi, &localSpace_));
  return true;
}

bool XrApp::createPanelSwapchain() {
  uint32_t count = 0;
  XR_CHECK(xrEnumerateSwapchainFormats(session_, 0, &count, nullptr));
  std::vector<int64_t> formats(count);
  XR_CHECK(xrEnumerateSwapchainFormats(session_, count, &count, formats.data()));
  int64_t format = 0;
  for (int64_t f : formats)
    if (f == GL_SRGB8_ALPHA8) format = f;
  if (format == 0)
    for (int64_t f : formats)
      if (f == GL_RGBA8) format = f;
  if (format == 0) {
    LOGE("no RGBA8 swapchain format offered");
    return false;
  }

  XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
  ci.format = format;
  ci.sampleCount = 1;
  ci.width = kPanelW;
  ci.height = kPanelH;
  ci.faceCount = 1;
  ci.arraySize = 1;
  ci.mipCount = 1;
  XR_CHECK(xrCreateSwapchain(session_, &ci, &panel_.handle));
  panel_.width = kPanelW;
  panel_.height = kPanelH;

  XR_CHECK(xrEnumerateSwapchainImages(panel_.handle, 0, &count, nullptr));
  std::vector<XrSwapchainImageOpenGLESKHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
  XR_CHECK(xrEnumerateSwapchainImages(panel_.handle, count, &count,
                                      reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));

  panel_.framebuffers.resize(count);
  glGenFramebuffers(static_cast<GLsizei>(count), panel_.framebuffers.data());
  for (uint32_t i = 0; i < count; ++i) {
    glBindFramebuffer(GL_FRAMEBUFFER, panel_.framebuffers[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[i].image, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      LOGE("panel framebuffer %u incomplete", i);
      return false;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  LOGI("panel swapchain %dx%d, %u images, format 0x%llx", kPanelW, kPanelH, count,
       static_cast<unsigned long long>(format));
  return true;
}

bool XrApp::createVideoSwapchain() {
  PFN_xrCreateSwapchainAndroidSurfaceKHR createSurface = nullptr;
  XR_PROC(instance_, "xrCreateSwapchainAndroidSurfaceKHR", createSurface);

  // The spec requires format, sample/face/array/mip counts to be zero here:
  // the producer (MediaCodec) decides the buffer format and may resize.
  XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  ci.width = kVideoW;
  ci.height = kVideoH;
  jobject surface = nullptr;
  XR_CHECK(createSurface(session_, &ci, &video_.handle, &surface));
  video_.width = kVideoW;
  video_.height = kVideoH;

  JNIEnv* env = nullptr;
  if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !surface) {
    LOGE("video surface: no JNI env or surface");
    return false;
  }
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (!window) {
    LOGE("video surface: ANativeWindow_fromSurface failed");
    return false;
  }
  VideoDecoder::instance().setWindow(window);  // takes its own reference
  ANativeWindow_release(window);
  LOGI("video surface swapchain %dx%d ready", kVideoW, kVideoH);
  return true;
}

bool XrApp::createPassthrough() {
  XR_PROC(instance_, "xrCreatePassthroughFB", xrCreatePassthroughFB_);
  XR_PROC(instance_, "xrDestroyPassthroughFB", xrDestroyPassthroughFB_);
  XR_PROC(instance_, "xrCreatePassthroughLayerFB", xrCreatePassthroughLayerFB_);
  XR_PROC(instance_, "xrDestroyPassthroughLayerFB", xrDestroyPassthroughLayerFB_);

  XrPassthroughCreateInfoFB pci{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
  pci.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
  XR_CHECK(xrCreatePassthroughFB_(session_, &pci, &passthrough_));

  XrPassthroughLayerCreateInfoFB lci{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
  lci.passthrough = passthrough_;
  lci.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
  lci.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
  XR_CHECK(xrCreatePassthroughLayerFB_(session_, &lci, &passthroughLayer_));
  LOGI("passthrough running");
  return true;
}

void XrApp::requestRefreshRate(float hz) {
  if (!hasRefreshRate_) return;
  PFN_xrEnumerateDisplayRefreshRatesFB enumerate = nullptr;
  PFN_xrRequestDisplayRefreshRateFB request = nullptr;
  if (XR_FAILED(xrGetInstanceProcAddr(instance_, "xrEnumerateDisplayRefreshRatesFB",
                                      reinterpret_cast<PFN_xrVoidFunction*>(&enumerate))) ||
      XR_FAILED(xrGetInstanceProcAddr(instance_, "xrRequestDisplayRefreshRateFB",
                                      reinterpret_cast<PFN_xrVoidFunction*>(&request))))
    return;

  uint32_t count = 0;
  if (XR_FAILED(enumerate(session_, 0, &count, nullptr)) || count == 0) return;
  std::vector<float> rates(count);
  if (XR_FAILED(enumerate(session_, count, &count, rates.data()))) return;
  std::string offered;
  for (float r : rates) offered += std::to_string(static_cast<int>(r)) + " ";
  PFN_xrGetDisplayRefreshRateFB getRate = nullptr;
  float current = 0.0f;
  if (XR_SUCCEEDED(xrGetInstanceProcAddr(instance_, "xrGetDisplayRefreshRateFB",
                                         reinterpret_cast<PFN_xrVoidFunction*>(&getRate))))
    getRate(session_, &current);
  LOGI("display offers %sHz, currently %.0f Hz", offered.c_str(), current);

  for (float r : rates) {
    if (r == hz) {
      XrResult res = request(session_, hz);
      LOGI("requested %.0f Hz: %d", hz, res);
      return;
    }
  }
  LOGW("%.0f Hz not offered by the display", hz);
}

void XrApp::pollEvents(bool* exitRequested) {
  XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
  while (xrPollEvent(instance_, &event) == XR_SUCCESS) {
    switch (event.type) {
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
        LOGW("instance loss pending");
        *exitRequested = true;
        break;
      case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
        const auto* e = reinterpret_cast<XrEventDataDisplayRefreshRateChangedFB*>(&event);
        LOGI("display refresh %.0f -> %.0f Hz", e->fromDisplayRefreshRate, e->toDisplayRefreshRate);
        break;
      }
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
        handleSessionState(reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state,
                           exitRequested);
        break;
      default:
        break;
    }
    event = {XR_TYPE_EVENT_DATA_BUFFER};
  }
}

void XrApp::handleSessionState(XrSessionState state, bool* exitRequested) {
  LOGI("session state %d -> %d", state_, state);
  state_ = state;
  // Surface swapchains may only receive frames while VISIBLE/FOCUSED, and must
  // be idle before xrEndSession; closing the gate waits for in-flight writes.
  VideoDecoder::instance().setRenderAllowed(state == XR_SESSION_STATE_VISIBLE ||
                                            state == XR_SESSION_STATE_FOCUSED);
  switch (state) {
    case XR_SESSION_STATE_READY: {
      XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
      bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
      if (XR_SUCCEEDED(xrBeginSession(session_, &bi))) {
        sessionRunning_ = true;
      } else {
        LOGE("xrBeginSession failed");
      }
      break;
    }
    case XR_SESSION_STATE_FOCUSED:
      // Quest accepts a refresh request earlier but only applies it reliably
      // once the app has focus; re-sending on every focus also covers
      // returning from the system menu.
      requestRefreshRate(90.0f);
      break;
    case XR_SESSION_STATE_STOPPING:
      xrEndSession(session_);
      sessionRunning_ = false;
      break;
    case XR_SESSION_STATE_EXITING:
    case XR_SESSION_STATE_LOSS_PENDING:
      *exitRequested = true;
      break;
    default:
      break;
  }
}

void XrApp::drawPanel() {
  uint32_t index = 0;
  XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (XR_FAILED(xrAcquireSwapchainImage(panel_.handle, &ai, &index))) return;
  XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wi.timeout = XR_INFINITE_DURATION;
  if (XR_FAILED(xrWaitSwapchainImage(panel_.handle, &wi))) return;

  const int w = panel_.width, h = panel_.height;
  glBindFramebuffer(GL_FRAMEBUFFER, panel_.framebuffers[index]);
  glViewport(0, 0, w, h);
  glEnable(GL_SCISSOR_TEST);

  // Grey body, a title bar, a grid to show the curvature, and coloured corner
  // markers to confirm orientation (red top-left, green bottom-right).
  fillRect(0, 0, w, h, 0.22f, 0.22f, 0.24f);
  fillRect(0, h - 72, w, 72, 0.16f, 0.36f, 0.70f);
  for (int x = w / 8; x < w; x += w / 8) fillRect(x - 1, 0, 2, h - 72, 0.40f, 0.40f, 0.43f);
  for (int y = h / 6; y < h - 72; y += h / 6) fillRect(0, y - 1, w, 2, 0.40f, 0.40f, 0.43f);
  fillRect(24, h - 72 - 24 - 96, 96, 96, 0.85f, 0.20f, 0.20f);
  fillRect(w - 24 - 96, 24, 96, 96, 0.20f, 0.75f, 0.30f);

  glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xrReleaseSwapchainImage(panel_.handle, &ri);
}

void XrApp::renderFrame() {
  XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
  XrFrameState fs{XR_TYPE_FRAME_STATE};
  if (XR_FAILED(xrWaitFrame(session_, &wi, &fs))) return;
  XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
  if (XR_FAILED(xrBeginFrame(session_, &bi))) return;

  XrCompositionLayerPassthroughFB ptLayer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
  XrCompositionLayerCylinderKHR cylLayer{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
  XrCompositionLayerQuad quadLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
  // MediaCodec fills the surface top row first; the compositor reads images
  // bottom row first (GL convention), so video needs a vertical flip.
  XrCompositionLayerImageLayoutFB flipY{XR_TYPE_COMPOSITION_LAYER_IMAGE_LAYOUT_FB};
  flipY.flags = XR_COMPOSITION_LAYER_IMAGE_LAYOUT_VERTICAL_FLIP_BIT_FB;
  const XrCompositionLayerBaseHeader* layers[2];
  uint32_t layerCount = 0;

  if (fs.shouldRender && state_ == XR_SESSION_STATE_FOCUSED && hasCylinder_) {
    const ScreenGeometry screen{{0.0f, -kPanelDropM, 0.0f}, kPanelRadiusM, kPanelWidthM / kPanelRadiusM,
                                static_cast<float>(kVideoW) / kVideoH};
    controllers_.update(localSpace_, fs.predictedDisplayTime, screen, kVideoW, kVideoH);
  }

  if (fs.shouldRender) {
    // Show the stream once its first frame is on the surface; until then (and
    // between streams) the GL test panel stands in.
    const bool showVideo = hasSurfaceSwapchain_ && VideoDecoder::instance().hasFrame();
    if (!showVideo) drawPanel();
    const Swapchain& screen = showVideo ? video_ : panel_;
    const void* screenNext = (showVideo && hasImageLayout_) ? &flipY : nullptr;

    if (hasPassthrough_) {
      ptLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
      ptLayer.space = XR_NULL_HANDLE;
      ptLayer.layerHandle = passthroughLayer_;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&ptLayer);
    }

    const XrSwapchainSubImage sub{screen.handle, {{0, 0}, {screen.width, screen.height}}, 0};
    const float aspect = static_cast<float>(screen.width) / screen.height;
    if (hasCylinder_) {
      cylLayer.next = screenNext;
      cylLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
      cylLayer.space = localSpace_;
      cylLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
      cylLayer.subImage = sub;
      cylLayer.pose = {{0, 0, 0, 1}, {0.0f, -kPanelDropM, 0.0f}};  // axis through the head
      cylLayer.radius = kPanelRadiusM;
      cylLayer.centralAngle = kPanelWidthM / kPanelRadiusM;
      cylLayer.aspectRatio = aspect;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cylLayer);
    } else {
      quadLayer.next = screenNext;
      quadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
      quadLayer.space = localSpace_;
      quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
      quadLayer.subImage = sub;
      quadLayer.pose = {{0, 0, 0, 1}, {0.0f, -kPanelDropM, -kPanelRadiusM}};
      quadLayer.size = {kPanelWidthM, kPanelWidthM / aspect};
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quadLayer);
    }
  }

  XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
  ei.displayTime = fs.predictedDisplayTime;
  ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  ei.layerCount = layerCount;
  ei.layers = layers;
  xrEndFrame(session_, &ei);
}

void XrApp::shutdown() {
  controllers_.destroy();
  // Stop the decoder writing into the surface before the surface goes away.
  VideoDecoder::instance().setWindow(nullptr);
  if (video_.handle != XR_NULL_HANDLE) xrDestroySwapchain(video_.handle);
  video_ = {};
  if (passthroughLayer_ != XR_NULL_HANDLE) xrDestroyPassthroughLayerFB_(passthroughLayer_);
  if (passthrough_ != XR_NULL_HANDLE) xrDestroyPassthroughFB_(passthrough_);
  if (!panel_.framebuffers.empty())
    glDeleteFramebuffers(static_cast<GLsizei>(panel_.framebuffers.size()), panel_.framebuffers.data());
  if (panel_.handle != XR_NULL_HANDLE) xrDestroySwapchain(panel_.handle);
  if (localSpace_ != XR_NULL_HANDLE) xrDestroySpace(localSpace_);
  if (session_ != XR_NULL_HANDLE) xrDestroySession(session_);
  if (instance_ != XR_NULL_HANDLE) xrDestroyInstance(instance_);
  egl_.shutdown();
  passthroughLayer_ = XR_NULL_HANDLE;
  passthrough_ = XR_NULL_HANDLE;
  panel_ = {};
  localSpace_ = XR_NULL_HANDLE;
  session_ = XR_NULL_HANDLE;
  instance_ = XR_NULL_HANDLE;
}
