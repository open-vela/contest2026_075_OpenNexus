#!/usr/bin/env bash
# Keep simulator user data outside the build directory (build may regenerate images).
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
workspace_root="$(cd "$repo_root/.." && pwd)"
runtime_dir="${FOCUSMATE_RUNTIME_DIR:-$HOME/.local/share/focusmate-pixel}"
mkdir -p "$runtime_dir"
if [[ ! -f "$runtime_dir/data.bin" ]]; then
  cp "$workspace_root/nuttx/vela_data.bin" "$runtime_dir/data.bin"
fi
cd "$workspace_root"
export DISPLAY="${DISPLAY:-:0}"
exec ./emulator.sh nuttx -data "$runtime_dir/data.bin" -no-audio -skin 900x600 "$@"
