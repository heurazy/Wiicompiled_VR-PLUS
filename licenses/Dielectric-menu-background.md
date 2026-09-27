# Dielectric menu background

The VR menu environment in `aurora-main/lib/vr_ui.hpp` adapts the
“Dielectric” shader credited to @Xor in the FragCoord.xyz screenshot
provided by the user.

Reference supplied by the user: https://t.co/kdebpbDcaQ

The code was transcribed from that screenshot because the linked page was
unavailable. This is a WGSL adaptation, with panoramic ray directions,
numerical guards, reduced brightness and slower animation. The field is rendered
at 1440 × 1440 for each eye with 28 × 6 iterations. Rays originate at each eye's
position in a shared space (six metres per field unit), so translation and stereo
separation produce parallax instead of sampling an infinitely distant panorama.

The source's licensing terms have not been verified. Attribution here does
not establish permission for redistribution of the original shader.
