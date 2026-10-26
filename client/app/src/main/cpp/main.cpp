#include <android_native_app_glue.h>

#include "stream/keyboard.h"
#include "stream/mouse.h"
#include "util/log.h"
#include "xr/xr_app.h"

namespace {

struct AppState {
  bool resumed = false;
};

void onAppCmd(android_app* app, int32_t cmd) {
  auto* state = static_cast<AppState*>(app->userData);
  switch (cmd) {
    case APP_CMD_RESUME: state->resumed = true; break;
    case APP_CMD_PAUSE: state->resumed = false; break;
    default: break;
  }
}

// Physical keyboard, mouse and touchpad input goes to the host while a stream runs.
int32_t onInputEvent(android_app*, AInputEvent* event) {
  switch (AInputEvent_getType(event)) {
    case AINPUT_EVENT_TYPE_KEY: return forwardKeyEvent(event) ? 1 : 0;
    case AINPUT_EVENT_TYPE_MOTION: return forwardMotionEvent(event) ? 1 : 0;
    default: return 0;
  }
}

}  // namespace

void android_main(android_app* app) {
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  AppState state;
  app->userData = &state;
  app->onAppCmd = onAppCmd;
  app->onInputEvent = onInputEvent;

  XrApp xr;
  bool alive = xr.init(app);
  if (!alive) {
    LOGE("OpenXR init failed; finishing");
    ANativeActivity_finish(app->activity);
  }

  bool exitRequested = false;
  while (!app->destroyRequested) {
    // Block on Android events when there's nothing to render; otherwise drain
    // them without waiting so the frame loop keeps xrWaitFrame's pacing.
    for (;;) {
      const bool idle = !alive || exitRequested || (!state.resumed && !xr.sessionRunning());
      int events = 0;
      android_poll_source* source = nullptr;
      if (ALooper_pollOnce(idle && !app->destroyRequested ? -1 : 0, nullptr, &events,
                           reinterpret_cast<void**>(&source)) < 0)
        break;
      if (source) source->process(app, source);
      if (app->destroyRequested) break;
    }
    if (!alive || exitRequested) continue;

    xr.pollEvents(&exitRequested);
    if (exitRequested) {
      LOGI("runtime requested exit");
      ANativeActivity_finish(app->activity);
      continue;
    }
    if (xr.sessionRunning()) xr.renderFrame();
  }

  xr.shutdown();
  app->activity->vm->DetachCurrentThread();
}
