# Touch Plus controller assets

Left and right Meta Quest Touch Plus GLB meshes, UV textures and button transforms
come from the WebXR Input Profiles asset repository, under its MIT license.

Source commit: `4484a05e30bcd43fe86bb4e06b7a707861a26796`

https://github.com/immersive-web/webxr-input-profiles/tree/4484a05e30bcd43fe86bb4e06b7a707861a26796/packages/assets/profiles/meta-quest-touch-plus

The original GLBs are retained for reproducibility. `android/Convert-TouchPlus.py`
exports textured triangles, nine animation endpoints and tutorial landmarks.
Only the converted geometry, RGBA textures and attribution ship in the APK and
desktop resources. Both builds animate them from the existing OpenXR button
states. On Windows, SteamVR's own controller models take priority; Touch Plus
is the fallback for other runtimes or unavailable SteamVR models.

See `LICENSE.md` for the upstream asset license.
