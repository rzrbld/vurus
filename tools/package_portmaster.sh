#!/bin/sh
# Explicit allowlist: never include original resources or portable user files.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:?Usage: package_portmaster.sh ARM_BUILD_DIR OUTPUT_DIR}
out_dir=${2:?Usage: package_portmaster.sh ARM_BUILD_DIR OUTPUT_DIR}
check_binary() {
    readelf -h "$1" | grep -q 'Machine:.*AArch64' || {
        echo 'Refusing to package a non-AArch64 executable' >&2; exit 1;
    }
    if readelf -d "$1" | grep 'NEEDED' | grep -Eq 'libc\+\+|libunwind'; then
        echo 'C++ runtime must be linked statically for this package.' >&2
        exit 1
    fi
    cmake -DBINARY="$1" -DREADELF="$(command -v readelf)" \
        -DMAX_GLIBC=2.31 -P "$project_dir/cmake/CheckGlibc.cmake"
}
grep -q '^VYRUZ_PORTMASTER:BOOL=ON$' "$build_dir/CMakeCache.txt"
check_binary "$build_dir/vurus"
benchmark=false
if grep -q '^VURUS_BUILD_BENCHMARK:BOOL=ON$' "$build_dir/CMakeCache.txt"; then
    check_binary "$build_dir/vurus-bench"
    benchmark=true
fi
mkdir -p "$out_dir"
out_dir=$(CDPATH= cd -- "$out_dir" && pwd)
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT HUP INT TERM
mkdir -p "$staging/vurus/LICENSES"
install -m 755 "$build_dir/vurus" "$staging/vurus/vurus.aarch64"
if [ "$benchmark" = true ]; then
    install -m 755 "$build_dir/vurus-bench" "$staging/vurus/vurus-bench.aarch64"
    install -m 755 "$project_dir/packaging/benchmark/Vurus Benchmark.sh" "$staging/Vurus Benchmark.sh"
    install -m 644 "$project_dir/packaging/benchmark/README.txt" "$staging/vurus/benchmark-README.txt"
    strip "$staging/vurus/vurus-bench.aarch64"
fi
install -m 755 "$project_dir/packaging/portmaster/Vurus.sh" "$staging/Vurus.sh"
install -m 644 "$project_dir/packaging/portmaster/README.txt" "$staging/vurus/README.txt"
install -m 644 "$project_dir/packaging/README.txt" "$staging/vurus/engine-README.txt"
install -m 644 "$project_dir/LICENSE.md" "$staging/vurus/LICENSE.md"
for component in oxideav-indeo zlib mame-psgs; do
    install -m 644 "$project_dir/third_party/$component/LICENSE" "$staging/vurus/LICENSES/$component.txt"
done
install -m 644 "$project_dir/third_party/mame-psgs/NOTICE" "$staging/vurus/LICENSES/MAME-PSGS-NOTICE.txt"
install -m 644 "$project_dir/third_party/smaa/LICENSE.txt" "$staging/vurus/LICENSES/SMAA.txt"
cp -R "$project_dir/third_party/oxideav-indeo/rust-licenses" "$staging/vurus/LICENSES/"
# libc++ is linked statically in the PortMaster builder.
if [ ! -f /usr/share/doc/libc++1-18/copyright ]; then
    echo 'Package inside the PortMaster builder to include its C++ runtime notices.' >&2
    exit 1
fi
cp /usr/share/doc/libc++1-18/copyright "$staging/vurus/LICENSES/libc++.txt"
cp /usr/share/doc/libc++abi1-18/copyright "$staging/vurus/LICENSES/libc++abi.txt"
cp /usr/share/doc/gcc-9-base/copyright "$staging/vurus/LICENSES/GCC-runtime.txt"
strip "$staging/vurus/vurus.aarch64"
# A fresh archive avoids retaining stale entries from an older output.
if [ "$benchmark" = true ]; then
    (cd "$staging" && zip -q -r package.zip Vurus.sh 'Vurus Benchmark.sh' vurus)
else
    (cd "$staging" && zip -q -r package.zip Vurus.sh vurus)
fi
mv "$staging/package.zip" "$out_dir/vurus-portmaster.zip"
sha256sum "$out_dir/vurus-portmaster.zip"
