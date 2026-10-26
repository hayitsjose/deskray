#include "stream/audio_renderer.h"

#include <opus_multistream.h>

#include <algorithm>
#include <cstring>
#include <thread>

#include "util/log.h"

AudioRenderer& AudioRenderer::instance() {
  static AudioRenderer renderer;
  return renderer;
}

int AudioRenderer::init(const OPUS_MULTISTREAM_CONFIGURATION& c) {
  cleanup();
  channels_ = c.channelCount;
  sampleRate_ = c.sampleRate;
  samplesPerFrame_ = c.samplesPerFrame;

  int err = 0;
  decoder_ = opus_multistream_decoder_create(c.sampleRate, c.channelCount, c.streams, c.coupledStreams,
                                             c.mapping, &err);
  if (!decoder_) {
    LOGE("audio: opus decoder create failed (%d)", err);
    return -1;
  }
  pcm_.assign(static_cast<size_t>(samplesPerFrame_) * channels_, 0);
  // Far deeper than kMaxTargetMs, so the producer never laps the consumer.
  ring_.assign(static_cast<size_t>(sampleRate_) * channels_ / 4, 0);  // 250 ms
  writePos_ = 0;
  readPos_ = 0;
  primed_ = false;
  framesSinceUnderrun_ = 0;
  targetMs_ = kStartTargetMs;
  underruns_ = skips_ = packets_ = concealed_ = 0;

  std::lock_guard<std::mutex> lock(streamMutex_);
  if (!openStream()) return -1;
  LOGI("audio: %d Hz, %d ch, %d samples/frame, jitter target %d ms (adaptive %d-%d)", sampleRate_,
       channels_, samplesPerFrame_, kStartTargetMs, kMinTargetMs, kMaxTargetMs);
  return 0;
}

bool AudioRenderer::openStream() {
  AAudioStreamBuilder* b = nullptr;
  if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;
  AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_EXCLUSIVE);
  AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
  AAudioStreamBuilder_setChannelCount(b, channels_);
  AAudioStreamBuilder_setSampleRate(b, sampleRate_);
  AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
  AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MOVIE);
  AAudioStreamBuilder_setDataCallback(b, &AudioRenderer::onAudio, this);
  AAudioStreamBuilder_setErrorCallback(b, &AudioRenderer::onError, this);
  aaudio_result_t r = AAudioStreamBuilder_openStream(b, &stream_);
  AAudioStreamBuilder_delete(b);
  if (r != AAUDIO_OK) {
    LOGE("audio: open failed: %s", AAudio_convertResultToText(r));
    stream_ = nullptr;
    return false;
  }
  // Two bursts is the usual floor for glitch-free low-latency output.
  AAudioStream_setBufferSizeInFrames(stream_, AAudioStream_getFramesPerBurst(stream_) * 2);
  if ((r = AAudioStream_requestStart(stream_)) != AAUDIO_OK) {
    LOGE("audio: start failed: %s", AAudio_convertResultToText(r));
    AAudioStream_close(stream_);
    stream_ = nullptr;
    return false;
  }
  LOGI("audio: AAudio %s, %d Hz, burst %d frames, buffer %d frames",
       AAudioStream_getSharingMode(stream_) == AAUDIO_SHARING_MODE_EXCLUSIVE ? "exclusive" : "shared",
       AAudioStream_getSampleRate(stream_), AAudioStream_getFramesPerBurst(stream_),
       AAudioStream_getBufferSizeInFrames(stream_));
  return true;
}

void AudioRenderer::closeStream() {
  if (!stream_) return;
  AAudioStream_requestStop(stream_);
  AAudioStream_close(stream_);
  stream_ = nullptr;
}

void AudioRenderer::decodeAndPlay(const unsigned char* data, int length) {
  if (!decoder_) return;
  // data == NULL marks a lost packet: let Opus conceal the gap.
  const int frames = opus_multistream_decode(decoder_, data, data ? length : 0, pcm_.data(), samplesPerFrame_, 0);
  if (frames <= 0) {
    LOGW("audio: opus decode error %d", frames);
    return;
  }
  if (!data) ++concealed_;
  const size_t n = static_cast<size_t>(frames) * channels_;
  const size_t cap = ring_.size();
  if (cap - available() < n) {
    ++skips_;  // consumer stalled (stream reopening); drop rather than overwrite unread audio
    return;
  }
  size_t w = writePos_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < n; ++i) ring_[(w + i) % cap] = pcm_[i];
  writePos_.store(w + n, std::memory_order_release);

  const uint64_t p = ++packets_;
  if (p % 2000 == 0) {
    LOGI("audio: %llu packets, buffered %.1f ms (target %d), underruns %llu, skips %llu, concealed %llu",
         static_cast<unsigned long long>(p), available() * 1000.0 / channels_ / sampleRate_, targetMs_.load(),
         static_cast<unsigned long long>(underruns_.load()), static_cast<unsigned long long>(skips_.load()),
         static_cast<unsigned long long>(concealed_.load()));
  }
}

aaudio_data_callback_result_t AudioRenderer::onAudio(AAudioStream*, void* user, void* audio, int32_t frames) {
  auto* self = static_cast<AudioRenderer*>(user);
  auto* out = static_cast<int16_t*>(audio);
  const size_t ch = static_cast<size_t>(self->channels_);
  const size_t want = static_cast<size_t>(frames) * ch;
  const size_t perMs = static_cast<size_t>(self->sampleRate_) * ch / 1000;
  const size_t cap = self->ring_.size();

  size_t r = self->readPos_.load(std::memory_order_relaxed);
  size_t avail = self->writePos_.load(std::memory_order_acquire) - r;
  int target = self->targetMs_.load(std::memory_order_relaxed);

  // Re-prime after start or an underrun: wait for the target depth.
  if (!self->primed_) {
    if (avail < target * perMs) {
      std::memset(out, 0, want * sizeof(int16_t));
      return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    self->primed_ = true;
  }
  // Well past the target (a burst, or the consumer was stalled): jump back to
  // the target instead of playing stale audio.
  if (avail > (target + kSkipSlackMs) * perMs) {
    const size_t drop = (avail - target * perMs) / ch * ch;
    r += drop;
    avail -= drop;
    ++self->skips_;
  }

  const size_t n = std::min(want, avail);
  for (size_t i = 0; i < n; ++i) out[i] = self->ring_[(r + i) % cap];
  self->readPos_.store(r + n, std::memory_order_release);

  if (n < want) {
    // Underrun: the link is jitterier than the buffer. Deepen it.
    std::memset(out + n, 0, (want - n) * sizeof(int16_t));
    self->primed_ = false;
    self->framesSinceUnderrun_ = 0;
    self->targetMs_.store(std::min(target + kGrowMs, kMaxTargetMs), std::memory_order_relaxed);
    ++self->underruns_;
  } else if ((self->framesSinceUnderrun_ += frames) >= self->sampleRate_ / 1000 * kStablePeriodMs) {
    // A quiet stretch: try a little less latency.
    self->framesSinceUnderrun_ = 0;
    self->targetMs_.store(std::max(target - kShrinkMs, kMinTargetMs), std::memory_order_relaxed);
  }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AudioRenderer::onError(AAudioStream*, void* user, aaudio_result_t error) {
  auto* self = static_cast<AudioRenderer*>(user);
  LOGW("audio: stream error %s; reopening", AAudio_convertResultToText(error));
  // AAudio forbids closing a stream from its own callback.
  std::thread([self] {
    std::lock_guard<std::mutex> lock(self->streamMutex_);
    if (!self->stream_) return;  // cleaned up meanwhile
    self->closeStream();
    self->primed_ = false;
    self->openStream();
  }).detach();
}

void AudioRenderer::cleanup() {
  {
    std::lock_guard<std::mutex> lock(streamMutex_);
    closeStream();
  }
  if (decoder_) {
    opus_multistream_decoder_destroy(decoder_);
    decoder_ = nullptr;
    LOGI("audio: stopped after %llu packets (underruns %llu, skips %llu, concealed %llu)",
         static_cast<unsigned long long>(packets_.load()), static_cast<unsigned long long>(underruns_.load()),
         static_cast<unsigned long long>(skips_.load()), static_cast<unsigned long long>(concealed_.load()));
  }
}
