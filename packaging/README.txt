Vurus - Linux / Bazzite

1. Extract this package into a writable folder.
2. Before the first launch, put your own Vyruz ISO or BIN + CUE in the
   same folder as vurus and launch.sh. Keep both BIN and CUE together
   to import the CD music. Use one disc image at a time.
3. Run launch.sh. The first launch unpacks the game into data/ beside
   the executable; allow it to finish and reach the game normally.
4. After successful unpacking and startup, the ISO or BIN/CUE is no
   longer needed. You can delete it yourself. Keep the entire data/
   folder: it contains the imported game, movie (if present) and music.

An SDL setup screen shows resource extraction and CD-track ripping progress.
Press Esc, controller B, or click Cancel to stop safely. Errors offer
Retry (Enter / controller A) or Exit (Esc / controller B).
After preparation the game starts automatically; later launches reuse data/.
If preparation is interrupted, launch again to retry safely. Free-space and
write errors keep existing imported data and the original disc unchanged.
Advanced validation: --check-data-all checks all 25 maps without opening a
window and verifies file checksums recorded by new imports.
Move data/ together with vurus, launch.sh and this README if relocating
or updating the game. The game never deletes your original disc image.

If upgrading from an older build that imported into your home folder:
copy the contents of the old import's folder (vz-data.rsc, Vz-slide.rsc,
Intro.avi if present, and music/) into data/ beside vurus. Old imports
are in ~/.local/share/vyruz26/imports/<id>/, or under XDG_DATA_HOME.
Alternatively, place your original image beside the new binary again.
Do not delete an old import until the new installation works.

Advanced: --data /path/to/resources or /path/to/disc.cue selects a source.
New disc imports still go into data/ beside the executable. This folder
must be writable for extraction. Existing external resource folders can
be read directly.

Options / Video / Advanced / Mode: Windowed, Fullscreen (exclusive), Borderless fullscreen
(desktop resolution), or Borderless window. Exclusive mode requires a supported
display resolution; unavailable changes retain the prior setting. Borderless
fullscreen uses the desktop size. Unavailable saved modes fall back to Windowed
for that run. Existing HUD aspect, brightness and filtering options are retained.
Video / Effects: Reactor FX, Ground / smoke, Shadows.
Video / Frame cap: 30 / 60 / Uncapped (default 60); gameplay stays at 60 Hz.
Video / Advanced / VSync: On/Off (default On). VSync may limit uncapped rendering.
Use VSync Off for fully uncapped rendering; --fps overrides the saved cap.
Video / Advanced / Scene effects / Vignette toggles original shaded gameplay screen edges
(default On); HUD panels stay clear.
Video / Advanced / Scene effects / Screen shader toggles MAME-PSGS curvature, scanlines and phosphor
mask (default Off). Requires OpenGL2.1 shader support and adds GPU work. Off
restores normal rendering. Apache2.0 license/notices are in LICENSES and --licenses.
Extras / Controls help shows current bindings; arrows, D-pad or wheel scroll;
Esc/Space/Return or controller A/B closes. F1 information and debug-only F2 stay.

Portable user files live beside the vurus binary:
  saves/      seven rolling checkpoints per player, with previews
  slot1.vzs   preserved legacy checkpoint
  options.cfg settings and player name
  vz.sco      leaderboard
Keep these files and the saves folder when replacing the binary; copy them together when moving
the game. The directory must be writable to save progress and settings.
On startup, missing portable files are copied from the old vyruz26 XDG/home
locations if available. Existing portable files are never overwritten, and
old files are left intact. --save-file and --options-file still override the
respective defaults; --load-save selects the save used by that session.

Testing controls: press F2 during gameplay, or open the pause menu
(Esc / controller Start) and choose Debug / testing. Use Up/Down or
controller D-pad to select, Enter / A to apply, and Esc / B to go back.
The menu provides all 25 levels, enemy AI on/off, god mode, full authored
ammo stocks, and a repeatable +1,000,000 money action. Active god mode
and disabled AI are shown on screen. New game and loading a saved game
reset both toggles. These are port testing aids, not original features.
Level select starts a fresh unsaved test session at the authored start;
current unsaved progress is discarded. It does not write disk saves.
Ordinary save terminals still save the current session when used.

Vurus is a reverse-engineered reimplementation of Vyruz: Destruction of
the Untel Empire. Original game data is not included.
Intro video uses the MIT-licensed OxideAV oxideav-indeo Rust decoder:
https://github.com/OxideAV/oxideav-indeo

Surprise testing: F2 -> Surprises. Up/Down (or D-pad) browses all pages.
Enter/A replaces the current effect and resumes play. The list includes
Random Surprise and Clear active Surprise.

Testing controls are opt-in: ./launch.sh --debug
Without --debug, the debug menu is hidden and F2 does nothing.
Extras contains Scores, Credits, Intro, Slide Show (if installed), and Demo.
Loading screens use the original artwork and show actual loading progress.

Options -> Video -> Advanced -> Sprite filtering: Off / Bilinear / Trilinear.
Applies immediately and saves in options.cfg. Off is the default. Bilinear
softens world sprites; trilinear additionally uses mipmaps when reduced.
Both filtered modes normally look alike when sprites are enlarged.
Menu text, terrain tiles and additive/shadow effects retain their usual look.

Crates, barrels and police barriers now use their authored mass and friction
for pushing; anchored scenery remains fixed. Player wall impacts bounce.
Gatling fire starts angled downward and levels out while the trigger is held,
so ground impacts progress from near the vehicle to farther away.
Save format19 retains moving-object momentum, Gatling firing state and guided
missile targets/turning/routes,
and reads older port saves.

Gameplay speech now follows the original weapon, pickup, combat and warning
triggers and timing. Some reactions are occasional, not guaranteed every time.
Options -> Audio controls voices and music separately.
Audio / Advanced / Menu sounds: Off silences menu hover/selection cues independently of game FX.
Imported CD music plays through the audio tracks. Keep music/ inside the
selected data folder, or beside the binary. CD tracks come from the original mixed-mode BIN + CUE;
a data-only ISO has no CD audio tracks, so the game uses the original data's WAV theme.
--music-dir PATH selects another extracted music folder; --music-track 2..8
keeps a specific track looping as a testing override.

Save terminals show their price when approached. The standard terminal costs
100,000 in-game credits; remain nearby for the countdown to save. A successful
save charges once, and a failed disk write preserves your credits.

Missile guidance: player and enemy homing missiles follow acquired targets and
self-destruct after their original5-second fuse. Stingers stay straight and
explode after2.5 seconds. Failed launch locks remain unguided.

Load Game opens Saved sessions. Select a checkpoint to see its energy, shield
and difficulty, then load it. Normal paid saves keep the latest seven sessions
in the same save history even when you change your name in Options. Existing
slot1.vzs remains available separately.

Command-line reference
======================
Pass these options to ./launch.sh or ./vurus. Quote paths containing spaces.
--debug enables the in-game testing menu; CLI probes are separately available.

Launch, data and user files
---------------------------
  --help, -h
    Print command-line usage and exit.
  --licenses
    Print third-party license notices and exit. Run ./vurus --licenses
    directly with no other arguments; launch.sh adds --data.
  --data PATH
    Select a resource directory, ISO, BIN or single-BIN CUE. New imports still
    go beside the binary.
  --check-data
    Import if needed, verify recorded import checksums and validate the
    selected level without a window.
  --check-data-all
    Import if needed, verify recorded import checksums and validate all 25
    maps without a window.
  --debug
    Enable F2, debug coordinate logging and the Debug / testing menu, including
    level selection, AI, god
    mode, ammo, money and Surprises. Without this flag, the menu is hidden and
    F2 does nothing.
  --play
    Skip the title menu and enter gameplay.
  --level WORLD/MAP.3DG
    Select the initial level or the level checked by --check-data; default
    UNDERGROUND/T1U1.3DG. --load-save supplies its own saved level.
  --options-file PATH
    Use another settings file instead of portable options.cfg.
  --save-file PATH
    Use a single checkpoint file instead of normal rolling save history.
  --load-save PATH
    Load a checkpoint directly; subsequent terminal saves in this session use
    that path.
  --controller-map FILE
    Load a standard SDL2 controller mapping database before setup/gameplay
    input.
  --fps 60|90
    Override the saved frame cap for this run (60 or 90 FPS).
    Gameplay simulation remains 60 Hz.

Audio
-----
  --mute
    Disable the audio device for this run.
  --volume 0..1
    Set the master volume multiplier; default 0.7.
  --no-music
    Disable music while retaining other audio.
  --music-dir PATH
    Use another extracted CD music directory containing track02.wav through
    track08.wav.
  --music-track 2..8
    Loop one CD track as a testing override instead of using the native
    playlist.
  --music-theme 1..3
    Choose the fallback WAV theme: 1 TECHNO, 2 DARKEN, 3 HARDCORE; default 1.
  --audio-language english|french
    Select gameplay speech language; default English.

Diagnostics and capture probes
------------------------------
  --profile-frames N
    Collect N measured loop frames, print timing/CPU statistics, then exit;
    clamped to 0..36000. Zero disables profiling.
  --profile-warmup N
    Exclude N initial frames from profiling; default 120, clamped to 0..36000.
  --shot FILE.ppm
    Capture one rendered frame as PPM, then exit. Uses drawable pixel
    dimensions.
  --simulate-ms 0..60000
    Advance gameplay before the interactive loop/capture, in whole 60 Hz
    ticks. Combine with --shot for an automatic exit.
  --fire
    Hold fire during --simulate-ms only.
  --anim-ms 0..60000
    Advance scene/HUD animation before simulation/capture; does not advance
    gameplay physics.
  --write-save PATH
    Write a free diagnostic checkpoint after initial simulation; does not
    itself exit. Combine with --shot for an automatic exit.
  --player X Y
    Override the initial player position in world/surface pixels, including
    after --load-save.
  --dir 0..23
    Force the player heading/render direction for a probe.
  --step N
    Force a zero-based raw player bitmap index for sprite inspection; use a
    valid bitmap index.
  --walk N
    Run N legacy geometric slide-probe steps before gameplay. This is not the
    live gameplay collision/movement path.
  --walkdir DX DY
    Set the direction vector for --walk; default 1 0.
  --money N
    Set initial player money for a probe; clamped to 0..1000000000.
  --map
    Attempt to open the paid map before simulation; normal money rules apply.
  --pause
    Start paused after initial simulation.
  --inventory
    Open inventory after initial simulation if the player is alive.
  --damage N
    Apply nonnegative damage to the player after initial simulation.
  --menu main|game|controls|confirm
    Open a menu probe; combine with --shot to capture it.

Examples:
  ./launch.sh --debug
  ./launch.sh --check-data-all
  ./launch.sh --simulate-ms 1000 --fire --shot combat.ppm

Probe order: load/position, geometric walk, animation, heading/money/map,
simulation, damage/pause/inventory, diagnostic save, menu/capture. For automated
probes use an isolated binary directory and explicit --options-file/--save-file
paths to keep normal user data separate.
VURUS_DATA_DIR sets the default source; VYRUZ_DATA_DIR is the legacy alias.
Explicit --data wins. Help and licenses do not require game resources.

--debug also shows FPS, average FRAME milliseconds and MAX frame milliseconds.
Updated every half second; includes VSync/pacing, not GPU-only timing.

Video / Advanced / Scene effects / Screen shader: Off / CRT / Retro / Bright (warm CRT colors; Bright adds highlight glow).
Scene effects: FXAA, SMAA, Subtle bloom and Gentle sharpening, all default Off.
FXAA and SMAA exclude each other. SMAA requires OpenGL 3.0 and uses three passes.
Bloom and sharpening are independent, with or without CRT. Filters apply
to the game scene before HUD/text and add GPU work. Reset effects disables all.

SMAA: https://github.com/iryoku/smaa (MIT), license in LICENSES/SMAA.txt

Saving (v0.1.2)
==============
Checkpoints encode/compress and write in the background. Old saves remain readable,
but older engine builds cannot read the new compressed saves. No game state is
removed. Existing files are not rewritten until you save again.
zlib is statically linked in release builds; its license is included.
