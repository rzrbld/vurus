Vurus — portable desktop build

Extract this entire directory into a writable location. Place your own original
Vyruz ISO or BIN/CUE beside vurus.exe (Windows) or vurus (macOS), then run it.
The first-run screen imports resources and CD audio. Original game assets are
not included. An extracted resource directory also works with --data PATH.

Windows: Windows 10 version 1903 or later, x64, working OpenGL driver. Keep any
packaged runtime DLLs beside the executable. SDL2 is static in CI packages.
macOS 15 or later: separate Apple Silicon and Intel builds. This is a portable command-line
executable, not an app bundle. Run ./vurus from Terminal. The builds are unsigned
and not notarized. SMAA is unavailable with the macOS legacy OpenGL renderer;
FXAA, CRT/Retro/Bright, bloom and sharpening remain optional.

Imported data/, saves/, options.cfg and vz.sco live beside the executable.
Keep the folder writable. Do not place it in Program Files or a read-only disk
image. --debug enables the F2 debug menu. --help lists launch options.

These are initial cross-platform builds: successful CI is required before they
are considered build-validated. Physical audio/controller/GPU testing remains
separate from CI. Operating-system libraries and graphics drivers are required;
this is not a fully static operating-system-independent executable.

Third-party notices: LICENSES/ and ./vurus --licenses (vurus.exe on Windows).

Saving (v0.1.2)
==============
Checkpoints encode/compress and write in the background. Old saves remain readable,
but older engine builds cannot read the new compressed saves. No game state is
removed. Existing files are not rewritten until you save again.
zlib is statically linked in release builds; its license is included.
