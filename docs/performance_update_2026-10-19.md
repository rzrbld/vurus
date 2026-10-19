# Memory and performance update — 2026-10-19

These results describe the optimization series included in desktop v0.1.3 and
the PortMaster Alpha Preview. CI builds and applicable tests passed on Linux,
Windows, both macOS architectures and ARM64 GLES. Hardware testing remains
separate from these host measurements.

## Observed memory use

The user reports ordinary gameplay RSS falling from approximately **500–700 MB
to 170–250 MB** over this optimization series. These are observations across
sessions, not a matched benchmark: map, resolution, effects, music and session
length can change the result. They are not minimum RAM requirements. RSS includes
resident process pages, including shared libraries; it is not total system or
GPU memory. On shared-memory GPUs, graphics allocations also compete for RAM.

## Controlled measurements

Each row is a separate experiment. Savings must not be added together or treated
as one before/after run. MiB means 1,048,576 bytes; byte counts are exact where shown.

| Change and workload | Before | After | Scope |
| --- | ---: | ---: | --- |
| Streamed music, full-media host probe after a second-level save | 569.34 MiB RSS | 326.62 MiB RSS | Comparable workload, but randomized track selection; not identical track IDs |
| Shared immutable properties, fresh world snapshots | 12–16 MiB | 2.5–3.5 MiB | Incremental allocations, not whole-process RSS |
| Compact sprite pixels, 177 decoded GAF banks | 57.15 MiB | 28.58 MiB | Corpus CPU pixel payload, not all resident at once |
| Compact pixels, three-map render probes | — | 13.57–20.44 MiB less RSS | 640×480, software rendering, dummy audio, music off |
| Recycled transient records, final level after 10,800 steps | 5,024 records | 1,789 records | Same 848 live records, including effects; about 13.2 MiB less host RSS |
| Same transient-record checkpoint, compressed | 2,025,657 bytes | 851,311 bytes | One controlled save, not a universal compression ratio |
| Current-level save retention, three-map progression | 2,281,462 bytes | 1,245,739 bytes | Same progression; completed worlds no longer saved |
| Filter construction, synthetic 512×512 sprite | 3.37 MiB | 2.03 MiB | Peak requested temporary heap, not process RSS or GPU memory |

CD music now uses bounded streaming buffers rather than whole resident tracks.
The original AVI intro is streamed without conversion, with a **512 KiB video
read-ahead queue** and **256 KiB audio ring**, plus frame/decoder/index storage.
Short effects and voices remain resident: the English cache across 25 maps was
**4.83–4.93 MiB** of PCM payload, avoiding playback-time disk access for combat.

## Archive loading follow-up

Archive initialization now reads headers/directories and retains a synchronized
file handle, avoiding whole-archive temporary allocation and per-entry reopening.
For the shipped data archive, requested initialization data fell from 51,887,890
bytes to 20,415 bytes (stream/OS read-ahead can read more). No payload cache or mmap
is introduced; loose files still open and close normally.

A standalone reader benchmark, five warm-cache host sweeps, measured median open
time **27.14→0.039 ms**, summed entry-read time **6.26→4.05 ms**, and process peak
RSS **54.8→7.2 MiB**. All 477 payload hashes combined matched; 22 slide entries also
matched in a separate sweep. This is not whole-game RSS or complete level-load
time, and no cold SD-card speedup has been measured. All 25 maps passed resource
validation; concurrent reads through copied handles are covered by regression tests.

## First-run extraction follow-up

ISO/BIN game resources now extract in 32-sector chunks, stripping raw-sector
wrappers in memory with a bounded 74 KiB buffer. Progress and cancellation retain
64 KiB output intervals. CD music ripping already used sequential chunks.

A synthetic 64 MiB + 17 byte resource, five warm-host samples and reverse-order repeats,
measured median ISO extraction **117–128→27–35 ms**, and raw BIN extraction
**120–124→26–37 ms**. All output/progress hashes match. This is roughly 3–5× for
that extraction step, not the complete import: checksums, durable writes and
publication are excluded. SD-card benefit remains unmeasured. Full-import tests
cover ISO, MODE1/MODE2, nonzero track starts, final partial sectors and cancellation.

## CPU timing for transient record reuse

Serial Linux host runs on CPU/T3P2, 35 authored generators, god mode, continuous
fire and deterministic position/weapon changes. 10,800 fixed 60 Hz steps;
first 120 excluded. No rendering, audio or tracing in timed runs. Before/after
final live populations match. This measures CPU work, not device FPS.

| Work per step, milliseconds | Before median / p95 / p99 / max | After median / p95 / p99 / max |
| --- | --- | --- |
| World simulation | 0.1859 / 0.3657 / 0.5121 / 10.5907 | 0.1749 / 0.3514 / 0.4949 / 5.3419 |
| Animation update | 0.1057 / 0.1341 / 0.1446 / 3.2867 | 0.0902 / 0.1052 / 0.1144 / 0.6186 |

These are single matched host runs; maxima include scheduling noise. They do not
prove elimination of every hitch or resolution of handheld late-level slowdown.
A longer 36,000-step run retained 2,143 records with 907 live; persistent objects
and enemy history are deliberately preserved, so total records are not constant.

## Rendering and fidelity

Sprite/terrain CPU storage retains original 16-bit pixels, expanding on upload.
Filtering keeps its color-key edge treatment and original/filtered GPU copies.
The scene sprite cache now accounts for uploaded base/filter/mip payload instead
of a blanket 3× estimate. Its 32 MiB/1,024-entry budget allows the current visible
working set to exceed the limit; it is not a total GPU-memory cap.

A desktop client-vertex-array prototype produced identical captured frames but
no consistent performance gain on Mesa llvmpipe. It was not adopted. Native
resolution, post-effect order and existing renderer remain intact. Post-process
memory still grows with drawable resolution. The GLES path already uses buffered
draw submission; this desktop experiment is not evidence of handheld speedups.

Local validation includes **154/154 automated tests**, exact sprite/filter pixel
comparisons, 117 cached/recreated rendering comparisons across three maps and
all filtering modes, and scoped save/animation continuation audits. These are
regression checks, not exhaustive original-game parity or full hardware acceptance.

Use `--debug` for FPS/frame-time and the Linux RSS/PSS memory breakdown, or
`--profile-frames 1200` for timing reports. Compare the same map, population,
resolution, effects and frame cap. Steam Deck and PortMaster long-session,
level-transition and slow-storage testing remains separate from these host results.
