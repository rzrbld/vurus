# Building Vurus

Build the engine from a source checkout. Original game resources are needed to
play, but are not needed for the standard test suite and must not be packaged
with the engine. Start with a clean build directory for each platform/toolchain.

## Desktop builds

Common requirements: a C++20 compiler, CMake, Rust/Cargo, SDL2, OpenGL and zlib.
The vendored Indeo decoder builds offline; you do not need to download Cargo
packages for it. See the [desktop workflow](../.github/workflows/build-desktop.yml)
for the exact CI dependency/toolchain versions and supported build matrix.

### Linux / Steam Deck

On Debian/Ubuntu, install the build dependencies and Rust/Cargo:

```sh
sudo apt-get install g++ cmake pkg-config libsdl2-dev zlib1g-dev libgl1-mesa-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --parallel 2
./build/vurus --data /path/to/game.iso
sh packaging/package_linux.sh build dist
```

The archive is `dist/vurus-linux.tar.gz`. A native build uses your host's
libraries and ABI; it does not automatically have the same compatibility as a
CI-built Linux package. Keep the existing glibc checks enabled. Steam Deck users
can use the Linux release package rather than installing a development toolchain
on the device.

### Windows x64

Use Visual Studio 2022 with the C++ workload and Windows SDK, CMake and a Rust
MSVC toolchain. Obtain SDL2 and zlib source directories; replace these example
paths with their actual locations. Run from a Visual Studio developer terminal:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON -DVYRUZ_SDL_SOURCE=C:/deps/SDL -DVYRUZ_ZLIB_SOURCE=C:/deps/zlib
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --parallel 2
cmake --build build --config Release --target package
```

The executable is `build/Release/vurus.exe`; the portable ZIP is created under
`build/`. CI uses static SDL2/zlib and includes the required MSVC runtime files.
An operating-system OpenGL driver is still required. The desktop packages target
Windows 10 version1903 or later.

### macOS arm64 / x86_64

Use Xcode command-line tools, CMake and Rust/Cargo for the host architecture.
Supply SDL2 and zlib source directories:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DVYRUZ_SDL_SOURCE=/absolute/path/to/SDL \
  -DVYRUZ_ZLIB_SOURCE=/absolute/path/to/zlib \
  -DVYRUZ_ENABLE_SMAA=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --parallel 2
cmake --build build --config Release --target package
```

Build separately on Apple Silicon and Intel hosts. This produces an architecture-
specific executable/ZIP, not a universal app bundle. CI packages require macOS15
or later and are unsigned/not notarized. SMAA is unavailable on the legacy macOS
OpenGL path; FXAA and the other desktop effects remain optional.

## Handheld / PortMaster build

The supported handheld target is ARM64 with OpenGL ES3.0 or later, using
PortMaster-compatible firmware. GLES2-only devices are not supported.
Use the pinned [PortMaster Dockerfile](../packaging/portmaster/Dockerfile) so the
binary targets the intended runtime instead of your host Linux distribution.

Docker needs an ARM64 host or working ARM64 emulation. From the source checkout:

```sh
docker build --platform linux/arm64 -t vurus-portmaster-builder:local packaging/portmaster
mkdir -p work/portmaster-build dist/portmaster
docker run --rm --platform linux/arm64 \
  -e VURUS_BUILD_TESTS=ON -e VURUS_BUILD_JOBS=4 \
  -v "$PWD:/source:ro" \
  -v "$PWD/work/portmaster-build:/build" \
  -v "$PWD/dist/portmaster:/dist" \
  vurus-portmaster-builder:local sh /source/tools/build_portmaster.sh /build /dist
```

The [builder script](../tools/build_portmaster.sh) configures a Release build with
`VYRUZ_GLES=ON` and `VYRUZ_PORTMASTER=ON`, runs tests and a headless GLES renderer
check, then packages `dist/portmaster/vurus-portmaster.zip`. Change
`VURUS_BUILD_JOBS` to limit build parallelism; tests default to On.

The package contains `Vurus.sh` and `vurus/`, including `vurus.aarch64`, instructions
and license notices. The packaging script must run inside the builder to include
the runtime notices. Desktop CRT/post-processing effects are disabled in this
GLES build; display resolution, adaptive HUD and controller controls remain.
See [handheld installation](../packaging/portmaster/README.txt) to copy the package
to a device and supply original resources.

For renderer development on a desktop GLES-capable host, use a separate local
build directory with the same two CMake flags. That does not create a compatible
ARM64 handheld binary or replace on-device performance testing.

## CI and validation

[Desktop CI](../.github/workflows/build-desktop.yml) builds/tests/packages Linux
x64, Windows x64 and both macOS architectures. [PortMaster CI](../.github/workflows/build-portmaster.yml)
is an independent ARM64 build/test/package workflow. Their release schedules are
separate. CI artifacts are temporary testing downloads; published packages are
on [GitHub Releases](https://github.com/rzrbld/vurus/releases).

Use CTest for the ordinary suite. Many original-resource and visual audits are
opt-in targets; they need locally owned game data and are not covered by a normal
successful CTest run. Successful compilation/headless checks do not establish
physical controller, audio or graphics behavior on every device.

Keep the source, toolchain, test results and target architecture associated with
each package. Include engine and third-party notices, and exclude original game
resources, local settings and saves. See the [player guide](../packaging/README.txt)
for runtime options and [performance report](performance_update_2026-10-19.md)
for the scope of existing measurements.
