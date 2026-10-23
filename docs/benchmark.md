# Vurus benchmark

`vurus-bench` is a separate executable using the game's World, animation,
audio and renderer. It runs seven scripted encounters automatically and ends with
a score and a small map/FPS/nearby-enemy/RSS table. Game saves and the
leaderboard are never read or written. Video preferences saved by the in-game menus
are read only. Players supply their own existing game data.

## From the game menu

Choose **Extras → Benchmark → Default** or **User Settings**. The two choices use
exactly the standalone benchmark's fixed preset and saved menu-video settings,
respectively; both disable VSync and the FPS cap for the run. Esc/B returns from
the submenu to Extras. The game releases its active runtime before the benchmark,
so a suspended game does not add renderer/audio work or inflate benchmark RSS.

The same seven scenes, score and HTML/JSON reports are used. Results stay visible
until a fresh A/B/Start/Enter/Esc press returns to the main menu; closing the window
quits instead. Saves, leaderboard and saved options are untouched, and the main
menu resumes with your normal settings. No separate benchmark executable is needed
for this menu entry.

## Run a development or future release build

The benchmark is built from current source with `VURUS_BUILD_BENCHMARK=ON`.
Desktop and PortMaster CI now include it in future game packages; older releases
such as desktop v0.1.5 and PortMaster alpha.4 do not contain it. No additional
game data is shipped. Import your disc with the normal game first; the benchmark
accepts existing resources, not disc images.

On Linux/macOS, run `./vurus-bench` beside the normal game and its imported
`data/` folder. On Windows, run `.\vurus-bench.exe` from that directory; a Visual
Studio source build puts it in `build/Release/`. Use `--data /path/to/resources`
if your imported files are elsewhere. From the Linux source build, for example:

```sh
./build/vurus-bench --data /path/to/existing/resources
./build/vurus-bench --default-settings --data /path/to/existing/resources
```

Allow a few minutes. The benchmark checks performance; CTest remains the
correctness check. Compare matching engine/data/video settings and close other
heavy applications rather than treating one score as a pass/fail threshold.

## On a PortMaster handheld

New `vurus-portmaster.zip` game packages include **Vurus Benchmark.sh** and
`vurus/vurus-bench.aarch64`. Extract both launchers and the `vurus/` folder into
the normal Ports directory, preserving your imported data and user files.
The optional separate `vurus-benchmark-portmaster.zip` add-on installs only the
benchmark beside an existing game. Import the original disc with the normal game first, then
launch **Vurus Benchmark** from the firmware menu. Allow a few minutes.

Esc/controller B cancels. The results screen stays open until a fresh A, B,
Start or Enter press. Completed runs save a new folder beside the binary:

```text
benchmark-results/YYYYMMDD-HHMMSS/
  report.html
  report.json
```

Copy that folder from the device and open `report.html` in a browser. It works
entirely offline, with two compact tables and a link to the JSON. JSON includes
hardware/build details, source-map fingerprints, populations and timing means,
median/p95/p99/max. Existing report folders are preserved.

## Menu settings and the fixed preset

Normal launches read `options.cfg` beside the benchmark—the settings saved by the
normal game's menus. They use its resolution, window mode, brightness, sprite
filtering, HUD aspect, shadows, reactor, ground/smoke, vignette, CRT variants and
advanced scene effects (FXAA/SMAA, bloom, sharpen). **VSync is always off and FPS
is always uncapped.** Unsupported desktop post-effects stay off on GLES, just as
in the normal handheld game. Audio, difficulty, input and player/profile settings
are not inherited. The file is never changed. With no file, game video defaults
are used (including the platform's default resolution).

Use `--default-settings` to ignore the menu file and reproduce the original
benchmark preset: 640×480 windowed, neutral brightness, automatic HUD aspect,
nearest sprites, shadows/reactor/vignette on, ground details and desktop
post-effects off. For example:

```sh
./vurus-bench --default-settings --data /path/to/existing/resources
```

If the desktop benchmark is in a separate folder, place it beside the normal
game or pass `--options-file /path/to/game/options.cfg`. Relative working
directories do not affect the default settings path. A malformed/unreadable
settings file fails with an error; `--default-settings` bypasses it.

Both modes receive scores when the workload completes normally. The final screen
labels **MENU** or **DEFAULT** plus the actual output resolution. Reports record
all effective visual settings, requested/actual size and the preset; compare
scores only when those settings and actual resolution match. JSON schema3 records
these fields plus the Burner crossfire workload and overlapping-fire counters.

The benchmark window is resizable, like the game. Fixed-display backends that
refuse the requested window size can use their existing native window instead;
reports record the effective dimensions without changing the menu settings file.
On a 480×320 handheld, use **User Settings** for a native-resolution benchmark.
Scoring still requires the driver to honor VSync off. The
**Default** preset still requests 640×480; if the display cannot provide that,
the run completes as nonstandard without a comparable default score. Resizing
the window during measurement also marks the run nonstandard. Resizing the
finished results screen does not change the saved report.

## Score and workload

**1,000 points = 60 complete benchmark steps per second.** Each scene contributes
equally: score = round(1000 × geometric-mean scene throughput / 60), using each
scene's mean measured frame time. A step is one fixed 1/60-second World update,
animation/audio maintenance and one full rendered/presented frame. This is
benchmark throughput, not the normal application's catch-up-scheduled gameplay FPS.

Protocol `vurus-bench-2`: 120 warmup + 1,200 measured steps per scene,
Normal difficulty, uncapped/VSync off, music off, real effects/voices at 70%
master volume. Visual settings come from the game menu unless `--default-settings`
is supplied. Player invulnerability, held fire, facing changes every 120 steps and ammo/weapon changes
every 600 steps are benchmark controls only. CPU/T3P2 also visits generator stations.

| Scene | Start position |
| --- | --- |
| CAVE/T1C2 | 5230,1080 |
| CPU/T3P2 | First authored generator +100,+50; next station every600steps |
| UNDERGROUND/T2U1 crowd | 4870,2180 |
| UNDERGROUND/T2U1 start | 3968,348 |
| HITEK/T1H1 Burner area | 3670,2570 |
| HITEK/T1H1 crowd | 2166,1107 |
| HITEK/T2H2 Burner crossfire | 4571,2841 |

The added Burner crossfire keeps the player stationary without firing; enemy
indestructibility sustains the authored attackers, using their normal AI and
weapons. Two Burners fire together for most of this encounter, with brief periods
of three. These are benchmark-only controls. JSON records the maximum number of
Burners firing in the preceding100ms and measured steps with at least two;
missing crossfire makes a run unscored. The other six workloads are unchanged.
Protocol2 scores include this seventh scene and must not be compared directly
with the earlier six-scene `vurus-bench-1` scores.

Loading, scripted controls, event polling, separate population/RSS sampling and
report output are outside timed frames. The progress/FPS overlay is included in
render/present. CPU total includes World, animation and audio; rendering includes
driver/swap waits. Nearby counts are alive enemies within1,000world units at spawn,
not active/on-screen counts. RSS is Linux process memory, and sampled peaks may
miss short spikes. Other platforms report unavailable RSS as N/A.

Compare the same protocol/settings/map fingerprints. Use the same engine build
for hardware comparisons: AI changes can alter workload even on the same map.
Background tasks, thermal state and driver pacing still affect the score. Do not
run the normal game or a build concurrently. Cancelled, incomplete, custom-sample,
silent/test-audio, incorrect fixed-preset resolution or driver-VSync runs retain
reports but are unscored. Scores are local only; no network or leaderboard upload occurs.

## Build and command line

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVURUS_BUILD_BENCHMARK=ON
cmake --build build --target vurus-bench -j4
./build/vurus-bench --data /path/to/existing/resources
sh tools/package_benchmark.sh build dist/benchmark linux
```

For the existing ARM64 PortMaster builder, also set `-DVYRUZ_GLES=ON
-DVYRUZ_PORTMASTER=ON`; package inside that builder with
`sh tools/package_benchmark.sh BUILD_DIR OUTPUT_DIR portmaster` to include its
static runtime notices and glibc check. The benchmark remains opt-in for source builds and has a standalone add-on
package; configured game-release builds include it beside the normal engine.

`--output DIR` selects the report-parent directory; defaults are independent of
the working directory. `--frames N` shortens validation runs (unscored unless the
standard 1,200 count is used). `--no-wait` exits after report generation. `--shot
RESULT.ppm` captures the final table before swapping. `--help` and `--licenses`
work without loading resources. `--data` accepts an existing resource directory;
the benchmark never imports or changes original media.
