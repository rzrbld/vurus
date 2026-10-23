# Ordinary Item animation admission

Original `433b30` traverses the padded camera-cell window in native cell/head
order. Its visible-update pass skips Graphic-update-needed classes. `431ae0`
Item subtype `0x12` advances the embedded shadow then body animation before
draw-only filtering. Its update predicate is phase != draw OR class lacks
Graphic-update-needed. Thus admitted ordinary Items receive both native phases;
off-window ordinary Items do not receive those playback calls.

Gameplay now applies this protocol to non-Graphic, non-invisible Item channels.
The general animation pass drains their construction/reset audio independently
of camera playback, then leaves cursor/held-sample updates to the existing native
cell phase traversal. The phase synchronizes a new binding before advancing its
cursor, so the next tick cannot reset its first admitted pose. Player-only passes
exclude Items. Legacy adapters retain their old broad advancement.

Saving-plot body playback additionally requires the selected World saving actor
(native `world+0x20c8`); its shadow has no such extra guard. That actor is supplied
as read-only view metadata. Every authored saving-terminal placement in the
25-map census is Graphic-update-needed and remains outside this migration.
Invisible Item fades and Graphic Ammo/FX/Thing/Gate/Lift clocks remain separate
boundaries. General channel-list enumeration still exists; no dynamic eligibility
worklist or universal offscreen freeze is introduced. Simulation ordering, World
layout and save schema do not change. Existing fixed-step phase timing remains
an adapter, not a claim of native variable-refresh equivalence.

## Validation

The phase audit loads four original maps, checks independently calculated cell
admission against actual cursor changes, and verifies authored Item Play events.
It includes camera changes, player-only masks, detail toggles, zero/17/91ms steps,
a 5000ms adversarial cursor fixture, renderer restore and post-advance Item births.
The long fixture tests cursor behavior; it is not a native main-loop timestep.
A negative control without binding synchronization reproduces the birth reset.

The gameplay audit runs two live applications with old/new Item admission. It
compares serialized World and non-migrated persisted renderer channels, plus
non-Item semantic audio, across movement, firing, real pickups, camera edges,
Darkness/Time Warp, redraws, post-advance effects and actual file save/reload.
Item poses deliberately differ; pixel equality is not asserted. Audio comparisons
exclude transient numeric sample-owner IDs because admission can change their
allocation order. This is not full-game or original Windows runtime acceptance.

```sh
cmake -S . -B build-item-audit -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVURUS_ACTOR_SYNC_PROFILE=ON
cmake --build build-item-audit --target item_animation_phase_audit item_animation_gameplay_audit -j4
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build-item-audit/item_animation_phase_audit /path/to/original/data
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build-item-audit/item_animation_gameplay_audit /path/to/original/data /tmp/vurus-item-gameplay-audit 360
```

Use isolated output paths and tester-owned original resources. No proprietary
fixtures, captures or disassembly exports are distributed.

The private candidate passed 187 tests, 443,464 cursor checks with four Item births
and nine Play events, and 1,440 gameplay comparisons. Original-instruction controls
covered 1,796 cases with declared playback/draw dependency hooks, including 96
saving-selection cases; these are not native GAF/audio/runtime playthroughs.
H700's corrected final matched pair reduced crossfire main-thread CPU mean from
14.6127 to 14.4009ms (1.45%). Seven-scene settings and combat counters matched.
An earlier pre-birth-correction ABBA trial showed about 0.64% there. These limited
instrumented CPU observations are not an official FPS gain or universal speedup;
some playback work moves from the animation phase into the draw phase.
