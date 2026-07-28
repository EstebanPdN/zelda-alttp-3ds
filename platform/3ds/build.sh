#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITARM="${DEVKITARM:-${DEVKITPRO}/devkitARM}"
SDL_BUILD="${ROOT}/build-3ds/sdl"
SDL_PREFIX="${ROOT}/build-3ds/prefix"
GAME_BUILD="${ROOT}/build-3ds/game"
TOOLS_ROOT="${ZELDA3_TOOLS_ROOT:-${ROOT}/../../Tools/bin}"

export DEVKITPRO DEVKITARM

if [[ ! -f "${SDL_PREFIX}/lib/cmake/SDL2/SDL2Config.cmake" ]]; then
  cmake \
    -S "${ROOT}/app/jni/SDL2" \
    -B "${SDL_BUILD}" \
    -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
    -DCMAKE_INSTALL_PREFIX="${SDL_PREFIX}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DSDL_SHARED=OFF \
    -DSDL_STATIC=ON \
    -DSDL_TEST=OFF
fi
cmake --build "${SDL_BUILD}" --parallel
cmake --install "${SDL_BUILD}"

cmake \
  -S "${ROOT}/platform/3ds" \
  -B "${GAME_BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSDL2_ROOT="${SDL_PREFIX}"
cmake --build "${GAME_BUILD}" --parallel

MAKEROM="${MAKEROM:-${TOOLS_ROOT}/makerom}"
BANNERTOOL="${BANNERTOOL:-${TOOLS_ROOT}/bannertool}"
if [[ ! -x "${MAKEROM}" || ! -x "${BANNERTOOL}" ]]; then
  printf '3DSX listo. Para crear la CIA define MAKEROM y BANNERTOOL.\n'
  exit 0
fi

"${BANNERTOOL}" makesmdh \
  -s "A Link to the Past 3DS v2.8" \
  -l "A Link to the Past 3DS v2.8" \
  -p "snesrev / samyost1 / 3DS port" \
  -i "${ROOT}/platform/3ds/assets/icon.png" \
  -f visible,nosavebackups \
  -o "${GAME_BUILD}/zelda3-3ds.icn"

if [[ -f "${ROOT}/platform/3ds/assets/banner.cgfx" ]]; then
  "${BANNERTOOL}" makebanner \
    -ci "${ROOT}/platform/3ds/assets/banner.cgfx" \
    -a "${ROOT}/platform/3ds/assets/banner.wav" \
    -o "${GAME_BUILD}/zelda3-3ds.bnr"
else
  "${BANNERTOOL}" makebanner \
    -i "${ROOT}/platform/3ds/assets/banner.png" \
    -a "${ROOT}/platform/3ds/assets/banner.wav" \
    -o "${GAME_BUILD}/zelda3-3ds.bnr"
fi

(
  cd "${ROOT}"
  "${MAKEROM}" \
    -f cia \
    -o "${GAME_BUILD}/zelda3-3ds-v2.8.cia" \
    -DAPP_ROMFS=build-3ds/game/romfs \
    -rsf platform/3ds/cia/zelda3.rsf \
    -target t \
    -exefslogo \
    -elf build-3ds/game/zelda3-3ds.elf \
    -icon build-3ds/game/zelda3-3ds.icn \
    -banner build-3ds/game/zelda3-3ds.bnr
)

printf 'Listos:\n'
printf '  %s\n' "${GAME_BUILD}/zelda3-3ds-v2.8.3dsx"
printf '  %s\n' "${GAME_BUILD}/zelda3-3ds-v2.8.cia"
