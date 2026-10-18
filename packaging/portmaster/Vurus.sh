#!/bin/bash
# Native SDL controller input; PortMaster supplies mappings and the exit hotkey.
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -z "${controlfolder:-}" ]; then
    for candidate in /opt/system/Tools/PortMaster /opt/tools/PortMaster \
        "$XDG_DATA_HOME/PortMaster" /mnt/mmc/MUOS/PortMaster /roms/ports/PortMaster; do
        if [ -f "$candidate/control.txt" ]; then
            controlfolder=$candidate
            break
        fi
    done
fi
if [ ! -f "${controlfolder:-}/control.txt" ]; then
    echo "PortMaster control.txt was not found. Install PortMaster first." >&2
    exit 1
fi
export controlfolder
source "$controlfolder/control.txt"
[ ! -f "$controlfolder/mod_${CFW_NAME}.txt" ] || source "$controlfolder/mod_${CFW_NAME}.txt"
get_controls

# Resolve relative to this launcher, including firmware-specific ports locations.
GAMEDIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/vurus"
if [ ! -d "$GAMEDIR" ] && [ -n "${directory:-}" ]; then
    GAMEDIR="/${directory#/}/ports/vurus"
fi
cd "$GAMEDIR" || exit 1
exec > >(tee "$GAMEDIR/log.txt") 2>&1
if [ "${DEVICE_ARCH:-aarch64}" != aarch64 ]; then
    echo "This Vurus package requires aarch64 and OpenGL ES 3.0."
    exit 1
fi
export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-${SDL_GAMECONTROLLERCONFIG:-}}"
trap 'pm_finish' EXIT
$GPTOKEYB "vurus.aarch64" &
pm_platform_helper "$GAMEDIR/vurus.aarch64"
./vurus.aarch64 "$@"
exit $?
