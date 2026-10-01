# Character body IK

This feature is enabled by default in the first-person cockpit. It uses
the selected character's original skeleton, mesh and textures. No additional
Nintendo assets are distributed.

In **VR settings -> Cameras**, switch **Character body IK** off to restore the
previous driver visibility and VR gloves. **Character hands only** hides the
body and arms while retaining tracked, textured character hands. This preference
is saved as `[vr] body_ik_hands_only = true`.

Hands are half their original size; arm cross sections use 65% of their original
thickness and their length is fitted to the calibrated wrist targets. Close
controllers no longer force a 65 cm arm folded towards the headset.
The 24 standard characters use
built-in palm-centre profiles recorded in the calibration workshop. These
profiles are fixed; legacy Config.toml overrides are ignored. Unknown models retain the
5.5 cm wrist offset and mirrored 1.5 cm outward correction. These offsets follow
hand orientation and character size changes. The hips and legs retain their
native seat placement. Wide torsos can lean back slightly about the spine for
eye clearance. Native controls determine comfortable seat height and a distance
of 35-45 cm, including Waluigi and King Boo. Native wheel selection includes
the topology and node-ID fixes by iChris4 (3d48514, f013f9d).

The seated torso is latched for each driver/model and stays with the kart.
Legs and feet retain their native turn/drift articulation relative to the seated
pelvis, without inheriting animated root movement. During detected tricks,
mini-turbo release bonuses and their short settling interval, legs keep the
seated reference as well. Starting another drift restores its leg articulation
even if the previous mini-turbo is still active. The boost itself is unchanged.
Both native driver animation layers are checked: only driving, drift, steering,
reverse, wheelie and waiting poses can articulate the legs. Dash/celebration
poses stay blocked for their entire animation and blend-out, independently of
the mini-turbo timer, so raised-leg bonus poses cannot slip through.
Native drift, jump and character-specific trick animations cannot overwrite the torso.
The torso stays stable and the rider cannot jump into the view. Tracked arms,
hands and headset lean remain live; lightning still scales the seated pose.
This is seated body IK,
not foot or individual finger tracking. The body uses the selected cockpit
comfort/motion frame; Bullet Bill and race introductions use the original camera.

Only the local racer's render palettes change. Original palettes are restored
after view calculation, before gameplay continues. Other racers, physics and
network data are unaffected. Standard rigs with `arm_l1/arm_l2/wrist_l1`, their
right-hand equivalents and a head bone are supported. King Boo uses hands only,
as its head and body cannot be separated. Miis with a separate face model and
unsupported custom skeletons retain the previous behavior. Toadette's separate
hair is hidden.

## Built-in hand placement

The 24 character profiles recorded in the calibration workshop are now fixed
product defaults. The workshop, in-race capture and reset controls are removed
from VR settings. Existing `[body_ik_hand_calibration]` entries in Config.toml
are preserved on disk but ignored. Body IK and character-hands-only toggles
remain available. No save data or unrelated settings are changed.

## Validation

Headless tests cover IK reach, folded arms, opposite rotations, limb fitting,
scaled characters, pose reconstruction, rotated/translating controllers, frozen
reference poses, capture/cancel and tracking loss. Workshop parsing is checked
against all 24 installed character archives, including independent profile keys
and texture/hand geometry loading. Regression checks place a controller at each
visible palm centre, verify capture and reconstruct the saved wrist pose for all
24 characters. Rigid hand vertices remain in wrist-local space; neutral poses
orient their finger axis forward rather than using the original T-pose.
Windows products are compiled and their generated dispatch tables checked. The
Windows cockpit behavior and final drift-animation fixes were tested in a headset.
The Quest package is rebuilt with the same implementation; these new body IK
changes have not yet received a separate Quest headset test. Further character,
motorcycle and item-scale coverage remains useful.
