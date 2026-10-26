#pragma once
#include <android/native_window.h>
#include <media/NdkMediaCodec.h>

#include <Limelight.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

// Hardware decoder writing straight into the OpenXR compositor's surface
// (XR_KHR_android_surface_swapchain): no copies, no GL work, the compositor
// samples whatever frame was released last.
//
// Threads: the XR thread owns the window and the render gate; moonlight-common-c's
// receive thread calls setup/submit/cleanup; one internal thread drains output.
class VideoDecoder {
 public:
  static VideoDecoder& instance();

  // XR thread. The decoder takes its own reference; pass nullptr to detach
  // (stops any running codec first).
  void setWindow(ANativeWindow* window);

  // XR thread. Only VISIBLE/FOCUSED sessions may receive frames, and nothing may
  // write once STOPPING arrives: closing the gate returns after any in-flight
  // release finishes, and later frames are decoded but dropped.
  void setRenderAllowed(bool allowed);

  // True once a frame has reached the surface since the last setup.
  bool hasFrame() const { return framesRendered_.load() > 0; }

  // moonlight-common-c callbacks.
  int setup(int videoFormat, int width, int height, int fps);
  int submit(PDECODE_UNIT du);
  void cleanup();

 private:
  void outputLoop();
  void stopCodecLocked();
  void recordSubmit(int64_t ptsUs);

  std::mutex windowMutex_;
  std::condition_variable windowCv_;
  ANativeWindow* window_ = nullptr;

  std::mutex renderMutex_;
  bool renderAllowed_ = false;

  std::mutex codecMutex_;
  std::atomic<AMediaCodec*> codec_{nullptr};  // read lock-free by submit()
  std::thread outputThread_;
  std::atomic<bool> running_{false};
  bool sentCodecConfig_ = false;

  std::atomic<uint64_t> framesRendered_{0};
  std::atomic<uint64_t> framesDropped_{0};

  // Decode-time measurement: submit time per presentation timestamp.
  static constexpr int kTimingSlots = 64;
  struct Timing { std::atomic<int64_t> pts{-1}; std::atomic<uint64_t> submitUs{0}; };
  Timing timings_[kTimingSlots];
  uint64_t decodeUsSum_ = 0;
  uint64_t decodeSamples_ = 0;
};
