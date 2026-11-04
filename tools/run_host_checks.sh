#!/usr/bin/env bash
set -euo pipefail
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORKSPACE=${1:-$(dirname "$REPO")}
CHECK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/focusmate-checks.XXXXXX")
APP="$REPO/app/focusmate"
CJSON="$WORKSPACE/apps/netutils/cjson/cJSON"
AGENT="$WORKSPACE/apps/packages/ai_agent"
mkdir -p "$CHECK_DIR/nuttx"
printf '#define OK 0\n#define ERROR -1\n' > "$CHECK_DIR/nuttx/config.h"
cc -std=c11 -Wall -Wextra -Werror -I "$APP" "$REPO/tests/test_focus_flow.c" "$APP/core/focus_flow.c" -o "$CHECK_DIR/flow"
cc -std=c11 -Wall -Wextra -Werror -I "$APP" "$REPO/tests/test_voice_flow.c" "$APP/core/focus_flow.c" -o "$CHECK_DIR/voice"
cc -std=c11 -Wall -Wextra -Werror -I "$APP/network" "$REPO/tests/test_ble_frame.c" "$APP/network/ble_frame.c" -o "$CHECK_DIR/frame"
cc -D_DEFAULT_SOURCE -std=c11 -Wall -Wextra -I "$APP" -I "$CJSON" "$REPO/tests/test_reminder_prompt.c" "$APP/reminder/prompt_state.c" "$APP/core/focus_flow.c" "$CJSON/cJSON.c" -lm -o "$CHECK_DIR/prompt"
cc -D_DEFAULT_SOURCE -std=c11 -I "$CHECK_DIR" -I "$AGENT/include" -I "$AGENT/src" -I "$CJSON" "$REPO/tests/test_cron_scheduler.c" "$CJSON/cJSON.c" -pthread -lm -o "$CHECK_DIR/cron"
for test in flow voice frame prompt cron; do "$CHECK_DIR/$test"; done
echo "Host checks passed; binaries: $CHECK_DIR"
