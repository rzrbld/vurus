Vurus Benchmark — separate on-demand performance test
===================================================

Use your own already imported Vyruz game resources. No game data is included.
The benchmark never reads/writes game saves or leaderboard files.
Video preferences saved by the game menu are read only; options.cfg is never changed.
Its scripted invulnerability and firing affect only its private in-memory World.

PortMaster / ARM64 GLES:
Extract "Vurus Benchmark.sh" and the vurus folder into the same Ports directory
as Vurus.sh. This adds vurus-bench.aarch64 beside your normal game executable.
Launch "Vurus Benchmark" from the firmware's Ports menu. Import your original
disc with the normal game first. Preserve all existing game/user files.

Linux desktop:
Run ./vurus-bench --data /path/to/existing/game/resources
Or place vurus-bench beside the game's executable and existing data/ import.

The default suite runs automatically: seven scenes, each with 120 warmup and
1,200 measured fixed steps. Allow a few minutes on an H700. Esc/controller B
cancels. At the final screen press A, B, Start or Enter to exit.

Results:
benchmark-results/YYYYMMDD-HHMMSS/report.html
benchmark-results/YYYYMMDD-HHMMSS/report.json
These live beside the benchmark executable, independently of working directory.
Each run gets a new folder. Copy the folder from the device: open report.html
in any browser, fully offline. report.json includes full timing percentiles,
populations, map fingerprints, hardware and settings. benchmark.log is the
PortMaster launch log. Use --output DIRECTORY to choose another results parent.

Score:
1,000 points = 60 complete benchmark steps/second. A step is one 1/60-second
World tick plus animation/audio and one rendered/presented frame, uncapped.
The overall score uses equal scene weights and the geometric mean of each
scene's mean throughput. Higher is faster. This is benchmark throughput, not
the normal game's catch-up-scheduled FPS. Scores compare only within the same
protocol, settings and original map data. No internet or leaderboard upload.

Video settings:
Normal launches read options.cfg beside this binary: the resolution, window mode,
brightness, filtering, HUD aspect and all video/scene effects saved by the game
menus. VSync is always OFF and FPS always UNCAPPED. GLES keeps unsupported
CRT/post-effects off, exactly like the handheld game. With no options file the
game's video defaults are used. Audio, controls, difficulty and profile are not
inherited. The options file is never changed.

--default-settings bypasses options.cfg and uses the original fixed preset:
640x480 windowed, neutral brightness, auto HUD aspect, nearest sprites,
shadows/reactor/vignette on, ground details and desktop post-effects off.
Desktop example: ./vurus-bench --default-settings --data /path/to/resources
If the benchmark lives elsewhere, --options-file /path/to/game/options.cfg reads
the game's menu settings. Invalid files fail; --default-settings bypasses them.
The final screen labels MENU/DEFAULT and actual output size. HTML/JSON record all
effective video settings: only compare scores with matching settings/resolution.

vurus-bench-2 workload (both modes):
Normal difficulty, music off, real effects/voices, volume70%, VSync/cap off.
Player invulnerable, held firing, facing changes every120steps; ammo refill
and weapon change every600steps. CPU/T3P2 moves through generator stations.
Scenes: CAVE/T1C2, CPU/T3P2, two UNDERGROUND/T2U1 and two HITEK/T1H1 starts,
plus HITEK/T2H2 Burner crossfire at4571,2841. This seventh scene keeps the
player stationary without firing and enemies invulnerable, sustaining2-3
Burners with normal AI/weapons. JSON records overlapping-fire counters; no
crossfire means no score. Protocol2 scores include the extra scene and cannot
be compared directly with previous six-scene protocol1 scores.
Briefings are skipped directly by the benchmark; resources remain unmodified.

Timing excludes loading, scripted control changes, event polling, separate
population/memory sampling and file output. The progress/FPS overlay is included
in render/present. Reports show means and JSON also has median/p95/p99/max.
RSS is Linux process memory; sampled peaks may miss short spikes. Nearby means
alive enemies within1,000world units at spawn, not all active or visible.

--frames N changes sample count for quick validation; it is unscored.
Silent/dummy-audio, driver-forced VSync or incorrect fixed-preset output runs
are also unscored. User-resolution runs still receive a score.
Cancelled/incomplete runs keep any completed scene results but have no score.
--no-wait exits after writing reports (for automation). --shot RESULT.ppm
captures the results screen before swapping. --help lists options.
Normal game settings are never changed. Close the game before
benchmarking; do not run builds/tracing at the same time as a scored test.

Engine and third-party notices accompany this package in benchmark-LICENSES/.
Also run vurus-bench --licenses (or vurus-bench.aarch64 --licenses).
