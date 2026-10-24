# Scene audio and balanced mixing

The application now offers an optional balanced Linux audio adapter. Gameplay
sound events, original samples, speech delays/cooldowns, weapon stages and native
channel masks remain the source of playback. This profile changes presentation;
it is not a claim about original DirectSound output. Native transfer helpers and
the legacy mix remain available. No save layout or simulation timing changes.

## Audible intent and controls

The original Audio hierarchy is restored from executable evidence: Music;
Sound FX, Voices; Quality; Frequency; Channels. Music and the combined FX/Voices
page provide independent switches and Tune Volume pages. Each volume is 0–100%
in 1% steps, with a 20-segment display, keyboard/controller adjustment and mouse
click/drag/wheel support. These existing volume preferences drive the corresponding
mixer groups in both profiles; there is no duplicate main-volume control.
Quality/Frequency retain the accepted 32-bit/48 kHz extensions.

Voice type selects English or French gameplay speech independently of HUD and
presentation language. The preference persists as `audio.voice_type`; older
configs inherit startup language until explicitly selected. Explicit
`--audio-language` overrides it for that launch. Changing packs prepares cached
replacement PCM on the main thread, preserves delayed speech deadlines and music,
and lets an already-playing line finish. Missing replacement samples reject the
change. It neither resets World nor changes saved-game state.

Menu sounds is on the main Audio page, before Advanced. The additional Advanced
entry exposes these preferences:

| Control | Default | Purpose |
| --- | --- | --- |
| Balanced mix | On | Separate effects, cues, ambience, dialogue, UI and music. Off uses legacy dynamics/admission. |
| Scene audio | On | Smooth world distance/stereo, including enemy weapons; requires Balanced mix. |
| Resampling | High quality on desktop; Linear on PortMaster | Cached filtered conversion versus the lighter legacy converter. Independent of mix profile. |
| Ambience | 50% | Motor, terrain samples and authored looping FX, relative to Sound FX volume. |
| Dialogue focus | 50% | Gentle signal-driven ducking of competing buses. Zero disables ducking. |
| FX mix level | 70% | Headroom for overlapping effects, relative to Sound FX volume. Cues keep their own level. |

UI sounds use
their own role, independent of the gameplay FX switch. Music no longer has an
extra hidden0.35 source trim: removing it adds9.12dB before output protection.
Lower FX mix level for dense combat; raise Ambience if environmental sounds feel
too distant. Adjust focus only after matching the three main volume controls.
Listening-based tuning remains necessary; this is not a loudness-normalized
replacement for the authored sounds.

## Scene perspective and offscreen sound

`scene_audio.h` uses player position as the listener, matching the renderer's
player-centered scrolling camera. Vehicle heading does not rotate left/right.
Surface X is screen horizontal; surface Y is compressed ground depth and is
scaled by2 for distance. Altitude is included separately. Positions are surface
pixels and native altitude, with distances normalized by the virtual half width.

Sources within0.25 half widths have full distance gain. A cubic smoothstep falls
to zero at3 half widths. Stereo pan follows horizontal offset and saturates at
90% to avoid complete one-ear muting of nearby offscreen sounds. Stereo-linked,
equal-power panning preserves the energy of mono native sources; authored stereo
channels are retained. Spatial gain/pan targets smooth over5ms.

World animation samples keep their authored emission/held/forget boundaries.
Terrain sources remain anchored. Enemy weapon one-shots stay at their emission
position; loops can follow the owner. Only admitted live weapon handles are
updated, not every actor/projectile. Native mode retains the original helper
and unspatialized enemy weapon route. Rendering visibility is not an extra audio
mute, but this adapter does not invent offscreen events omitted by producers.

No wall occlusion, room model, synthetic ambience or reverb was added. The
multi-floor2D masks do not establish acoustic rooms/materials; deriving blocking
from drawing overlap could incorrectly silence valid sounds. Such additions need
an acoustic contract, repeatable benefit and target-device cost measurements.

## Roles, priorities and dynamics

The existing16 native logical channels and their masks still bound admission;
the generic32-voice API remains. Legacy selection uses the retained first-free /
priority / shortest-remaining-time rules. The balanced profile additionally:

- Protects active dialogue from UI/FX stealing. Explicit caller stops still work.
- Limits low-priority ambience at4 active logical voices; the player's priority191
  engine feedback is exempt from rejection at this cap.
- Rejects a third equal/quieter same-clip, near-same-pan start within12ms in the
  requested native mask. Separate handles are never aliased. Cues/dialogue/UI are
  exempt; this targets phase-coherent duplicate bursts, not firing cadence.

The callback sums seven fixed buses in128-frame blocks. Effects use configurable
headroom, ambience a separate gain, and cues/dialogue retain prominence. Actual
audible dialogue gently lowers music/FX/ambience; cues lower ambience only. Music
is not ducked on every shot. Default maximum dialogue reduction is about2.2dB for
music,1.9dB for effects and4.1dB for ambience, with8ms attack/200ms recovery.
A stereo-linked0.98 safety ceiling has immediate attack and80ms recovery. It
remains a safeguard, rather than the only means of controlling busy scenes.

Voice starts/natural ends have1ms edge fades; interrupted voices use up to8 fixed
3ms tails. Explicit music replacement uses40ms fades and overlays20ms. Automatic
track succession keeps exact source-clock handoff. Gain sliders smooth over5ms;
near-zero targets snap to exact zero to avoid denormals. Pause fades held world
samples over3ms without advancing their source clocks; UI and unpaused speech
continue. Resume retains source position. These are output envelopes, not delays
inserted into the simulation or sample timeline.

## PCM integrity and real-time ownership

Original PCM8/PCM16 mono/stereo stays unchanged in memory. High quality uses
precomputed windowed-sinc phase tables:16-or-more taps for common upsampling,
ratio-scaled taps for downsampling, bounded at256.128 phases plus the endpoint
are normalized for DC gain. Very short clips fall back to cubic interpolation;
Linear retains the previous converter. No detail beyond the original source
bandwidth is created. Filter history/lookahead is read from already loaded PCM
or the primed disk ring, with no extra output delay. Source clocks remain in
original frames across output rate changes.

Filters are built on the main thread, with16 cached rate pairs per output rate;
active voices retain immutable tables. The streamed PCM ring remains65536source
frames. Consuming retains the filter history and refills stay on the disk worker.
Underruns emit silence and hold the affected source clock; they never wait for
I/O. Worker shutdown wakes its timed wait instead of waiting out a10ms sleep.

The callback allocates no memory, destroys no clips/streams, performs no file I/O,
joins no worker and takes no application mutex. Main-thread device operations use
SDL's device gate. Ownership guards keep both outgoing and rejected incoming
clips alive until after unlocking; completed fade tails are collected on the main
thread. Offline allocation/deallocation guards cover mix and queued handoff.

## Diagnostics and validation scope

The existing `--debug` information overlay shows active voices by role, rejected
requests, steals, underruns, input/output peaks, overload/clipping counts, minimum
limiter gain and callback average/max time. Counters are cumulative for the device
session; overload means pre-limiter full scale exceeded, not clipped output.
Callback time includes output conversion. The offline CPU benchmark measures the
mixer alone, and is not device FPS or a complete audio-backend latency test.

`audio_pipeline` covers spatial translation/stereo/depth/offscreen boundaries,
headroom, fades, selective duck/recovery, repeated bursts, dialogue protection,
exact mute, clock preservation, callback partition invariance, allocation guards,
frequency/image/alias controls and live SDL dummy diagnostics. `wav_stream` checks
resident/stream equality for both quality modes through refill/loop boundaries.
The opt-in `audio_pipeline_app_audit` exercises actual resources and App controls,
with a controlled World/cue schedule and repeatable captures. It checks persistent
World isolation, profile continuation, movement, pause/resume, level transition
and preference persistence. It is not an original Windows listening playthrough.

Detailed source manifests, numerical results, CPU conditions and proprietary
local-only A/B audio are retained under
`work/quirk-audit/audio-pipeline-20261024/`. Physical listening, full campaign
coverage and real hardware latency remain unverified. Native H700 checks are
described below; they do not establish physical listening or whole-game FPS.


## Handheld validation boundary (2026-10-25)

Native H700 firmware SDL/ALSA/GLES checks exercise the Audio controls, queued
speech/music isolation, both scene mix profiles, listener movement, pause/resume,
map transitions and copied-save continuation. Headless dummy-backend unit tests
use a private test-only SDL because the firmware build exposes ALSA only; the
virtual-pad fixture ignores the built-in pad for its simulated input. Neither
fixture override is used by the installed game or live UI/audio checks.

A resident mixer probe on H700 uses authored Gatling, water and explosion
samples:16native FX voices plus one looping music voice,48kHz/512frames,
200warmup and2000measured blocks. Linear p50/p95/p99/max is approximately
1.70/1.73/1.88/2.27ms; filtered7.74/7.88/8.21/11.08ms. This excludes device
conversion, disk refill and backend latency; it is not FPS or a matched
before/after comparison. The10.67ms block budget supports retaining Linear as
the handheld default. Filtered remains an explicit option. Initial environment:
ondemand governor,1512MHz,59.4C. Physical listening and broader campaign stress
remain separate acceptance boundaries.
