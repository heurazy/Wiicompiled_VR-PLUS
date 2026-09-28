# VR PLUS 1.2 test builds

The Quest test build corrects a renderer abort during scene transitions when a
GX draw has no position attribute. The GX descriptor mirror now follows
GXClearVtxDesc's direct-position default, and Aurora treats a remaining
positionless draw as degenerate geometry instead of terminating the game.
Quest game kits use the same header-fingerprint ordering in PowerShell 5 and 7,
so locally rebuilt games match the APK. The corrected APK and both game
packages were installed on a Quest 3; Retro Rewind reached a race in VR.

The Windows setup and Quest APK identify this fork as version 1.2.0. Wheel Wizard checks
`heurazy/Wiicompiled_VR-PLUS` for VR updates, rather than the old port's v1.1.1 release.

Windows setup downloads and installs Retro Rewind by default, using the official Wheel Wizard
distribution endpoint. Uncheck **Download and install Retro Rewind automatically** to install only
the base game. An explicitly selected existing RetroRewind6 folder takes precedence over downloading.
The downloaded pack is installed with the product, and Wheel Wizard uses that same directory for
subsequent updates and patches. An unsuccessful download is reported as an installation failure.

Every Retro Rewind installation checks the official version manifest, including an existing pack
selected manually. Older packs are replaced by a staged download with every incremental update
newer than the full ZIP applied before compilation. Personal `Patches` files are preserved.
Current packs are reused without downloading another copy. A failed version check stops the
installation rather than silently claiming an old pack is current.

Wheel Wizard's online/offline label follows Retro Rewind's distribution connectivity. Missing or
unreachable GitHub releases for a verified local VR bundle no longer force **Play Offline**;
Retro Rewind's own server failures still do.

The local compiler's SHA-256 helper no longer depends on PowerShell's Utility module being
available after PATH is restricted to the bundled tools. The setup also dispatches its documented
`--info-json` and `--build-quest` commands again.

For a scripted installation, pass `--download-retro-rewind` to `--silent` together with
`--game` and `--install-dir`. It enables the online Retro-WFC payload too. This option cannot be
combined with `--retro-dir`; use the latter for an existing pack instead.

Release packages do not include a ROM, game data, or translated Nintendo executable. Each user supplies their own clean PAL Mario Kart Wii disc image and compiles the game locally.
