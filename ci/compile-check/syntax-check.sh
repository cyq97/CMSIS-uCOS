#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

CC=${CC:-gcc}
CFLAGS=(
  -std=c11 -Wall -Wextra -Werror
  -fsyntax-only
  -D__STATIC_INLINE=static\ inline
)

echo "[compile-check] uCOS3 wrapper (syntax-only)"
"$CC" "${CFLAGS[@]}" \
  -I"$ROOT_DIR/ci/compile-check/stubs/ucos3" \
  -I"$ROOT_DIR/CMSIS/RTOS2/Include" \
  -I"$ROOT_DIR/CMSIS/RTOS2/uCOS3/Include" \
  -I"$ROOT_DIR/libs/uC-OS3/Source" \
  "$ROOT_DIR/CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c"

echo "[compile-check] uCOS2 wrapper (syntax-only)"
"$CC" "${CFLAGS[@]}" \
  -I"$ROOT_DIR/ci/compile-check/stubs/ucos2" \
  -I"$ROOT_DIR/CMSIS/RTOS2/Include" \
  -I"$ROOT_DIR/CMSIS/RTOS2/uCOS2/Include" \
  -I"$ROOT_DIR/libs/uC-OS2/Source" \
  "$ROOT_DIR/CMSIS/RTOS2/uCOS2/Source/cmsis_os2_ucos2.c"

echo "[compile-check] OK"
