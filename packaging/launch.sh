#!/bin/sh
# Put your own Vyruz ISO or BIN/CUE alongside this script and the binary.
# --data overrides the source; imported assets stay in data/ beside the binary.
set -eu
launcher_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$launcher_dir/vurus" --data "${VURUS_DATA_DIR:-${VYRUZ_DATA_DIR:-$launcher_dir}}" "$@"
