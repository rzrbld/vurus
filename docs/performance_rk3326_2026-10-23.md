# RK3326 / ArkOS benchmark — 2026-10-23

The Extras-menu benchmark now completes on the fixed 480×320 display instead of
aborting during video initialization and returning to the game. Both the game-menu
and standalone runners use the same repaired implementation. Source: [599f3bd](https://github.com/rzrbld/vurus/commit/599f3bd95dccf4b533fbba64802b9f3f4e68039e).

## Failure and repair

The game successfully created a 480×320 SDL/GLES window, but the benchmark then
required the requested 640×480 size to apply exactly. ArkOS refused that resize;
`Display size unavailable` caused a handled benchmark failure and a return to the
game. This was not evidence of a gameplay crash or missing resources.

Benchmark windows now use the game's resizable and high-DPI flags. If a windowed
size request fails, the runner can retry using the valid existing window dimensions,
then report the effective size. It does not alter the user's options file or relax
unsupported fullscreen-mode checks. Resizing during the workload marks the result
nonstandard; resizing the completed results view redraws it without altering the
saved report. The [benchmark manual](benchmark.md) describes preset/score behavior.

## Device and method

- User-identified RK3326 handheld, ArkOS, Mali-G31. The firmware's device-tree model
  string is `Hardkernel ODROID-GO2`; no more specific physical model is inferred.
- ARM64, glibc 2.30; GLES 3.2 driver
  `v1.r13p0-01rel0.8747d14aec16ec21ac2b2da252c4ec5f`, real ALSA audio.
- Development build `arkos-fixed-display-20261023`; renderer source includes the
  accepted camera-cell optimization. No World, AI, collision, save-layout or shader change.
- Actual window and drawable 480×320. User-menu video preset, nearest filtering,
  shadows/reactor/vignette/ground details on; music off, cap 0, VSync requested off.
  The driver still reports **swap interval 1**. The report therefore correctly
  marks this run **nonstandard and unscored**, with `driver VSync enabled`.
- Seven scenes, 120 warmup + 1,200 measured fixed 1/60-second tick/frame steps each.
  Normal difficulty, benchmark invulnerability, held player fire except Burner;
  Burner retains ordinary AI/fire cadence with test-only enemy indestructibility.
- Automated keyboard events select Extras → Benchmark → User Settings; private
  SDL hooks capture the results after measurement, confirm it, and observe the
  return to the main application window. These hooks are absent from installed
  engines. The frontend was temporarily suspended for the SSH test and restored.
- Copied options and isolated save paths. Both copied and normal menu settings
  hashes remain unchanged; all 34 checked normal non-engine/non-data files survive
  installation. Old engines were backed up. Original BIN/CUE and imported data
  were preserved; the user supplied those files directly on the SD card.

The H700 report uses another build, 640×480, trilinear filtering, VSync off and
different final populations. These results are **not a controlled H700/RK3326 CPU
or hardware speed comparison**. They also do not establish hitch-free gameplay.

## Per-map throughput and memory

FPS = 1,000 / mean complete-step milliseconds, not ordinary gameplay FPS. Nearby
means alive enemies within 1,000 world units at spawn, not the visible/awake count.
RSS is sampled process resident memory and can miss short peaks; it excludes total
system/GPU memory. CPU/T3P2 tours generator stations; coordinates are its starting point.

| Map / start | Bench FPS | Nearby enemies | Initial / final rows | Final live | Peak projectiles | Sampled RSS MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CAVE/T1C2 · 5230,1080 | 38.31 | 30 | 1129 / 1334 | 1254 | 17 | 128.13 |
| CPU/T3P2 · 3523,2763 | 49.36 | 11 | 754 / 1109 | 865 | 13 | 115.76 |
| UNDERGROUND/T2U1 · 4870,2180 | 43.10 | 39 | 1020 / 1131 | 1066 | 13 | 130.23 |
| UNDERGROUND/T2U1 · 3968,348 | 50.17 | 8 | 1020 / 1079 | 1020 | 7 | 130.38 |
| HITEK/T1H1 · 3670,2570 | 44.13 | 18 | 1102 / 1225 | 1161 | 8 | 126.71 |
| HITEK/T1H1 · 2166,1107 | 46.07 | 19 | 1102 / 1250 | 1155 | 20 | 128.27 |
| HITEK/T2H2 · 4571,2841 | 33.33 | 30 | 1342 / 1442 | 1407 | 20 | 127.50 |

## Timing distributions

Milliseconds; CPU stages are per tick, render/present and complete step are per
frame. CPU total includes the earlier CPU stages. Percentiles are independent
and should not be added. Render/present includes CPU composition, submission and
swap; these intervals do not isolate GPU execution time.

| Map / start | Stage | Mean | Median | p95 | p99 | Maximum |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| CAVE/T1C2 · 5230,1080 | World | 4.900 | 4.379 | 8.975 | 11.293 | 16.444 |
| CAVE/T1C2 · 5230,1080 | Animation | 2.407 | 2.264 | 2.661 | 7.920 | 8.376 |
| CAVE/T1C2 · 5230,1080 | Audio | 0.502 | 0.440 | 0.656 | 2.201 | 6.305 |
| CAVE/T1C2 · 5230,1080 | CPU update total | 8.756 | 8.099 | 13.399 | 15.255 | 20.371 |
| CAVE/T1C2 · 5230,1080 | Render / present | 17.347 | 16.513 | 22.790 | 24.543 | 27.338 |
| CAVE/T1C2 · 5230,1080 | Complete step | 26.103 | 25.082 | 32.072 | 35.229 | 38.324 |
| CPU/T3P2 · 3523,2763 | World | 4.454 | 4.562 | 5.984 | 6.885 | 10.439 |
| CPU/T3P2 · 3523,2763 | Animation | 2.176 | 2.197 | 2.575 | 2.734 | 3.797 |
| CPU/T3P2 · 3523,2763 | Audio | 0.736 | 0.519 | 2.103 | 2.907 | 3.368 |
| CPU/T3P2 · 3523,2763 | CPU update total | 8.056 | 8.246 | 10.349 | 11.098 | 14.086 |
| CPU/T3P2 · 3523,2763 | Render / present | 12.205 | 11.878 | 14.677 | 15.781 | 17.092 |
| CPU/T3P2 · 3523,2763 | Complete step | 20.260 | 19.952 | 23.940 | 24.952 | 27.323 |
| UNDERGROUND/T2U1 · 4870,2180 | World | 3.892 | 3.681 | 5.684 | 7.076 | 11.786 |
| UNDERGROUND/T2U1 · 4870,2180 | Animation | 2.098 | 1.986 | 3.359 | 4.443 | 5.448 |
| UNDERGROUND/T2U1 · 4870,2180 | Audio | 0.435 | 0.407 | 0.516 | 0.724 | 3.026 |
| UNDERGROUND/T2U1 · 4870,2180 | CPU update total | 7.235 | 7.025 | 9.397 | 10.824 | 15.245 |
| UNDERGROUND/T2U1 · 4870,2180 | Render / present | 15.967 | 16.000 | 17.693 | 18.478 | 29.895 |
| UNDERGROUND/T2U1 · 4870,2180 | Complete step | 23.202 | 23.131 | 25.125 | 27.281 | 36.711 |
| UNDERGROUND/T2U1 · 3968,348 | World | 2.414 | 2.269 | 3.273 | 5.300 | 7.450 |
| UNDERGROUND/T2U1 · 3968,348 | Animation | 1.930 | 1.916 | 2.211 | 2.384 | 2.738 |
| UNDERGROUND/T2U1 · 3968,348 | Audio | 0.427 | 0.384 | 0.637 | 1.334 | 1.899 |
| UNDERGROUND/T2U1 · 3968,348 | CPU update total | 5.510 | 5.350 | 6.716 | 8.466 | 10.712 |
| UNDERGROUND/T2U1 · 3968,348 | Render / present | 14.422 | 13.763 | 18.309 | 19.184 | 24.825 |
| UNDERGROUND/T2U1 · 3968,348 | Complete step | 19.932 | 19.388 | 23.808 | 24.355 | 30.312 |
| HITEK/T1H1 · 3670,2570 | World | 4.414 | 4.263 | 6.831 | 8.080 | 10.082 |
| HITEK/T1H1 · 3670,2570 | Animation | 2.174 | 2.127 | 2.628 | 2.907 | 3.443 |
| HITEK/T1H1 · 3670,2570 | Audio | 0.445 | 0.397 | 0.622 | 1.192 | 3.750 |
| HITEK/T1H1 · 3670,2570 | CPU update total | 7.886 | 7.747 | 10.561 | 12.126 | 13.916 |
| HITEK/T1H1 · 3670,2570 | Render / present | 14.774 | 14.628 | 16.827 | 17.920 | 18.699 |
| HITEK/T1H1 · 3670,2570 | Complete step | 22.660 | 22.630 | 25.771 | 27.951 | 30.501 |
| HITEK/T1H1 · 2166,1107 | World | 3.745 | 3.597 | 5.213 | 6.151 | 11.654 |
| HITEK/T1H1 · 2166,1107 | Animation | 2.242 | 2.226 | 2.624 | 2.830 | 3.453 |
| HITEK/T1H1 · 2166,1107 | Audio | 0.709 | 0.419 | 2.251 | 3.082 | 3.502 |
| HITEK/T1H1 · 2166,1107 | CPU update total | 7.546 | 7.276 | 9.602 | 10.552 | 15.532 |
| HITEK/T1H1 · 2166,1107 | Render / present | 14.161 | 13.865 | 16.540 | 17.663 | 28.919 |
| HITEK/T1H1 · 2166,1107 | Complete step | 21.708 | 21.613 | 24.161 | 26.529 | 36.481 |
| HITEK/T2H2 · 4571,2841 | World | 7.540 | 7.356 | 10.154 | 11.406 | 26.954 |
| HITEK/T2H2 · 4571,2841 | Animation | 2.818 | 2.779 | 3.168 | 3.501 | 16.026 |
| HITEK/T2H2 · 4571,2841 | Audio | 0.841 | 0.657 | 1.900 | 2.522 | 35.801 |
| HITEK/T2H2 · 4571,2841 | CPU update total | 12.246 | 11.995 | 15.078 | 16.901 | 46.623 |
| HITEK/T2H2 · 4571,2841 | Render / present | 17.757 | 17.505 | 18.446 | 29.988 | 47.054 |
| HITEK/T2H2 · 4571,2841 | Complete step | 30.003 | 29.524 | 33.302 | 48.631 | 65.532 |

## Validation and limits

The exact publication candidate builds both desktop entry points and passes the
three focused benchmark-report/window-mode tests. A separate real-SDL offscreen
fixture resizes during warmup: all seven scenes finish, the window is resizable,
and the report suppresses a comparable score because the drawable changed.

On ArkOS, the full User Settings menu run completes seven scenes, shows its
results, accepts confirmation and returns to the main application. A separate
standalone Default smoke test completes all seven scenes with one measured step
each. That shortened run validates startup/completion only; it is not a performance
sample. Default still requests 640×480; native fallback preserves the existing
nonstandard/default-resolution rule. Both new engines pass the glibc 2.30 limit,
help checks and installation hash verification.

The 480×320 text remains small; the user accepts it for tests. This repair does
not redesign fonts or layout, override firmware VSync, establish a matched hardware
speedup or validate every fullscreen backend. The repaired game and standalone benchmark were installed separately; these are
development-build results rather than measurements of a published binary release.

## Actual results screen and raw data

![RK3326 native benchmark results](images/rk3326-benchmark-results-2026-10-23.png)

Actual 480×320 results screen, captured after measurement and converted losslessly
from PPM to PNG. Decoded pixels were checked identical. It is not a selected
instantaneous gameplay-FPS frame.

- [Full User Settings JSON](benchmarks/rk3326-2026-10-23-user.json): effective
  settings, actual output, firmware strings, map fingerprints, populations,
  overlapping Burner fire, and all timing distributions.
- [H700 report](performance_update_2026-10-23.md): separate controlled renderer
  experiment and device results; retained without another measurement.
