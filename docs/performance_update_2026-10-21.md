# H700 performance update — 2026-10-21

Development source [`d23b515`](https://github.com/rzrbld/vurus/commit/d23b515ff69ff2b30ae399d65ad7943444f5564f),
compared with [`7756f9d`](https://github.com/rzrbld/vurus/commit/7756f9d1fb000dc4da18a64d3aaf460dbc51aa95).
These measurements follow seven gameplay/AI fixes; they do not describe the
older released packages or a new renderer change. The four launcher names identify
**map spots with mixed enemies**, not the classes responsible for the cost.
The [October 19 memory report](performance_update_2026-10-19.md) retains the earlier
memory experiments and release checkpoint.

## Device and procedure

Actual Anbernic RG40XX H, Allwinner H700 / Mali-G31, 1 GB RAM, muOS
2502.0 PIXIE, SDL 2.28.5, proprietary GLES driver r20p0. Normal Ports launcher and
ALSA FX/voices, music off; 640×480 nearest, shadows/reactor/vignette on,
ground details off, uncapped and VSync off. Both builds use Clang 18/libc++, O3,
FP contraction off and identical renderer/audio sources. No concurrent builds
or tests ran during device timing. Governor stayed ondemand. Boundary samples
showed 936–1512 MHz and 57.9–63.2°C; they are not continuous clock traces.

Four ordinary-App encounters, baseline/current/current/baseline order per spot,
two runs per revision, 120 warmup + 1,200 measured frames per run. Sixteen valid
profiles in total. A standing invulnerable player allows enemies to attack; no
held player fire. Native ARM-generated isolated checkpoints are identical inputs
for both revisions. Original maps and enemies are unchanged; two private empty
briefing-text overrides bypass modal briefings. Briefing-paused trials and failed
host-generated RNG-text checkpoint loads were excluded. Every accepted profile
has live World work and requested cap/VSync off.

## Encounter context and RSS

| Map / position | Nearby enemies at spawn | Startup RSS |
| --- | --- | ---: |
| UNDERGROUND/T2U1 · 4870,2180 | 18 Cops, 20 Turrets, 1 Roller (39 total) | 116.0 MiB |
| UNDERGROUND/T2U1 · 3968,348 | 2 Cops, 2 Turrets, 4 Spiders (8 total) | 117.6 MiB |
| HITEK/T1H1 · 3670,2570 | 7 Cops, 6 Turrets, 3 Burners, 2 Helis (18 total) | 130.9 MiB |
| HITEK/T1H1 · 2166,1107 | 6 Cops, 7 Turrets, 3 Burners, 3 Helis (19 total) | 130.9 MiB |

Nearby means alive enemies within a 1,000-world-unit radius of the spawn;
not visible/awake counts or per-function visits. RSS was read from the debug
HUD in separate short **baseline `7756f9d` startup captures**, rounded to whole
MiB in the README. It was not sampled in the timed App runs, does not establish
current-build peak/steady memory, and excludes total device/GPU memory. Music
was disabled and startup lacks long-session texture/FX growth; these figures
should not replace earlier long-session user observations.

## Current build: timing breakdown

Cells are **p50 / p95 / p99 / maximum milliseconds**. Each quantile is the mean
of two per-run quantiles, not a pooled distribution; maximum is the worst run.
World/animation/CPU total are per simulation tick; render/frame are per frame.
CPU total includes World, animation, audio, input and other update work. These
nested costs and independent quantiles must not be added to reconstruct a frame;
a frame may contain multiple 60 Hz simulation ticks.

| Map / position | Metric | p50 / p95 / p99 / max (ms) |
| --- | --- | --- |
| UNDERGROUND/T2U1 · 4870,2180 | World / tick | 4.855 / 6.784 / 7.752 / 12.530 |
| UNDERGROUND/T2U1 · 4870,2180 | Animation / tick | 1.973 / 2.389 / 2.643 / 5.265 |
| UNDERGROUND/T2U1 · 4870,2180 | CPU total / tick | 8.532 / 10.351 / 11.768 / 20.190 |
| UNDERGROUND/T2U1 · 4870,2180 | Render / frame | 10.638 / 11.905 / 13.526 / 20.956 |
| UNDERGROUND/T2U1 · 4870,2180 | Whole frame | 20.093 / 28.989 / 34.061 / 45.594 |
| UNDERGROUND/T2U1 · 3968,348 | World / tick | 3.121 / 5.134 / 7.721 / 10.471 |
| UNDERGROUND/T2U1 · 3968,348 | Animation / tick | 1.593 / 2.146 / 2.471 / 3.875 |
| UNDERGROUND/T2U1 · 3968,348 | CPU total / tick | 6.099 / 8.639 / 11.116 / 14.840 |
| UNDERGROUND/T2U1 · 3968,348 | Render / frame | 9.628 / 13.535 / 18.809 / 25.782 |
| UNDERGROUND/T2U1 · 3968,348 | Whole frame | 16.660 / 20.679 / 28.180 / 32.898 |
| HITEK/T1H1 · 3670,2570 | World / tick | 6.553 / 12.370 / 14.178 / 25.050 |
| HITEK/T1H1 · 3670,2570 | Animation / tick | 1.816 / 2.133 / 2.366 / 2.882 |
| HITEK/T1H1 · 3670,2570 | CPU total / tick | 9.785 / 15.390 / 17.337 / 28.364 |
| HITEK/T1H1 · 3670,2570 | Render / frame | 9.631 / 10.629 / 12.175 / 20.189 |
| HITEK/T1H1 · 3670,2570 | Whole frame | 23.822 / 37.930 / 43.498 / 61.434 |
| HITEK/T1H1 · 2166,1107 | World / tick | 6.835 / 9.567 / 11.163 / 15.644 |
| HITEK/T1H1 · 2166,1107 | Animation / tick | 1.969 / 2.427 / 2.654 / 5.318 |
| HITEK/T1H1 · 2166,1107 | CPU total / tick | 10.325 / 13.288 / 15.254 / 21.115 |
| HITEK/T1H1 · 2166,1107 | Render / frame | 10.162 / 10.979 / 12.951 / 21.021 |
| HITEK/T1H1 · 2166,1107 | Whole frame | 29.593 / 35.040 / 42.559 / 59.681 |

README FPS = 1,000 divided by the mean of two run-median frame times; it is
neither arithmetic mean FPS nor a whole-map average. The Spider-start median
near 16.67 ms is a practical plateau even with requested VSync/cap off; it does
not establish that Spider navigation is free. Rendering includes draw preparation,
driver submission and swap, so it is not a pure GPU measurement.

## Before/after and workload

| Spot | Baseline frame p50 | Current frame p50 | Change | Baseline/current World ticks per run | Final record rows | Dropped ticks |
| --- | ---: | ---: | ---: | --- | ---: | ---: |
| UNDERGROUND/T2U1 · 4870,2180 | 19.950 ms | 20.093 ms | +0.71% | 1582/1607 → 1605/1612 | 1099 | 0 |
| UNDERGROUND/T2U1 · 3968,348 | 16.666 ms | 16.660 ms | -0.04% | 1219/1223 → 1228/1227 | 1020 | 0 |
| HITEK/T1H1 · 3670,2570 | 23.471 ms | 23.822 ms | +1.49% | 1800/1810 → 1814/1826 | 1126 | 0 |
| HITEK/T1H1 · 2166,1107 | 29.490 ms | 29.593 ms | +0.35% | 2006/1986 → 2020/1996 | 1169 | 0 |

No large median loss in these windows. At the Burner-start spot, both current
frame medians (23.894/23.749 ms) exceed both baselines (23.305/23.638 ms), with
World median about 2% higher. This is a small consistent shift, not proven noise.
Tails are mixed: current frame maxima are 45.594/32.898/61.434/59.681 ms in table
order; baseline maxima were 37.656/42.727/59.151/54.657 ms. Spider frame p99 rose
25.399→28.180 ms. These measurements do not prove hitch-free play.

All 16 profiles dropped zero ticks. Final row totals match at each spot, but
catch-up schedules, simulated tick counts and live combat windows differ. Equal
row totals do not imply equal live workloads; these are practical application
profiles, not a deterministic equal-work comparison or per-enemy attribution.

## Separate fixed-step combat controls

Eight further H700 processes used CAVE/T1C2 (5230,1080) and CPU/T3P2 (moving
between authored generator stations), same balanced order and warmup/sample
counts. One fixed 1/60 s step and one rendered frame per iteration, God/ammo
controls, held fire and periodic weapon/facing changes. This is a controlled
probe rather than the normal App catch-up schedule or displayed FPS. Population
traces match exactly across and within revisions. CAVE had 1,252–1,538 rows and
1,165–1,473 live actors; CPU had 783–1,148 rows and 767–899 live, ending with 45
generated records. Each cell pools 2,400 measured samples per revision/map.

| Map / metric | Baseline p50 / p95 / p99 / max (ms) | Current p50 / p95 / p99 / max (ms) |
| --- | --- | --- |
| CAVE / world | 6.292 / 9.673 / 14.968 / 21.975 | 6.347 / 9.649 / 14.945 / 19.403 |
| CAVE / animation | 2.277 / 2.799 / 3.128 / 5.919 | 2.283 / 2.780 / 3.115 / 4.904 |
| CAVE / audio | 0.430 / 0.962 / 1.206 / 1.639 | 0.431 / 0.957 / 1.198 / 1.623 |
| CAVE / update | 10.115 / 13.689 / 18.559 / 25.541 | 10.139 / 13.593 / 18.353 / 23.337 |
| CAVE / render | 10.445 / 12.876 / 14.834 / 23.737 | 10.508 / 12.598 / 14.680 / 23.492 |
| CAVE / frame | 20.593 / 26.554 / 30.352 / 38.048 | 20.655 / 26.236 / 30.360 / 37.662 |
| CPU / world | 5.807 / 7.372 / 8.860 / 14.433 | 5.770 / 7.468 / 8.559 / 21.337 |
| CPU / animation | 1.968 / 2.402 / 2.646 / 5.222 | 1.960 / 2.400 / 2.625 / 5.286 |
| CPU / audio | 0.469 / 0.845 / 1.136 / 1.597 | 0.466 / 0.836 / 1.147 / 2.399 |
| CPU / update | 9.097 / 10.886 / 12.559 / 20.048 | 9.038 / 10.945 / 12.374 / 24.702 |
| CPU / render | 8.548 / 11.695 / 14.867 / 24.867 | 8.522 / 11.919 / 16.351 / 25.220 |
| CPU / frame | 17.551 / 20.853 / 26.487 / 36.952 | 17.643 / 20.693 / 26.597 / 35.239 |

Fixed-step frame medians changed +0.30%/+0.52%, World +0.88%/−0.64%. The CPU
current World maximum (21.337 ms) included only 12.060 ms of thread CPU; the other
current run replayed that event at 11.188 ms wall / 10.945 ms thread CPU. The
worst wall spike was not repeated, but its exact cause remains unattributed.

No gameplay, serialization or renderer optimization was made for this benchmark.
All 19 normal handheld engine/launcher/settings/score/save/preview hashes stayed
unchanged, and temporary device files were removed after receipt collection.
These are port-on-device results, not original Windows runtime comparisons or
full-campaign performance guarantees. The next requested profiling locations
are the complete encounters at T2U1 (4870,2180) and T1H1 (2166,1107).

## README benchmark image

The README image is an unaltered, losslessly converted 640×480 capture from
current `d23b515`, CAVE/T1C2 (5230,1080), second fixed-step combat process on
2026-10-21, actual H700/Mali. Its debug overlay reads 40.1 FPS and 141.1 MiB RSS.
This is the frequently used crowded CAVE/T1C2 spot, not CAVE/T1C1. The overlay's
momentary rate belongs to the fixed-step probe, not the ordinary-App table or
a measured whole-run average. Debug God/ammo controls and combat messages remain
visible; no overlay, gameplay pixels or performance numbers were edited.
