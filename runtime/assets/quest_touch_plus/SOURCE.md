# Touch Plus controller assets

Left and right Meta Quest Touch Plus GLB meshes, UV textures and button transforms
come from the WebXR Input Profiles asset repository, under its MIT license.

Source commit: `4484a05e30bcd43fe86bb4e06b7a707861a26796`

https://github.com/immersive-web/webxr-input-profiles/tree/4484a05e30bcd43fe86bb4e06b7a707861a26796/packages/assets/profiles/meta-quest-touch-plus

The original GLBs are retained for reproducibility. `android/Convert-TouchPlus.py`
exports textured triangles, nine animation endpoints and tutorial landmarks.
Only the converted geometry, RGBA textures and attribution ship in the APK.
The Android runtime animates them from its existing OpenXR button states, while
Windows continues to use the controller models returned by SteamVR.

See `LICENSE.md` for the upstream asset license.
