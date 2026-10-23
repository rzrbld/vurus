# Native projectile contacts and collision lookup

Ordinary Ammo uses the retained native endpoint graph for physical contacts.
The source executable's moving-body caller builds proposed bounds through
`41b4b0` and queries `4105d0`; `410a00` traverses the linked X/Y endpoints.
The implementation preserves graph publication order and closest non-contact
hints, `41a470` admission, and `4069d0` point/circle/edge/rectangle dispatch.

Contact selection uses the original priority and proposed-center distance rule.
An Item loses priority to a non-Item. Exact distance ties admit the later visited
candidate. Caller exclusions applied after selection do not trigger a search for
another target. Physical Ammo/Ammo rejection occurs before geometry; this is
separate from radial damage admission. Hidden/disabled source bounds bypass
physical contact traversal according to the source flags.

Point-source geometry retains the observed independent float stores and extended
intermediates. Point/rectangle selects its first crossing edge; circle/rectangle
uses its closest-contact path. Authored edge direction is retained. Moving Ammo
outside map bounds is released before contact queries and CREATE/impact effects.
The already implemented Grenade response remains a separate adapter.

The ordinary authored-world path no longer performs a second swept actor
selection or a 16-step blocked-terrain refinement after native contact selection.
Explicit synthetic surface-only edges retain their fixture fallback: they do not
supply native segment/rectangle records.

## Secondary index removal

The port's additional surface bounding-box vectors, global sort and groups of
16 bodies are absent from normal builds. Native physical actors and ordinary
Ammo share the existing endpoint graph. The accepted continuous player sweep,
passive movement and synthetic fallback use the existing 64-by-32 actor-cell
lists for candidate discovery, followed by their existing exact tests.

Fallback coverage retains a tick-boundary eligibility mask, a conservative
maximum-radius scalar, IDs without a native cell, and the existing newborn tail.
These are derived metadata, not another spatial index or serialized state.
Successful map load and restore rebuild this metadata. The player movement,
inertia and continuous wall solver are not replaced by native endpoint response.
The original game contains both actor cells and an endpoint graph; reuse of the
cells here does not establish complete parity of the continuous port adapter.

## Validation and scope

Retained original-instruction records cover 896 point-source, 896 circle-source
and 672 edge/rectangle cases. Fresh comparisons pass with zero finite coordinate
error; singular results are separately counted. These execute original
instructions with synthetic memory and hooked dependencies, not a Windows
playthrough. The source PE SHA-256 is
`415f40822f0475808caa019122c1d3dd15da6c5e94ebceeffddc4b3a6134bc46`.

The portable regressions cover contact winner selection, Item/late-ignore rules,
point/circle and bare/visible Ammo, terrain response, map-edge disposal and save
continuation. An independent cell-coverage regression compares 8,000 live queries
with a full-body scan, including held/out-of-map bodies and newborns. Touch tests
check live cell coverage, graph bounds, deferred death and continuation.

For original-resource integration validation, configure
`-DVURUS_COLLISION_INDEX_ORACLE=ON -DBUILD_TESTING=ON`, explicitly build
`collision_index_equivalence_audit`, then run:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  ./build/collision_index_equivalence_audit ORIGINAL_DATA OUTPUT_DIR 360
```

The reference index is compiled only for this opt-in audit. Both compared Worlds
use native Ammo contacts; they differ only in secondary-index use. Four maps and
1,440 paired frames, including movement, firing and file save/reload, retain
identical World/renderer bytes and pixels in the validated controls.

A matched H700 control/candidate pair (same native contact mechanics in both)
reduced crossfire World mean from 4.9016 to 4.5481 ms, with identical recorded
populations and firing counters. FPS was approximately 60 in both. Each scene used
120 warm-up and 1,200 measured steps, fixed 1/60 World time and one rendered frame,
640-by-480 Mali-G31 GLES rendering and ALSA. This is one pair, not a general FPS
or repeatability claim. The native contact repair changes the encounter relative
to the old swept solver; comparing those different encounters is not a matched
performance measurement.

This work does not claim full-game weapon parity. Stock transactions, Triple RNG,
guidance ordering, graphics-completion retirement and other separately tracked
boundaries are outside this change. No save schema or persistent actor/projectile
layout changes are introduced.
