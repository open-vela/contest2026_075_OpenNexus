#!/usr/bin/env python3
"""Generate a C byte array from skills/focus-planner.md."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = root / "skills" / "focus-planner.md"
dst = root / "app" / "focusmate" / "agent" / "focus_skill_embed.h"
data = src.read_bytes()
rows = []
for i in range(0, len(data), 12):
    chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
    rows.append("  " + chunk + ",")
body = "\n".join(rows)
dst.write_text(
    "/* Auto-generated from skills/focus-planner.md. Do not edit manually. */\n"
    "#ifndef FOCUS_SKILL_EMBED_H\n"
    "#define FOCUS_SKILL_EMBED_H\n\n"
    "static const unsigned char g_focus_planner_skill[] = {\n"
    f"{body}\n"
    "  0x00\n"
    "};\n\n"
    "#endif /* FOCUS_SKILL_EMBED_H */\n",
    encoding="ascii",
)
print(f"generated {dst} ({len(data)} bytes)")
