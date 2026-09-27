# Quest 3 standalone preview

The Android application runs the shared VR port on the headset using OpenXR and
Vulkan. SteamVR and a streaming PC are not used during play. Both Mario Kart Wii
and Retro Rewind are available in the Android launcher.

This preview includes the migrated camera, driving, onboarding, HUD and menu
features. The Android defaults disable SteamVR and use the Performance menu
shader preset. Native and VR frame interpolation are disabled on Quest, including
when an older configuration enables them; their controls are hidden on Android.
Head tracking remains active and the game keeps its normal simulation speed.
Touch Plus models include their textures and animated buttons and thumbsticks.

Mario Kart Wii and Retro Rewind have been launched on a Quest 3, with working
image, controls and in-race VR settings. The latest interpolation change still
needs visual confirmation in the headset.

## Installing a local test build

1. Enable developer mode on your Quest and install the APK through SideQuest or
   `adb install -r WiiCompiled-VR-Quest3.apk`.
2. Open **WiiCompiled** once. Import your clean PAL RMCP01 disc image in the
   launcher, or copy your extracted `DATA` directory into
   `/sdcard/Android/data/org.wiicompiled.quest/files/WiiCompiledOpenXRVR/DATA`.
3. Build from your disc using **Build on this Quest**, or copy a matching `.wcgame`
   package into the launcher's `Import` directory and reopen the launcher.
4. Select Mario Kart Wii or Retro Rewind. Retro Rewind also requires its mod pack,
   which the launcher can download.

The APK contains the runtime game kit and build tools. It does not contain a ROM
or translated game. Keep packages built from your disc for your own installation.
The game runs independently after setup.

## Building on Windows

See [the Android build guide](../android/README.md). The build requires JDK 17,
Android SDK 36, NDK 29, .NET 10 and Rust's `aarch64-linux-android` target.
`MKW_POWERSHELL` can select PowerShell 7 for the export/toolchain tasks.
`MKW_ANDROID_FETCHCONTENT_BASE_DIR` and `MKW_ANDROID_NATIVE_BUILD_DIR` can point to
short absolute directories when a Windows checkout exceeds path limits.

`Build-Quest.ps1 -StockDawn` builds with the pinned binary Dawn package. That
variant has no fragment-density foveation; the default source build enables it.
