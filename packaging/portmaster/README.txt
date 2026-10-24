Vurus — PortMaster Beta ARM64 / OpenGL ES 3.0 build

Extract Vurus.sh and the vurus folder together into your firmware's ports folder.
Install PortMaster first. Launch Vurus from your firmware's ports menu.

Supply your own original Vyruz: Destruction of the Untel Empire resources.
Place an ISO or single-BIN/CUE inside vurus/, beside vurus.aarch64. On first
launch the built-in importer creates vurus/data/. Alternatively copy an existing
import's data/ folder there. The original disc files are never deleted.
The startup screen supports controller Cancel, Retry and Exit.

Keep data/, saves/, slot1.vzs, options.cfg and vz.sco when upgrading. Install
engine updates over the existing folder; do not replace it with an empty folder.
All user storage is beside the executable, independently of launch location.

Launch Vurus Benchmark from Ports to run the separate automatic performance test
after importing game data. See benchmark-README.txt for scores, video settings,
--default-settings and offline HTML/JSON reports; game saves/settings stay unchanged.

Native SDL controller input uses PortMaster's mappings. Configure controls in
Options. Start opens the game menu; use PortMaster's usual exit hotkey to quit.
Options -> Video -> Advanced -> FPS counter enables a saved On/Off frame-rate
and RSS memory overlay (default Off); no debug mode or SSH connection is needed.
Default resolution is 640x480; other resolutions and adaptive HUD remain available.
Optional CRT, bloom, sharpen and antialiasing shaders are unavailable in this
build. Gameplay effects remain enabled. See engine-README.txt for all options.

This Beta is a normal optimized Release build, not a debug build.
It requires aarch64 and native GLES 3.0; GLES 2-only devices are unsupported.
User-tested on Anbernic RG40XX H (H700,1GB RAM,muOS2410 Banana): roughly30-40FPS
with FX and terrain effects disabled, dipping into the low20s in heavy battles.
These are user observations, not guarantees for other devices. Long-session and
repeated-level-transition validation continues. Not an official PortMaster
catalog submission or acceptance.
No original game assets, saves, settings, or graphics-driver libraries are bundled.
Thanks to the original game's creators. Vurus and third-party notices are in
LICENSE.md and LICENSES/.
