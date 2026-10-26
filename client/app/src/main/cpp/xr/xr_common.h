#pragma once
// Single include point for the platform + OpenXR headers, in the order
// openxr_platform.h needs them.
#include <jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "util/log.h"

// Log and bail out of a bool-returning function on any XrResult failure.
#define XR_CHECK(call)                                                        \
  do {                                                                        \
    XrResult r_ = (call);                                                     \
    if (XR_FAILED(r_)) {                                                      \
      LOGE("%s failed: %d (%s:%d)", #call, r_, __FILE__, __LINE__);           \
      return false;                                                           \
    }                                                                         \
  } while (0)

// Look up an extension function into a typed pointer.
#define XR_PROC(instance, name, fn) \
  XR_CHECK(xrGetInstanceProcAddr(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&fn)))
