# Deskray

Low-latency remote desktop from **Meta Quest 3** to your **Linux** machines.

Deskray streams a Linux desktop to the headset with game-streaming latency:
the host's GPU encodes, the Quest's hardware decodes straight into the
compositor, and your controllers, keyboard and touchpad drive the desktop.
Use it in an ordinary Horizon OS window next to your other apps, or on a
curved screen in immersive mode.

It speaks the open Moonlight/GameStream protocol to [Sunshine](https://github.com/LizardByte/Sunshine),
which runs on each Linux machine.

| | |
| --- | --- |
| **Window mode** | The desktop in a resizable Horizon OS window. Point and click with the system pointer. |
| **Immersive mode** | A curved screen over passthrough. Controller ray = mouse, trigger = click, A/X = right click, thumbstick = scroll, B/Y = back to the machine list. |
| **Input** | Controllers, Bluetooth keyboard (Super works, for Hyprland/GNOME/KDE shortcuts) and keyboard touchpads. |
| **Measured** (Quest 3, 1080p60 HEVC over Tailscale) | ~6.7 ms host encode, ~2.8 ms headset decode, adaptive 20–30 ms audio buffer. |

## Install on the Quest

1. Put the headset in [developer mode](https://developers.meta.com/horizon/documentation/native/android/mobile-device-setup/).
2. Download `deskray-<version>.apk` from [Releases](https://github.com/hayitsjose/deskray/releases) and install it:
   ```bash
   adb install deskray-<version>.apk
   ```
   SideQuest and [Obtainium](https://github.com/ImranR98/Obtainium) (for automatic updates from Releases) work too.
3. Open **Deskray** from your library (Unknown sources).

## Set up each Linux machine

```bash
git clone https://github.com/hayitsjose/deskray && cd deskray
less host/host-setup.sh    # read it first: it uses sudo to install Sunshine
bash host/host-setup.sh
```

The script installs Sunshine (Arch, Debian/Ubuntu, Fedora), picks the screen
capture method for your desktop (wlroots/Hyprland/Sway, KDE, GNOME, X11) and
the hardware encoder for your GPU, and enables it as a user service. Then open
`https://localhost:47990` on that machine and create the Sunshine login.

**Networking.** The headset and the machine must reach each other: the same
LAN, or [Tailscale](https://tailscale.com) on both (sideload the Tailscale
Android APK on the Quest). If your firewall doesn't trust the tailnet, open
TCP 47984, 47989, 48010 and UDP 47998–48000 on `tailscale0`.

## Pair and connect

1. In Deskray, tap **Add machine** and enter its Tailscale name or IP.
2. The headset shows a 4-digit PIN. On the machine, open `https://localhost:47990/pin` and enter it.
3. Tap **Window** or **Immersive**.

Resolution, frame rate and bitrate are on the main screen. On Wi-Fi, keep the
host wired to the router if you can, and use 5 or 6 GHz for the headset.

## Build from source

Needs JDK 17, the Android SDK with NDK 27.2.12479018 and CMake 3.22.1.

```bash
git clone --recursive https://github.com/hayitsjose/deskray
cd deskray/client
echo "sdk.dir=$HOME/Android/Sdk" > local.properties
./gradlew installDebug
```

Release builds are signed from `~/.local/share/deskray/signing/keystore.properties`
(`storeFile`, `storePassword`, `keyAlias`, `keyPassword`) or the `DESKRAY_KEYSTORE*`
environment variables used by CI. Tagging `vX.Y.Z` builds a signed APK and
publishes a GitHub release.

### Layout

| Path | What |
| --- | --- |
| `host/host-setup.sh` | Provisions Sunshine on a Linux machine |
| `client/app/src/main/java/…/ui` | Home (machine list) and window-mode panels |
| `client/app/src/main/java/…/net` | Identity, pairing, GameStream HTTP(S) |
| `client/app/src/main/cpp/xr` | OpenXR immersive mode: session, curved screen, controllers |
| `client/app/src/main/cpp/stream` | moonlight-common-c glue, MediaCodec video, Opus/AAudio audio, keyboard/mouse |

## License

GPL-3.0. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the
components Deskray builds on, notably moonlight-common-c and code ported from
moonlight-android. Not affiliated with Moonlight, Sunshine, Meta or NVIDIA.
