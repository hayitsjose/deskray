#pragma once
#include <aaudio/AAudio.h>

#include <Limelight.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

struct OpusMSDecoder;

// Opus -> lock-free ring -> AAudio (low latency). The network thread decodes
// and writes; AAudio's callback thread reads. The ring is an adaptive jitter
// buffer: each underrun raises the target depth, a quiet stretch lowers it
// again, and the callback skips ahead when the ring grows well past the target,
// so audio tracks the link's actual jitter without drifting behind video.
class AudioRenderer {
 public:
  static AudioRenderer& instance();

  // moonlight-common-c callbacks.
  int init(const OPUS_MULTISTREAM_CONFIGURATION& config);
  void decodeAndPlay(const unsigned char* data, int length);
  void cleanup();

 private:
  static constexpr int kStartTargetMs = 20;
  static constexpr int kMinTargetMs = 10;
  static constexpr int kMaxTargetMs = 80;
  static constexpr int kGrowMs = 5;          // per underrun
  static constexpr int kShrinkMs = 2;        // per stable period
  static constexpr int kStablePeriodMs = 10000;
  static constexpr int kSkipSlackMs = 25;    // skip ahead beyond target + slack

  bool openStream();
  void closeStream();
  static aaudio_data_callback_result_t onAudio(AAudioStream*, void* user, void* audio, int32_t frames);
  static void onError(AAudioStream*, void* user, aaudio_result_t error);

  size_t available() const { return writePos_.load() - readPos_.load(); }

  std::mutex streamMutex_;  // open/close/reopen
  AAudioStream* stream_ = nullptr;
  OpusMSDecoder* decoder_ = nullptr;
  int channels_ = 2;
  int sampleRate_ = 48000;
  int samplesPerFrame_ = 240;
  std::vector<int16_t> pcm_;  // one decoded Opus frame

  // Single-producer/single-consumer ring of interleaved samples. Positions
  // only grow; index = pos % capacity.
  std::vector<int16_t> ring_;
  std::atomic<size_t> writePos_{0};
  std::atomic<size_t> readPos_{0};
  bool primed_ = false;           // callback thread only
  int framesSinceUnderrun_ = 0;   // callback thread only
  std::atomic<int> targetMs_{kStartTargetMs};

  std::atomic<uint64_t> underruns_{0};
  std::atomic<uint64_t> skips_{0};
  std::atomic<uint64_t> packets_{0};
  std::atomic<uint64_t> concealed_{0};
};
