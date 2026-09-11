#!/usr/bin/env bash
# Prepare the official goldfish ARM64 board config with ai_agent + FocusMate.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_ROOT="$(cd "${REPO_ROOT}/.." && pwd)"
BOARD_DIR="${WORKSPACE_ROOT}/vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap"
AI_AGENT_DEFCONFIG="${WORKSPACE_ROOT}/packages/ai_agent/defconfigs/goldfish-arm64-v8a-ap/goldfish-arm64-v8a-ap_defconfig"
TARGET_DEFCONFIG="${BOARD_DIR}/defconfig"

AI_AGENT_REPO="${WORKSPACE_ROOT}/packages/ai_agent"
AI_AGENT_LINK_PATCH="${REPO_ROOT}/docs/board_bringup/ai_agent_velaclaw_link_fix.patch"

if [[ ! -d "${BOARD_DIR}" ]]; then
  echo "ERROR: goldfish board config directory not found: ${BOARD_DIR}" >&2
  exit 1
fi
if [[ ! -f "${AI_AGENT_DEFCONFIG}" ]]; then
  echo "ERROR: ai_agent goldfish defconfig not found: ${AI_AGENT_DEFCONFIG}" >&2
  exit 1
fi

if [[ -f "${AI_AGENT_LINK_PATCH}" ]]; then
  if git -C "${AI_AGENT_REPO}" apply --reverse --check "${AI_AGENT_LINK_PATCH}" 2>/dev/null; then
    echo "ai_agent velaclaw link fix already applied"
  elif git -C "${AI_AGENT_REPO}" apply --check "${AI_AGENT_LINK_PATCH}" 2>/dev/null; then
    git -C "${AI_AGENT_REPO}" apply "${AI_AGENT_LINK_PATCH}"
    echo "Applied ai_agent velaclaw link fix"
  else
    echo "ERROR: cannot apply or verify ${AI_AGENT_LINK_PATCH}" >&2
    exit 1
  fi
fi

cp "${AI_AGENT_DEFCONFIG}" "${TARGET_DEFCONFIG}"

append_if_missing() {
  local key="$1"
  local line="$2"
  if ! grep -q "^${key}=" "${TARGET_DEFCONFIG}"; then
    printf '%s\n' "${line}" >> "${TARGET_DEFCONFIG}"
  fi
}

append_if_missing CONFIG_EXAMPLES_AI_AGENT_VELA        'CONFIG_EXAMPLES_AI_AGENT_VELA=y'
append_if_missing CONFIG_LVX_USE_DEMO_FOCUSMATE         'CONFIG_LVX_USE_DEMO_FOCUSMATE=y'
append_if_missing CONFIG_FOCUSMATE_DATA_DIR             'CONFIG_FOCUSMATE_DATA_DIR="/data/focusmate"'
append_if_missing CONFIG_FOCUSMATE_LCD_DEVPATH          'CONFIG_FOCUSMATE_LCD_DEVPATH="/dev/lcd0"'
append_if_missing CONFIG_FOCUSMATE_INPUT_DEVPATH        'CONFIG_FOCUSMATE_INPUT_DEVPATH="/dev/input0"'
append_if_missing CONFIG_FOCUSMATE_ENABLE_UI            'CONFIG_FOCUSMATE_ENABLE_UI=y'
append_if_missing CONFIG_LV_FONT_MONTSERRAT_40         'CONFIG_LV_FONT_MONTSERRAT_40=y'

echo "Prepared: ${TARGET_DEFCONFIG}"
grep -E 'CONFIG_(EXAMPLES_AI_AGENT_VELA|LVX_USE_DEMO_FOCUSMATE|FOCUSMATE_)' "${TARGET_DEFCONFIG}"
