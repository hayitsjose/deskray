# deskray

Low-latency remote desktop from Meta Quest 3 to your Linux machines.
A native OpenXR client speaks the Moonlight protocol to [Sunshine](https://github.com/LizardByte/Sunshine)
running on each Linux host, and shows the desktop on a curved screen over passthrough.

Status: early development (Phase 2 of 8: OpenXR client skeleton).

## Host (each Linux machine)

```bash
bash host/host-setup.sh
```

Then open `https://localhost:47990` on that machine and create the Sunshine admin login.

## Client (Quest)

Requires JDK 17 and an Android SDK with NDK 27.2 (`client/local.properties` → `sdk.dir=...`).

```bash
cd client
./gradlew installDebug
adb shell am start -n io.github.hayitsjose.deskray/android.app.NativeActivity
adb logcat -s deskray
```

## License

GPL-3.0. Built on [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c)
and inspired by [Moonlight](https://moonlight-stream.org/); not affiliated with Moonlight, Sunshine or Meta.
