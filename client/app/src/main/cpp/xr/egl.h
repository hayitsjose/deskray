#pragma once
#include "xr/xr_common.h"

// Headless GLES 3 context: OpenXR swapchains are the only render targets, so a
// 16x16 pbuffer is enough to make the context current.
struct Egl {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLConfig config = nullptr;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;

  bool init();
  void shutdown();
};
