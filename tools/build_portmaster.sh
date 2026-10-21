#!/bin/sh
# Run inside packaging/portmaster/Dockerfile's ARM64 image.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-/build}
out_dir=${2:-/dist}
test_mode=${VURUS_BUILD_TESTS:-ON}
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING="$test_mode" -DVYRUZ_GLES=ON -DVYRUZ_PORTMASTER=ON \
    -DVURUS_BUILD_BENCHMARK=ON -DVURUS_BENCH_BUILD="${GITHUB_SHA:-unversioned}"
cmake --build "$build_dir" --parallel "${VURUS_BUILD_JOBS:-4}"
"$build_dir/vurus" --help
"$build_dir/vurus" --licenses > "$build_dir/licenses-smoke.txt"
"$build_dir/vurus-bench" --help
"$build_dir/vurus-bench" --licenses > "$build_dir/benchmark-licenses-smoke.txt"
if [ "$test_mode" = ON ]; then
    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        ctest --test-dir "$build_dir" --output-on-failure --parallel "${VURUS_BUILD_JOBS:-4}"
    cmake --build "$build_dir" --target render_backend_test --parallel "${VURUS_BUILD_JOBS:-4}"
    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy "$build_dir/render_backend_test"
fi
sh "$project_dir/tools/package_portmaster.sh" "$build_dir" "$out_dir"
