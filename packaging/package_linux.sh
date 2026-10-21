#!/bin/sh
# Package the native executable, launcher and short README; never game assets.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"$project_dir/build"}
out_dir=${2:-"$project_dir/dist"}
cmake --build "$build_dir" --target vurus -j 4
benchmark=false
if grep -q '^VURUS_BUILD_BENCHMARK:BOOL=ON$' "$build_dir/CMakeCache.txt"; then
    cmake --build "$build_dir" --target vurus-bench -j 4
    benchmark=true
fi
mkdir -p "$out_dir"
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT HUP INT TERM
mkdir "$staging/vurus-linux"
install -m 755 "$build_dir/vurus" "$staging/vurus-linux/vurus"
if [ "$benchmark" = true ]; then
    install -m 755 "$build_dir/vurus-bench" "$staging/vurus-linux/vurus-bench"
    install -m 644 "$project_dir/packaging/benchmark/README.txt" "$staging/vurus-linux/benchmark-README.txt"
    strip "$staging/vurus-linux/vurus-bench"
fi
install -m 755 "$project_dir/packaging/launch.sh" "$staging/vurus-linux/launch.sh"
install -m 644 "$project_dir/packaging/README.txt" "$staging/vurus-linux/README.txt"
install -m 644 "$project_dir/LICENSE" "$project_dir/LICENSE.md" "$staging/vurus-linux/"
mkdir "$staging/vurus-linux/LICENSES"
install -m 644 "$project_dir/third_party/zlib/LICENSE" "$staging/vurus-linux/LICENSES/zlib.txt"
mkdir -p "$staging/vurus-linux/third_party/oxideav-indeo"
cp -R "$project_dir/third_party/oxideav-indeo/rust-licenses" "$staging/vurus-linux/third_party/oxideav-indeo/"
install -m 644 "$project_dir/third_party/smaa/LICENSE.txt" "$staging/vurus-linux/LICENSES/SMAA.txt"
install -m 644 "$project_dir/third_party/mame-psgs/LICENSE" "$staging/vurus-linux/LICENSES/MAME-PSGS.txt"
install -m 644 "$project_dir/third_party/mame-psgs/NOTICE" "$staging/vurus-linux/LICENSES/MAME-PSGS-NOTICE.txt"
install -m 644 "$project_dir/packaging/README.txt" "$out_dir/README.txt"
strip "$staging/vurus-linux/vurus"
tar --sort=name --mtime='UTC 2026-01-01' --owner=0 --group=0 --numeric-owner -czf "$out_dir/vurus-linux.tar.gz" -C "$staging" vurus-linux
printf 'Created %s/vurus-linux.tar.gz (binary, launcher and README)\n' "$out_dir"
