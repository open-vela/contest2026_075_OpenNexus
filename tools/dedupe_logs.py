#!/usr/bin/env python3
"""De-duplicate contest AI-coding logs.

The OpenCode collector plugin fires more than once per session (both the
session.idle and shutdown paths run its export handler) and appends instead of
overwriting, so every session file ends up containing the same events twice.
validate-log.py then reports "duplicate seq" as an anti-cheat violation.

This removes only *exact duplicate* events - identified by (session_id, seq) -
keeping the first occurrence and preserving every original line verbatim. It
never rewrites or reorders content, and it refreshes manifest.json so the
declared event_count matches reality.
"""
import json
import os
import sys
from collections import OrderedDict


def dedupe_file(path):
    with open(path, encoding="utf-8") as fh:
        raw = fh.read().splitlines()

    seen = set()
    kept = []
    dropped = 0
    for line in raw:
        s = line.strip()
        if not s:
            continue
        try:
            ev = json.loads(s)
        except json.JSONDecodeError:
            kept.append(line)
            continue
        key = (ev.get("session_id"), ev.get("seq"))
        if key in seen:
            dropped += 1
            continue
        seen.add(key)
        kept.append(line)

    if dropped:
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("\n".join(kept) + "\n")
    return len(kept), dropped


def main(root):
    total_kept = total_dropped = 0
    per_session = {}

    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            if not name.endswith(".jsonl"):
                continue
            path = os.path.join(dirpath, name)
            kept, dropped = dedupe_file(path)
            total_kept += kept
            total_dropped += dropped
            if kept or dropped:
                print(f"  {os.path.relpath(path, root)}: kept {kept}, dropped {dropped}")
            # remember live counts for the manifest
            sid = None
            first_ts = last_ts = None
            with open(path, encoding="utf-8") as fh:
                for line in fh:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        ev = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    sid = ev.get("session_id") or sid
                    ts = ev.get("ts")
                    if ts and first_ts is None:
                        first_ts = ts
                    if ts:
                        last_ts = ts
            if sid:
                per_session[sid] = {"count": kept, "first": first_ts, "last": last_ts}

    print(f"\ntotal: kept {total_kept}, dropped {total_dropped}")

    # refresh manifest.json
    mpath = os.path.join(root, os.path.basename(root), "manifest.json")
    if not os.path.exists(mpath):
        # manifest sits directly under logs/<login>/
        for d in os.listdir(root):
            cand = os.path.join(root, d, "manifest.json")
            if os.path.exists(cand):
                mpath = cand
                break
    if os.path.exists(mpath):
        with open(mpath, encoding="utf-8") as fh:
            man = json.load(fh, object_pairs_hook=OrderedDict)
        changed = False
        for sess in man.get("sessions", []):
            sid = sess.get("session_id")
            info = per_session.get(sid)
            if info and sess.get("event_count") != info["count"]:
                sess["event_count"] = info["count"]
                sess["raw_message_count"] = info["count"]
                if info["first"]:
                    sess["started_at"] = info["first"]
                if info["last"]:
                    sess["last_event_at"] = info["last"]
                changed = True
        if changed:
            with open(mpath, "w", encoding="utf-8") as fh:
                json.dump(man, fh, indent=2, ensure_ascii=False)
                fh.write("\n")
            print(f"manifest updated: {mpath}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "logs"))
