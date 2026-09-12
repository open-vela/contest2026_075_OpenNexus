#!/usr/bin/env bash
# Prepare SF32LB52-DevKit-LCD for FocusMate Demo v2.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_ROOT="$(cd "${REPO_ROOT}/.." && pwd)"
NUTTX_REPO="${WORKSPACE_ROOT}/nuttx"
VENDOR_REPO="${WORKSPACE_ROOT}/vendor/sifli"
NUTTX_PATCH="${REPO_ROOT}/docs/board_bringup/nuttx_mkallsyms_fix.patch"
VENDOR_PATCH="${REPO_ROOT}/docs/board_bringup/vendor_sifli_devkit_lcd_fix.patch"
BOARD_CONFIG="${VENDOR_REPO}/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh"

apply_patch_once() {
  local repo="$1"
  local patch="$2"

  if git -C "${repo}" apply --reverse --check "${patch}" 2>/dev/null; then
    echo "Already applied: ${patch}"
  elif git -C "${repo}" apply --check "${patch}" 2>/dev/null; then
    git -C "${repo}" apply "${patch}"
    echo "Applied: ${patch}"
  else
    echo "ERROR: cannot apply or verify ${patch}" >&2
    exit 1
  fi
}

if [[ ! -d "${BOARD_CONFIG}" ]]; then
  echo "ERROR: SF32LB52 board config not found: ${BOARD_CONFIG}" >&2
  exit 1
fi

apply_patch_once "${NUTTX_REPO}" "${NUTTX_PATCH}"
apply_patch_once "${VENDOR_REPO}" "${VENDOR_PATCH}"

echo "Prepared: ${BOARD_CONFIG}"
grep -E 'CONFIG_(LVX_USE_DEMO_FOCUSMATE|FOCUSMATE_|NETUTILS_CJSON|LV_FONT_MONTSERRAT_(20|32|40)|INPUT_FT6146|TOUCH_IRQ_PIN)' "${BOARD_CONFIG}/defconfig"
