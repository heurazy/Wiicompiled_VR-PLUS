# VR settings integration audit

Checked against the merged runtime on 2026-09-26. This is a source and automated-test audit; headset behavior still requires a hardware test.

The desktop VR menu and headset panel call the same DrawVrSettings function and write the same RuntimeConfigFile settings. They do not maintain separate configurations.

| Control | Consumer / effect | Timing |
| --- | --- | --- |
| Enable VR, force SteamVR | OpenXR integration initialization, steamvr_launch | Next launch |
| Desktop mirror | aurora_set_stereo_mirror_view | Live |
| VR controller mode | OpenXRSetControllerMode; PAD/KPAD source selection | Live |
| Automatic Wii Remote search | SetWiiContinuousScanEnabled; Bluetooth polling opt-in | Live; legacy preference ignored |
| VR frame interpolation | OpenXRSetFrameInterpolationFps; headset pacing | Live; guest simulation remains 60 Hz |
| Resolution per eye | VrRenderScale; swapchain initialization | Next launch |
| Adaptive resolution | OpenXR integration adaptive scale controller | Live |
| Eye replay diagnostics | Aurora EFB replay flags | Live; hidden under advanced diagnostics |
| HUD virtual screen | ApplyVrHudVirtualScreen; stereo HUD placement | Live |
| Left-hand HUD | OpenXR integration hand-HUD pose | Live; cockpit uses forward HUD |
| Recenter / key binding | OpenXRRequestRecenter; host input handler | Live |
| Lean-back angle | OpenXRSetLeanBackDegrees | Live |
| Race view | Policy immersive toggle, OpenXR immersive window | Live |
| Race camera / default camera | MkwVRSetCameraMode / new-race initialization | Live / next race |
| Diorama distance, height, scale | MkwVRFirstPerson camera builder | Live |
| Stick calibration, item/trick and drift/brake swaps | MapQuestInput in PADRead | Live in recommended Gamepad mode |
| Menu shader quality | Aurora shader and OpenXR scene composition | Live |
| Replay introduction and tutorials | Welcome flag / independent tutorial completion bits | Next presentation / eligible race |
| Seat, cockpit scale, custom head offsets | MkwVRFirstPersonApplyConfiguredSettings; seat anchor | Live |
| View rotation | Custom/legacy camera orientation | Live; disabled when cockpit vehicle-motion setting supersedes it |
| Follow vehicle motion / Safe, Tilt, Tilt+side, Full | First-person motion builder and Safe interpolation | Live |
| Model visibility | First-person local-driver / kart draw filtering | Live |
| Vehicle wheel, native wheel, hand steering | First-person mesh animation and OpenXR driving state | Live; hand mesh availability may require restart |
| Wheel tuning and haptics | OpenXR driving WheelTuning | Live |
| Graphics, audio, controllers | Shared desktop runtime settings functions | Their existing live/restart behavior |

Android-only passthrough and foveation controls are excluded from the Windows panel.

## Corrections made during audit

- Removed the duplicate first-person checkbox: Race camera is now the single three-mode selector.
- Unified the two model-hiding checkboxes into one mutually exclusive visibility selector.
- Disabled legacy rotation when cockpit vehicle motion already controls rotation.
- Distinguished desktop interpolation from VR interpolation; these affect separate outputs.
- Exposed the existing per-eye render scale with an explicit next-launch label.
- Removed an unrelated menu-shader update from the steering-wheel checkbox.
- Reset first-person defaults also restores Safe vehicle motion.
- Tutorial labels now follow the configured item/trick and cockpit drift/brake swaps.
- Kept Wii Remote emulation labeled as a separate mode; the port's button swaps apply to Gamepad mode.
- Opening VR settings in an active race requests the game's own pause input. Closing resumes only a pause initiated by settings; an existing player pause is preserved. Requests are bounded if the game cannot pause (for example an online race).
- All game input sources are blocked while the settings panel is open. The pause injection is applied afterward.

The onboarding test package has its own portable marker, configuration, NAND and identity. No links to an existing save are created. Intro and both tutorial completion flags start unset.
