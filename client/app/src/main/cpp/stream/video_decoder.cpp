#include "stream/video_decoder.h"

#include <media/NdkMediaFormat.h>

#include <chrono>
#include <cstring>
#include <vector>

#include "util/log.h"

namespace {

uint64_t nowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

const char* mimeFor(int videoFormat) {
  if (videoFormat & VIDEO_FORMAT_MASK_H265) return "video/hevc";
  if (videoFormat & VIDEO_FORMAT_MASK_AV1) return "video/av01";
  return "video/avc";
}

}  // namespace

VideoDecoder& VideoDecoder::instance() {
  static VideoDecoder decoder;
  return decoder;
}

void VideoDecoder::setWindow(ANativeWindow* window) {
  if (!window) {
    std::lock_guard<std::mutex> codecLock(codecMutex_);
    stopCodecLocked();
  }
  std::lock_guard<std::mutex> lock(windowMutex_);
  if (window_) ANativeWindow_release(window_);
  window_ = window;
  if (window_) ANativeWindow_acquire(window_);
  windowCv_.notify_all();
}

void VideoDecoder::setRenderAllowed(bool allowed) {
  std::lock_guard<std::mutex> lock(renderMutex_);
  renderAllowed_ = allowed;
}

int VideoDecoder::setup(int videoFormat, int width, int height, int fps) {
  std::lock_guard<std::mutex> codecLock(codecMutex_);
  stopCodecLocked();

  ANativeWindow* window = nullptr;
  {
    // The XR thread creates the surface right after the session; a stream can
    // only start later, but don't hang forever if XR init failed.
    std::unique_lock<std::mutex> lock(windowMutex_);
    if (!windowCv_.wait_for(lock, std::chrono::seconds(5), [this] { return window_ != nullptr; })) {
      LOGE("decoder: no video surface after 5 s");
      return -1;
    }
    window = window_;
    ANativeWindow_acquire(window);
  }

  const char* mime = mimeFor(videoFormat);
  codec_ = AMediaCodec_createDecoderByType(mime);
  if (!codec_) {
    LOGE("decoder: no hardware decoder for %s", mime);
    ANativeWindow_release(window);
    return -1;
  }

  AMediaFormat* format = AMediaFormat_new();
  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
  AMediaFormat_setInt32(format, "low-latency", 1);                           // Android 11+
  AMediaFormat_setInt32(format, "vendor.qti-ext-dec-low-latency.enable", 1);  // Qualcomm
  AMediaFormat_setInt32(format, "vendor.qti-ext-dec-picture-order.enable", 1);
  AMediaFormat_setInt32(format, "priority", 0);                              // realtime
  AMediaFormat_setInt32(format, "operating-rate", 32767);
  media_status_t st = AMediaCodec_configure(codec_, format, window, nullptr, 0);
  AMediaFormat_delete(format);
  ANativeWindow_release(window);  // the codec holds its own reference now
  if (st != AMEDIA_OK || AMediaCodec_start(codec_) != AMEDIA_OK) {
    LOGE("decoder: configure/start failed (%d)", st);
    AMediaCodec_delete(codec_);
    codec_ = nullptr;
    return -1;
  }

  sentCodecConfig_ = false;
  framesRendered_ = 0;
  framesDropped_ = 0;
  decodeUsSum_ = 0;
  decodeSamples_ = 0;
  for (auto& t : timings_) t.pts = -1;
  running_ = true;
  outputThread_ = std::thread(&VideoDecoder::outputLoop, this);

  char* name = nullptr;
  AMediaCodec_getName(codec_, &name);
  LOGI("decoder: %s %dx%d @ %d via %s", mime, width, height, fps, name ? name : "?");
  if (name) AMediaCodec_releaseName(codec_, name);
  return 0;
}

int VideoDecoder::submit(PDECODE_UNIT du) {
  AMediaCodec* codec = codec_;  // set before the stream starts, cleared after it stops
  if (!codec) return DR_NEED_IDR;

  // Parameter sets travel at the front of IDR frames. Hand the first set to the
  // codec as config; later ones stay in-band with their picture.
  if (du->frameType == FRAME_TYPE_IDR && !sentCodecConfig_) {
    std::vector<uint8_t> csd;
    for (PLENTRY e = du->bufferList; e; e = e->next)
      if (e->bufferType != BUFFER_TYPE_PICDATA) csd.insert(csd.end(), e->data, e->data + e->length);
    if (!csd.empty()) {
      ssize_t idx = AMediaCodec_dequeueInputBuffer(codec, 100000);
      if (idx < 0) return DR_NEED_IDR;
      size_t cap = 0;
      uint8_t* buf = AMediaCodec_getInputBuffer(codec, idx, &cap);
      if (!buf || csd.size() > cap) return DR_NEED_IDR;
      std::memcpy(buf, csd.data(), csd.size());
      AMediaCodec_queueInputBuffer(codec, idx, 0, csd.size(), 0, AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);
    }
    sentCodecConfig_ = true;
  }
  if (!sentCodecConfig_) return DR_NEED_IDR;  // can't decode P-frames before the first IDR

  ssize_t idx = AMediaCodec_dequeueInputBuffer(codec, 10000);
  if (idx < 0) {
    LOGW("decoder: no input buffer free; requesting IDR");
    return DR_NEED_IDR;
  }
  size_t cap = 0;
  uint8_t* buf = AMediaCodec_getInputBuffer(codec, idx, &cap);
  if (!buf || static_cast<size_t>(du->fullLength) > cap) {
    LOGE("decoder: frame of %d bytes exceeds input buffer (%zu)", du->fullLength, cap);
    AMediaCodec_queueInputBuffer(codec, idx, 0, 0, 0, 0);
    return DR_NEED_IDR;
  }
  size_t off = 0;
  for (PLENTRY e = du->bufferList; e; e = e->next) {
    std::memcpy(buf + off, e->data, e->length);
    off += e->length;
  }
  recordSubmit(static_cast<int64_t>(du->presentationTimeUs));
  AMediaCodec_queueInputBuffer(codec, idx, 0, off, du->presentationTimeUs, 0);
  return DR_OK;
}

void VideoDecoder::recordSubmit(int64_t ptsUs) {
  Timing& t = timings_[static_cast<uint64_t>(ptsUs) % kTimingSlots];
  t.submitUs = nowUs();
  t.pts = ptsUs;
}

void VideoDecoder::outputLoop() {
  AMediaCodecBufferInfo info;
  while (running_) {
    ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, 50000);
    if (idx >= 0) {
      Timing& t = timings_[static_cast<uint64_t>(info.presentationTimeUs) % kTimingSlots];
      if (t.pts.load() == info.presentationTimeUs) {
        decodeUsSum_ += nowUs() - t.submitUs.load();
        ++decodeSamples_;
      }
      bool rendered = false;
      {
        std::lock_guard<std::mutex> lock(renderMutex_);
        if (renderAllowed_) {
          AMediaCodec_releaseOutputBuffer(codec_, idx, true);  // straight to the compositor
          rendered = true;
        }
      }
      if (!rendered) {
        AMediaCodec_releaseOutputBuffer(codec_, idx, false);
        ++framesDropped_;
        continue;
      }
      const uint64_t n = ++framesRendered_;
      if (n == 1 || n % 300 == 0) {
        LOGI("decoder: %llu frames shown (%llu dropped), avg decode %.2f ms",
             static_cast<unsigned long long>(n), static_cast<unsigned long long>(framesDropped_.load()),
             decodeSamples_ ? decodeUsSum_ / 1000.0 / decodeSamples_ : 0.0);
        decodeUsSum_ = 0;
        decodeSamples_ = 0;
      }
    } else if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      AMediaFormat* f = AMediaCodec_getOutputFormat(codec_);
      LOGI("decoder: output format %s", AMediaFormat_toString(f));
      AMediaFormat_delete(f);
    } else if (idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER && idx != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      LOGE("decoder: dequeueOutputBuffer error %zd", idx);
      LiRequestIdrFrame();
    }
  }
}

void VideoDecoder::cleanup() {
  std::lock_guard<std::mutex> codecLock(codecMutex_);
  stopCodecLocked();
}

void VideoDecoder::stopCodecLocked() {
  if (!codec_) return;
  running_ = false;
  if (outputThread_.joinable()) outputThread_.join();
  AMediaCodec_stop(codec_);
  AMediaCodec_delete(codec_);
  codec_ = nullptr;
  LOGI("decoder: stopped after %llu frames", static_cast<unsigned long long>(framesRendered_.load()));
}
