#include "xr/egl.h"

bool Egl::init() {
  display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
    LOGE("eglInitialize failed: 0x%x", eglGetError());
    return false;
  }

  const EGLint cfgAttrs[] = {
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_SAMPLES, 0,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_NONE};
  EGLint count = 0;
  if (!eglChooseConfig(display, cfgAttrs, &config, 1, &count) || count == 0) {
    LOGE("eglChooseConfig found no RGBA8 ES3 config: 0x%x", eglGetError());
    return false;
  }

  const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttrs);
  if (context == EGL_NO_CONTEXT) {
    LOGE("eglCreateContext failed: 0x%x", eglGetError());
    return false;
  }

  const EGLint pbAttrs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  surface = eglCreatePbufferSurface(display, config, pbAttrs);
  if (surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context)) {
    LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
    return false;
  }

  LOGI("EGL ready: %s / %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));
  return true;
}

void Egl::shutdown() {
  if (display == EGL_NO_DISPLAY) return;
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
  if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
  eglTerminate(display);
  display = EGL_NO_DISPLAY;
  context = EGL_NO_CONTEXT;
  surface = EGL_NO_SURFACE;
}
