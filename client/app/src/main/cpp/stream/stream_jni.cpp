// moonlight-common-c glue: video goes to the zero-copy MediaCodec decoder,
// audio to Opus + AAudio.
#include <android/log.h>
#include <jni.h>

#include <Limelight.h>

#include <atomic>
#include <cstdarg>
#include <cstring>
#include <string>

#include "stream/audio_renderer.h"
#include "stream/video_decoder.h"
#include "util/log.h"

namespace {

std::atomic<uint64_t> gFrames{0};

// SERVER_INFORMATION holds raw pointers; keep the strings alive for the session.
struct SessionStrings {
  std::string address, appVersion, gfeVersion, rtspUrl;
} gStrings;

int drSetup(int videoFormat, int width, int height, int redrawRate, void*, int) {
  LOGI("video setup: format 0x%x, %dx%d @ %d fps", videoFormat, width, height, redrawRate);
  gFrames = 0;
  return VideoDecoder::instance().setup(videoFormat, width, height, redrawRate);
}

int drSubmit(PDECODE_UNIT du) {
  const uint64_t n = ++gFrames;
  if (n == 1 || n % 300 == 0) {
    LOGI("video frame %llu: %d bytes, %s, host %.1f ms, receive %.1f ms",
         static_cast<unsigned long long>(n), du->fullLength,
         du->frameType == FRAME_TYPE_IDR ? "IDR" : "P", du->frameHostProcessingLatency / 10.0,
         (du->enqueueTimeUs - du->receiveTimeUs) / 1000.0);
  }
  return VideoDecoder::instance().submit(du);
}

void drCleanup() { VideoDecoder::instance().cleanup(); }

int arInit(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opus, void*, int) {
  return AudioRenderer::instance().init(*opus);
}

void arDecodeAndPlay(char* data, int length) {
  AudioRenderer::instance().decodeAndPlay(reinterpret_cast<const unsigned char*>(data), length);
}

void arCleanup() { AudioRenderer::instance().cleanup(); }

void clStageStarting(int stage) { LOGI("stage starting: %s", LiGetStageName(stage)); }
void clStageComplete(int stage) { LOGI("stage complete: %s", LiGetStageName(stage)); }
void clStageFailed(int stage, int err) { LOGE("stage failed: %s (error %d)", LiGetStageName(stage), err); }
void clConnectionStarted() { LOGI("connection started"); }
void clConnectionTerminated(int err) {
  if (err == ML_ERROR_GRACEFUL_TERMINATION) LOGI("connection ended by host");
  else LOGE("connection terminated: error %d", err);
}
void clStatusUpdate(int status) { LOGI("connection status: %s", status == CONN_STATUS_OKAY ? "okay" : "poor"); }
void clLogMessage(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  __android_log_vprint(ANDROID_LOG_INFO, "deskray-mlcc", fmt, ap);
  va_end(ap);
}

std::string toString(JNIEnv* env, jstring s) {
  if (!s) return {};
  const char* utf = env->GetStringUTFChars(s, nullptr);
  std::string out(utf);
  env->ReleaseStringUTFChars(s, utf);
  return out;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_hayitsjose_deskray_stream_NativeBridge_launchQueryParameters(JNIEnv* env, jobject) {
  return env->NewStringUTF(LiGetLaunchUrlQueryParameters());
}

extern "C" JNIEXPORT jint JNICALL
Java_io_github_hayitsjose_deskray_stream_NativeBridge_startStream(
    JNIEnv* env, jobject, jstring address, jstring appVersion, jstring gfeVersion, jstring rtspUrl,
    jint codecModes, jint width, jint height, jint fps, jint bitrateKbps, jint packetSize,
    jint streamingRemotely, jbyteArray riKey, jint riKeyId) {
  gStrings = {toString(env, address), toString(env, appVersion), toString(env, gfeVersion),
              toString(env, rtspUrl)};

  SERVER_INFORMATION server;
  LiInitializeServerInformation(&server);
  server.address = gStrings.address.c_str();
  server.serverInfoAppVersion = gStrings.appVersion.c_str();
  server.serverInfoGfeVersion = gfeVersion ? gStrings.gfeVersion.c_str() : nullptr;
  server.rtspSessionUrl = rtspUrl ? gStrings.rtspUrl.c_str() : nullptr;
  server.serverCodecModeSupport = codecModes;

  STREAM_CONFIGURATION cfg;
  LiInitializeStreamConfiguration(&cfg);
  cfg.width = width;
  cfg.height = height;
  cfg.fps = fps;
  cfg.bitrate = bitrateKbps;
  cfg.packetSize = packetSize;
  cfg.streamingRemotely = streamingRemotely;
  cfg.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
  cfg.supportedVideoFormats = VIDEO_FORMAT_H265 | VIDEO_FORMAT_H264;
  cfg.clientRefreshRateX100 = fps * 100;
  cfg.colorSpace = COLORSPACE_REC_709;
  cfg.colorRange = COLOR_RANGE_LIMITED;
  cfg.encryptionFlags = ENCFLG_AUDIO;
  env->GetByteArrayRegion(riKey, 0, 16, reinterpret_cast<jbyte*>(cfg.remoteInputAesKey));
  // IV = the key id, big-endian, in the first 4 bytes; the rest stays zero.
  const uint32_t id = static_cast<uint32_t>(riKeyId);
  const unsigned char iv[4] = {static_cast<unsigned char>(id >> 24), static_cast<unsigned char>(id >> 16),
                               static_cast<unsigned char>(id >> 8), static_cast<unsigned char>(id)};
  std::memcpy(cfg.remoteInputAesIv, iv, sizeof(iv));

  DECODER_RENDERER_CALLBACKS video;
  LiInitializeVideoCallbacks(&video);
  video.setup = drSetup;
  video.submitDecodeUnit = drSubmit;
  video.cleanup = drCleanup;
  video.capabilities = CAPABILITY_DIRECT_SUBMIT;

  AUDIO_RENDERER_CALLBACKS audio;
  LiInitializeAudioCallbacks(&audio);
  audio.init = arInit;
  audio.decodeAndPlaySample = arDecodeAndPlay;
  audio.cleanup = arCleanup;
  audio.capabilities = CAPABILITY_DIRECT_SUBMIT;

  CONNECTION_LISTENER_CALLBACKS listener;
  LiInitializeConnectionCallbacks(&listener);
  listener.stageStarting = clStageStarting;
  listener.stageComplete = clStageComplete;
  listener.stageFailed = clStageFailed;
  listener.connectionStarted = clConnectionStarted;
  listener.connectionTerminated = clConnectionTerminated;
  listener.connectionStatusUpdate = clStatusUpdate;
  listener.logMessage = clLogMessage;

  LOGI("starting stream to %s: %dx%d@%d, %d kbps, packet %d, remote=%d", server.address, width, height,
       fps, bitrateKbps, packetSize, streamingRemotely);
  return LiStartConnection(&server, &cfg, &listener, &video, &audio, nullptr, 0, nullptr, 0);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_hayitsjose_deskray_stream_NativeBridge_stopStream(JNIEnv*, jobject) {
  LiStopConnection();
  LOGI("stream stopped; %llu video frames received", static_cast<unsigned long long>(gFrames.load()));
}
