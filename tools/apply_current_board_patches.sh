#!/usr/bin/env bash
set -euo pipefail
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORKSPACE=${1:-$(dirname "$REPO")}
apply_once() {
  local repo=$1 patch=$2
  if git -C "$repo" apply --reverse --check "$patch" 2>/dev/null; then
    echo "Already applied: $(basename "$patch")"
  elif git -C "$repo" apply --check "$patch"; then
    git -C "$repo" apply "$patch"
    echo "Applied: $(basename "$patch")"
  else
    echo "Cannot safely apply $(basename "$patch"); inspect partial patches first." >&2
    return 1
  fi
}
apply_once "$WORKSPACE/vendor/sifli" "$REPO/patches/board/current-sifli.patch"
apply_once "$WORKSPACE/nuttx" "$REPO/patches/board/current-nuttx.patch"
echo 'See docs/board_bringup/CURRENT_BUILD_zh-cn.md for opt-in build flags.'
