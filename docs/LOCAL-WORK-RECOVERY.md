# Recovering the project after deleting a local checkout

Snapshot recorded on 2026-10-30. The maintained source is the `openxr-work`
branch of [Wiicompiled VR PLUS](https://github.com/heurazy/Wiicompiled_VR-PLUS).

```powershell
git clone --branch openxr-work https://github.com/heurazy/Wiicompiled_VR-PLUS.git
cd Wiicompiled_VR-PLUS
```

## Pending SteamVR controller/menu fix

The source after v1.3.1 contains the test correction previously prepared locally:

- A separate ImGui WebGPU backend for the headset panel, with its own projection
  uniform and vertex/index buffers rather than the desktop backend's resources.
- The stable `mkw_vr_controls_v2` OpenXR action-set name, requesting fresh default
  bindings instead of retaining the older incomplete SteamVR layout. Custom
  SteamVR bindings for the old action set may need to be reapplied.
- Logs for accepted suggested profiles and transitions of each hand's current
  profile and pose activity, without a per-frame retry/log loop.

Both Windows products compiled, both dispatch registries validated, and 5
configuration/controller/interpolation plus 52 renderer tests passed when the
test executables were prepared. Confirmation on the affected SteamVR headset
is still pending. This source push does not replace the v1.3.1 release assets
and is not a claim that the reported hardware issue is confirmed resolved.

## Other source that must not be lost

- `integrations/wheelwizard-vr.patch` exactly matches the modified local Wheel
  Wizard checkout at pinned revision `86618e7367df935d78401583136e492c6f00fa27`
  (comparison ignores line-ending differences only). Its changes and tests
  are already tracked; the ignored checkout is disposable. See
  [launcher integration](../integrations/README.md).
- [Archived pre-PLUS changes](archive/pre-plus-local-changes.patch) preserve
  the 28 modified tracked files in the older `github-ready-0.3.0` checkout.
  They are historical migration work, **not another patch to apply to PLUS**.
  The base is public commit `f4d86f51286ccdc531c8ce6015f849176d1f52ff` of
  `heurazy/mario-kart-wii-VR-port`. To inspect/reconstruct that older work,
  check out that commit in a separate clone and apply this patch there.
  Its availability does not assert that every old hunk should replace newer
  PLUS code. The local `legacy-port-publish` checkout had no uncommitted changes.
- Runtime controller assets, hand/body defaults, build scripts and integration
  documentation are versioned with the maintained source. Release v1.3.1 in
  both public repositories preserves the installer, portable bundle, Quest APK
  and checksums.

## Rebuild instead of backing up build caches

Use the versioned `Launcher/Build-Installer.ps1`, `Launcher/Build-Portable.ps1`,
`Launcher/Build-WheelWizard.ps1` and `android/Build-Quest.ps1` scripts. Follow
[Quest build instructions](quest-port.md) and [packaging notes](PORT-CONTROLS-AND-PACKAGING.md)
for prerequisites and game translation. Do not reuse the v1.3.1 toolkit tag for
a future corrected release; select that future release's own tag.

`generated`, dependency downloads, native builds, Gradle/.NET caches and installed
game folders are reproducible from source, tools and the user's own clean PAL
disc image. Machine-specific SDK paths in `android/local.properties` should be
recreated. Generated game code, disc data and locally translated game executables
are not public backup artifacts.

## Private data stays outside GitHub

Configuration and save files found inside the local test installations were
archived separately, outside the workspace, with a manifest and verified hashes.
Keep that private archive before deleting the original workspace. It is not
included here: profiles, Miis, online identities and saves are personal data.
Build caches, crash memory dumps and logs were excluded. Other installations
outside the workspace and data on the Quest are not covered by this local archive.

Keep the existing Android signing keystore outside the checkout as well; it is
needed to update the same installed app without changing signing identity.
Do not upload signing keys, private saves, ROMs or game assets to this public repo.
