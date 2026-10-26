# Third-party notices

Deskray is licensed under the GNU General Public License v3.0 (see `LICENSE`).
It includes or links the following software, each under its own license. All
of these licenses are compatible with distributing Deskray under GPL-3.0.

| Component | Use | License | Source |
| --- | --- | --- | --- |
| moonlight-common-c | Streaming protocol (RTSP, video/audio/input, FEC) | GPL-3.0 | https://github.com/moonlight-stream/moonlight-common-c |
| ENet (bundled with moonlight-common-c) | Reliable UDP control channel | MIT | https://github.com/cgutman/enet |
| nanors (bundled with moonlight-common-c) | Reed-Solomon forward error correction | MIT | https://github.com/sleepybishop/nanors |
| Mbed TLS | AES-GCM for moonlight-common-c | Apache-2.0 (dual-licensed with GPL-2.0-or-later) | https://github.com/Mbed-TLS/mbedtls |
| Opus | Audio decoding | BSD-3-Clause | https://github.com/xiph/opus |
| Khronos OpenXR loader | OpenXR runtime loader | Apache-2.0 | https://github.com/KhronosGroup/OpenXR-SDK-Source |
| Bouncy Castle (bcpkix, bcprov, bcutil) | Client certificate generation for pairing | MIT | https://www.bouncycastle.org |

Parts of the pairing, HTTP and keyboard-mapping code are ported from
[moonlight-android](https://github.com/moonlight-stream/moonlight-android)
(GPL-3.0). Deskray streams from [Sunshine](https://github.com/LizardByte/Sunshine)
(GPL-3.0), which runs separately on each host and is not distributed with the app.

Deskray is not affiliated with or endorsed by the Moonlight or Sunshine
projects, Meta, or NVIDIA.
