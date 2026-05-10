#!/usr/bin/env python3
"""
patch_wled.py — Injects StairController into WLED's usermods_list.cpp

Usage:
    python patch_wled.py <path/to/wled/wled00/usermods_list.cpp>

The script is idempotent: running it twice produces the same result.
"""

import sys
import re

INCLUDE_MARKER  = "#ifdef USERMOD_STAIR_CONTROLLER"
INCLUDE_BLOCK   = """\
#ifdef USERMOD_STAIR_CONTROLLER
#include "../usermods/stair_controller/stair_controller.h"
#endif"""

REGISTER_MARKER = "usermods.add(new StairController())"
REGISTER_BLOCK  = """\
  #ifdef USERMOD_STAIR_CONTROLLER
  usermods.add(new StairController());
  #endif"""

def patch(path: str) -> None:
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    changed = False

    # ── 1. Add #include at the top of the include section ────────────
    if INCLUDE_MARKER not in src:
        # Insert before the first existing #ifdef USERMOD_ line
        m = re.search(r"(#ifdef USERMOD_)", src)
        if m:
            src = src[:m.start()] + INCLUDE_BLOCK + "\n\n" + src[m.start():]
        else:
            # Fallback: append at end of includes (before first non-comment non-include line)
            m2 = re.search(r"^(?!//|#include|#ifdef|#endif|\s*$)", src, re.MULTILINE)
            ins = m2.start() if m2 else len(src)
            src = src[:ins] + INCLUDE_BLOCK + "\n\n" + src[ins:]
        changed = True
        print(f"[patch] Added #include block for StairController")

    # ── 2. Register inside registerUsermods() ────────────────────────
    if REGISTER_MARKER not in src:
        # Find the opening brace of registerUsermods()
        m = re.search(r"void\s+registerUsermods\s*\(\s*\)\s*\{", src)
        if not m:
            print("[patch] ERROR: could not find registerUsermods() — please add manually:")
            print(REGISTER_BLOCK)
            sys.exit(1)
        insert_pos = m.end()
        src = src[:insert_pos] + "\n" + REGISTER_BLOCK + src[insert_pos:]
        changed = True
        print(f"[patch] Registered StairController in registerUsermods()")

    if not changed:
        print("[patch] Already patched — nothing to do.")
        return

    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print(f"[patch] Done: {path}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <path/to/usermods_list.cpp>")
        sys.exit(1)
    patch(sys.argv[1])
