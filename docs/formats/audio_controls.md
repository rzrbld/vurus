# Audio cues and gameplay help controls

The native basis is the supplied executable with SHA-256
`415f40822f0475808caa019122c1d3dd15da6c5e94ebceeffddc4b3a6134bc46`.
Original-instruction probes use synthetic memory and explicit dependency hooks;
they are not a Windows listening or campaign playthrough.

## Save, transition and help cues

- Successful positive save debit emits one transient MONEY request on explicit
  channel3/priority191 (`42a516`). Synchronous/asynchronous paths share this
  boundary. Failed, free, retry and repeated completions stay silent; the port's
  debit-on-success policy remains.
- Opening Controls Help queues localized HELP with delay0/hold1000/tail5000ms
  (`43b67a`), after the presentation resets audio. Immediate speech update makes
  it audible even though this page suspends normal gameplay speech updates.
- An accepted Lift departure clears pending speech and queues WELL-DONE with
  0/1000/10000ms, attempting it before teardown (`442dd8–442e09`). The app waits
  for destination validation and preserves only the exact admitted mixer handle
  through the commit's audio/presentation resets. Later ordinary resets clear it;
  a replacement owner of the same channel is never preserved. Global speech
  cooldown is retained. Failed destinations supply no departure cue.
- FADE uses all16channels/priority191, nonlooping (`41e71c`, `440273`), when
  returning from the captured game menu to gameplay and on a leading-`*` ending.
  Retry/End Game disable the original menu gate. It is not added to every Lift,
  ordinary pause, map/inventory, hover or debug navigation. Native pause calls
  the fade helper only in its missing-PAUSE.GAF fallback.

Clips are cached at load; ordinary cue playback performs no resource reads.
Existing FX/voice volume controls and native scheduler admission still apply.

## Live Panel Help

Native action25 toggles a nonmodal HUD annotation overlay (`434130`):13localized
labels, green palette250leader polylines and one endpoint pixel, using PANEL
font3. Energy points to the lower bar, Shield to the upper. The overlay draws
following the normal HUD/cursor; inventory, menus and modal pages exclude it.
HELP0/1000/5000ms is queued only when enabling, not disabling or redrawing.

The port's remappable default is **F3**, preserving accepted F1 information and
debug-only F2. Native physical F2 is therefore an explicit input-adapter difference.
Controller defaults remain unbound. New actions append without renumbering old
ones; existing custom F3/Page Down bindings win over absent new defaults.

208text/line calls match isolated original controls across2languages×4viewports,
using hooked text/line services and a mocked8px text advance. Four native toggles
produce two enable-only HELP requests. This verifies layout/control flow under
those hooks; actual font/raster integration is checked separately by captures.
The endpoint uses a1logical-pixel quad for desktop/GLES compatibility.

## Manual high/low speed and checkpoints

**Page Down**, remappable and initially unbound on controllers, supplies native
one-press action9 and HIGH/LOW-SPEED0/1000/2000ms commentary. See
[manual speed evidence and arithmetic](manual_speed_native.md). Active Thrust
Loss blocks toggles; pending application does not. Velocity, thrust and current
turn countdown do not reset when switching mode.

The speed HUD indicator compares live speed threshold with the class default
(`4355cb–4355ef`): inequality selects PANEL4, equality PANEL5. Pinball elasticity
is not that predicate; Pinball movement/collision/FX remain unchanged.

Schema38 adds optional World speed threshold, integer turn period and temporary
pre-XTC/Slow threshold. Readers1–37 leave it absent rather than guessing a mode
from a saved turn interval. State is created lazily on first manual input,
preserving the earlier ordinary-motion path until then. In-memory snapshots
retain active effects; disk saving restores their baseline under the existing
Surprise-reset policy. Invalid/nonfinite or incoherent state is rejected before
transactional restore. Legacy tests remove the actual new fields before deriving
v37/older payloads; captured older fixtures remain independent checks.

Native text saving emits modified Turn-timeout but omits the live threshold in
the inspected writer. Deterministic threshold persistence here is the port's
checkpoint guarantee, not exact original text-save behavior. Native living-player
Lift reuse preserves manual values: missing Destination defaults0,0, overwriting
World startup−1; explicit negative/custom reconstruction resets the new mode.
Existing authored arrival and inventory/vitals handoff are unchanged.

## NRJ-LOW scope

No Energy warning is invented.432original HUD controls select SHIELD-LOW for low
Shield; Energy is only an alive guard there. The actual voice loader requests
48sounds and leaves slot25unwritten. Neither language's NRJ recording has an
identical decoded PCM alias among126standalone WAVs. NRJ-LOW appears unused in
this executable; no supported Energy threshold was established.

## Scene mix and Audio options

The requested [balanced scene adapter](audio_pipeline.md) retains gameplay event
and native helper rules while adding output roles, headroom, smooth positioning,
selective ducking, filtered resampling and debug diagnostics. The original Audio
menu hierarchy and separate Music/FX/Voices volumes drive the corresponding
levels; added controls live under Advanced. Menu sounds stays on the Audio root.
Voice type selects English/French gameplay speech independently of HUD language.
