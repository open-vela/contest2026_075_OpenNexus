#!/usr/bin/env bash
# Regenerate FocusMate's 24px CJK font.
#
# The built-in lv_font_simsun_16_cjk is too small to read on the 390x450
# panel and only carries ~1100 glyphs, so console-typed goals could not be
# rendered.  This produces a 24px font covering ASCII plus the 3755 GB2312
# level-1 hanzi.
#
# Requires lv_font_conv (npm i -g lv_font_conv).
set -eu
WS=${WS:-$HOME/openvela-workspace}
OUT=$WS/contest2026_075_OpenNexus/app/focusmate/ui/lv_font_simsun_24_cjk.c
SIMSUN=$WS/apps/graphics/lvgl/lvgl/scripts/built_in_font/SimSun.woff

python3 - <<'PY'
chars = set(chr(c) for c in range(0x20, 0x7F))
for b1 in range(0xB0, 0xD8):          # GB2312 level-1: 3755 common hanzi
    for b2 in range(0xA1, 0xFF):
        try:
            ch = bytes([b1, b2]).decode('gb2312')
        except UnicodeDecodeError:
            continue
        if '\u4e00' <= ch <= '\u9fff':
            chars.add(ch)
open('/tmp/charset.txt', 'w', encoding='utf-8').write(''.join(sorted(chars)))
print(f'characters: {len(chars)}')
PY

lv_font_conv --font "$SIMSUN" --size 24 --bpp 2 --format lvgl --no-compress \
  --symbols "$(cat /tmp/charset.txt)" -o "$OUT"
echo "wrote $OUT"
