# Directional Ammo and Thing body graphics

The native body graphics array is actor+0x1ac:24 independent ANM records of72
bytes. Relevant ANM fields are flags+0, bank+8, sequence+c, remaining integer
milliseconds+14, one-based subframe cursor+20, and audio ownership+30.

## Binding and selection

Initializer417b30 first returns when both bank pointers are absent OR when both
primary/shadow sequence indices are0. These are independent fences. An allocated
primary array can remain unbound, including shadow-only and cross-pair inputs.
Ammo418cc9..418d3f binds authored base+h to heading h=0..23. Thing418e16..418e69
binds the SAME authored base to24 independent records. Authored base25 is valid;
subframes within each sequence are temporal poses, not headings. Invalid indices
are native fatal errors, not absent graphics.45e770 initializes each record and
enters its first subframe with elapsed0, in slot order. Empty bound sequences stop.

431ec2..431f19 selects heading*72. Only the selected record advances; switching
headings does not reset state, and returning resumes its retained clock. The port
stores these clocks indirectly on visual actors and shares immutable decoded
bank metadata. Compact ordinary Ammo has no extra actor or24-slot allocation.
Unknown metadata, known absent array and present-but-unbound sequence0 are distinct.
An unavailable renderer channel cannot prove an array absent.
Without a ContentReader, World explicitly runs with graphics disabled: body
bindings remain Unknown even in sprite-free geometry fixtures. Installing the
reader before load enables authoritative native binding/retirement. Adding a
reader later does not invent past clocks; use a genuine birth/rebind/reload.

Recycled fixed projectile-trail slots follow the same birth contract as new
objects: forget prior graphics/audio mappings before replacing ActorState, then
initialize body graphics after the new native identity. Restore the authored
Graphic-update-needed flag and fresh independent clocks even when offscreen.
A later visible pass cannot substitute for birth initialization. Repeated reuse
and real save/reload continuation are covered by native_body_graphics_world_test.

## Calling phases

431ba8..431bce admits advancement for nonzero mode, or mode0 only when the class
lacks Graphic-update-needed0x20000. Caller admission still applies:

- Admitted Graphic-needed awake visits advance before Ammo motion; activation
  off/on early returns and prior lifetime/energy removal do not advance.
- Post-World433b30 mode1 skips Graphic-needed bodies and uses the previous camera.
- Actual draw433b30 mode0 uses the current camera. A non-Graphic admitted body
  can advance in both visible phases. These are cell/mask admissions, not sprite
  rectangle visibility; hidden/detail filtering follows animation work.

Original active draws can reuse the stored World+101c integer delta without a new
World tick. Native pause, inventory and help operate on a frozen framebuffer and
perform no body animation pass. The port explicitly suspends body phases in these
modal states while preserving retained-delta active redraws. Existing enemy phase
adapters remain separate.

The current projectile adapter preserves accepted slope/activation ordering:
body advancement is after successful activation and before guidance/horizontal
motion; its earlier slope calculation does not inspect animation. A ground-impact
branch bypassing activation retains that accepted ordering and advances graphics.
This is an explicit adapter boundary, not a claim that all instructions moved into
native dispatcher order.

## Retirement and audio

The common444ca0/445274 tail examines the FINAL heading after movement/CREATE,
which can differ from the heading advanced before motion. Runtime actor flag8
bypasses retirement. Otherwise a missing array retires; selected sequence0 stays;
a bound selected record with stopped8 retires. Blank bitmap and null frame are
not completion tests: a stopped hold-last record may still expose a frame. Both
Ammo and Thing use full destroy, preserving drops, Explosion transfer, manager and
identity release. Graphical completion is not an immediate radial-impact event.
Completion reached in a visible phase waits until the next admitted World tail.

Each slot has distinct transient audio ownership. Heading changes neither stop
nor forget the previous voice. Frame entries request sample playback, selected
updates refresh actual live ownership, and genuine rebind forgets old mappings
without stopping already playing sounds. Soundless slots emit no Forget events.
Opaque portable IDs are not native voice handles. Audio consumes queued ownership
requests and validates mixer liveness; raw sample-command bits remain available.
World/App retain actor pins until those queued cues have been consumed/discarded.
Cross-producer ordering follows the current application's explicit phase drains;
this does not establish every native enemy/Ammo audio interleaving.

## Persistence and boundaries

Save schema37 writes explicit binding/kind/base and all24 sequence/cursor/flags/
clock states, plus runtime hidden state and the stored World body-phase delta.
Audio IDs and device ownership are transient and are recreated without replaying
current samples. Metadata-independent shape checks occur during decoding; World
restore also checks actual bank sequence/cursor validity before mutation.

Readers1–36 remain supported. Their body histories use explicit LegacySingleCursor
behavior until genuine birth/rebind, preserving the old renderer path. One legacy
clock is never cloned into24 invented histories. New saves retain that explicit
legacy policy when necessary. An ordinary application save captures before final
draw and loads into the corresponding immediate-draw boundary before the next
World tick; tooling must compare the same caller phase.

These findings derive from retained disassembly/decompilation and bounded original-
instruction probes. They do not establish full Windows runtime parity across all
banks/maps, all constructor callbacks or every visibility/camera mode. Shadows,
player/enemy special banks and separately audited Explosion timing remain separate.
