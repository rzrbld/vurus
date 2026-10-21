#!/bin/sh
# Standalone add-on: never package game resources, reports or user files.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:?Usage: package_benchmark.sh BUILD_DIR OUTPUT_DIR linux|portmaster}
out_dir=${2:?Usage: package_benchmark.sh BUILD_DIR OUTPUT_DIR linux|portmaster}
platform=${3:?Usage: package_benchmark.sh BUILD_DIR OUTPUT_DIR linux|portmaster}
case "$platform" in linux|portmaster) ;; *) echo 'Expected linux or portmaster' >&2;exit 1;;esac
test -f "$build_dir/vurus-bench"
if [ "$platform" = portmaster ]; then
    readelf -h "$build_dir/vurus-bench" | grep -q 'Machine:.*AArch64'
    grep -q '^VYRUZ_PORTMASTER:BOOL=ON$' "$build_dir/CMakeCache.txt"
    if readelf -d "$build_dir/vurus-bench" | grep 'NEEDED' | grep -Eq 'libc\+\+|libunwind'; then
        echo 'C++ runtime must be linked statically.' >&2;exit 1
    fi
    cmake -DBINARY="$build_dir/vurus-bench" -DREADELF="$(command -v readelf)" \
        -DMAX_GLIBC=2.31 -P "$project_dir/cmake/CheckGlibc.cmake"
fi
mkdir -p "$out_dir"
out_dir=$(CDPATH= cd -- "$out_dir" && pwd)
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT HUP INT TERM
if [ "$platform" = portmaster ]; then
    package_dir="$staging/vurus"
    mkdir -p "$package_dir"
    install -m 755 "$project_dir/packaging/benchmark/Vurus Benchmark.sh" "$staging/Vurus Benchmark.sh"
    install -m 755 "$build_dir/vurus-bench" "$package_dir/vurus-bench.aarch64"
else
    package_dir="$staging/vurus-benchmark-linux"
    mkdir -p "$package_dir"
    install -m 755 "$build_dir/vurus-bench" "$package_dir/vurus-bench"
fi
install -m 644 "$project_dir/packaging/benchmark/README.txt" "$package_dir/benchmark-README.txt"
notices="$package_dir/benchmark-LICENSES"
mkdir -p "$notices"
install -m 644 "$project_dir/LICENSE" "$notices/engine-MIT.txt"
install -m 644 "$project_dir/LICENSE.md" "$notices/engine-LICENSE.md"
for component in oxideav-indeo zlib mame-psgs; do
    install -m 644 "$project_dir/third_party/$component/LICENSE" "$notices/$component.txt"
done
install -m 644 "$project_dir/third_party/mame-psgs/NOTICE" "$notices/MAME-PSGS-NOTICE.txt"
install -m 644 "$project_dir/third_party/smaa/LICENSE.txt" "$notices/SMAA.txt"
cp -R "$project_dir/third_party/oxideav-indeo/rust-licenses" "$notices/"
if [ "$platform" = portmaster ]; then
    cp /usr/share/doc/libc++1-18/copyright "$notices/libc++.txt"
    cp /usr/share/doc/libc++abi1-18/copyright "$notices/libc++abi.txt"
    cp /usr/share/doc/gcc-9-base/copyright "$notices/GCC-runtime.txt"
    strip "$package_dir/vurus-bench.aarch64"
    (cd "$staging" && zip -q -r package.zip 'Vurus Benchmark.sh' vurus)
    mv "$staging/package.zip" "$out_dir/vurus-benchmark-portmaster.zip"
    (cd "$out_dir" && sha256sum vurus-benchmark-portmaster.zip > SHA256SUMS-portmaster.txt)
else
    strip "$package_dir/vurus-bench"
    tar --sort=name --mtime='UTC 2026-01-01' --owner=0 --group=0 --numeric-owner \
        -czf "$out_dir/vurus-benchmark-linux.tar.gz" -C "$staging" vurus-benchmark-linux
    (cd "$out_dir" && sha256sum vurus-benchmark-linux.tar.gz > SHA256SUMS-linux.txt)
fi
