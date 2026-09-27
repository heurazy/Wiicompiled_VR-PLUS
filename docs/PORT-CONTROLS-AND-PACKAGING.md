# Original-port controls and packaging

The Windows OpenXR integration now defaults to **Gamepad** controller mode and
the controls from the original heurazy VR port. The optional Wii Remote emulation
mode retains its own mapping and motion gestures; select it explicitly in F10.

## Touch / Quest / PICO and Index

| Control | Action |
| --- | --- |
| Right trigger | Accelerate / pointer selection |
| Left trigger | Brake, then reverse; overrides throttle and drift |
| Left stick | Steer, navigate and aim items forward/backward |
| Left Y (Index B) | Use / hold item |
| Left X (Index A) | Trick / wheelie |
| Right stick | Directional tricks |
| Right stick click | Cycle Original, First person, Diorama during a race |
| Right A | Confirm; accelerate outside cockpit; drift in cockpit |
| Right B | Brake / back |
| Grips in cockpit | Grab the wheel / handlebar; either hand or both |
| Right grip outside cockpit | Hop / drift |
| Left X+Y (Index A+B) | Open / close VR settings |
| SteamVR: hold left X/Index A for 0.65 seconds | One Mario Kart pause pulse |
| Other runtimes: left Menu | Mario Kart pause |
| F10 | VR settings on desktop and in headset |

SteamVR's system button stays reserved for its dashboard. Tapping X still tricks;
an options chord and focus recovery cannot accidentally generate a pause.
The headset welcome and tutorials stay available, and held inputs are withheld
until released when a panel closes. USB wheels retain priority in races.

## Controllers without face-button clusters

Vive wands and WMR/Odyssey use explicit alternative paths, rather than Touch
bindings. Left Menu is the trick/pause control and left pad click uses an item;
press both for VR options. Left trigger brakes/reverses in every camera.

WMR: right pad click confirms/drifts, right Menu brakes/goes back, and right
stick click changes cameras. Vive: right trigger confirms/accelerates, right
Menu drifts in cockpit (back outside it), and right pad click changes cameras.
These bindings and their runtime models still need testing on the actual hardware.
Unsupported optional profiles do not prevent supported controllers from loading.

## Options and upgrade behavior

F10 offers radial stick deadzone, outer range and centre calibration, plus item/
trick and cockpit drift/brake swaps. Calibration reads the raw stick even while
the panel blocks gameplay input.

Adaptive resolution is opt-in. It lowers internal eye dimensions in 10% steps
down to 70% after slow timing windows, and recovers after three good windows.
It never changes the game's simulation rate. Without frame interpolation its
target is capped at the game's 60 Hz render rate.

Legacy `[controller] wii_continuous_scan` is ignored. Searching defaults off;
only `wii_continuous_scan_opt_in = true`, set by the checkbox at the top of F10
or the Wii Remotes menu, enables continuous Bluetooth scanning. Already-connected
Wii Remotes remain usable. USB wheel discovery keeps the existing hotplug path.

`[vr] force_steamvr = true` defaults on. On Windows the game selects an installed
SteamVR manifest through `XR_RUNTIME_JSON` for its own process, including Steam
library folders. It does not rewrite the system's active runtime. Disable **Use
SteamVR (next launch)** to use another runtime. If SteamVR is not installed, a
diagnostic is written and the available runtime is retained.

## Wheel Wizard and portable bundle

The pinned Wheel Wizard integration now renames existing RKPD licenses even if
their Mii is absent from the NAND. It writes only the UTF-16 name and save checksum,
preserving identity, friend code and statistics; invalid/empty slots are refused.
Editing is blocked while the VR game runs. Existing NAND Miis are also renamed.

Build `Launcher/Build-Installer.ps1`, then `Launcher/Build-Portable.ps1`.
`Build-WheelWizard.ps1` builds the pinned GPL source with `wheelwizard-vr.patch`.
The installer embeds Wheel Wizard and publishes it transactionally. The portable
ZIP includes the full ROM-free installer, launcher, marker and English instructions.
Run setup with your own PAL ROM, then `Launch-WiiCompiled-VR.cmd`.

Code tests cover controls, pause timing, calibration, scan preferences, adaptive
scaling and license name byte boundaries. Headset, hardware-family and multiplayer
validation remain separate from these automated checks.
