#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
"""Copy the non-system PE DLL closure of a MinGW-built xanatoo.exe.

Usage: collect_dlls.py PATH/TO/xanatoo.exe DLL_SEARCH_DIR [DLL_SEARCH_DIR ...]
Uses objdump (or $OBJDUMP). Search directories may be package prefixes or bin
directories. Only reads imports; never runs the exe.
"""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SYSTEM = {
    "advapi32.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll",
    "cfgmgr32.dll", "crypt32.dll", "d3d11.dll", "dbghelp.dll",
    "dinput8.dll", "dwmapi.dll", "dxgi.dll", "gdi32.dll",
    "hid.dll", "imm32.dll", "iphlpapi.dll", "kernel32.dll",
    "msimg32.dll", "msvcrt.dll", "netapi32.dll", "ntdll.dll",
    "ole32.dll", "oleaut32.dll", "opengl32.dll", "psapi.dll",
    "rpcrt4.dll", "secur32.dll", "setupapi.dll", "shcore.dll",
    "shell32.dll", "shlwapi.dll", "user32.dll", "userenv.dll",
    "ucrtbase.dll", "uxtheme.dll", "version.dll", "winhttp.dll",
    "wininet.dll", "winmm.dll", "winspool.drv", "wldap32.dll",
    "ws2_32.dll", "wsock32.dll", "wtsapi32.dll", "xinput1_4.dll",
}


def imports(binary: Path) -> list[str]:
    text = subprocess.check_output(
        [os.environ.get("OBJDUMP", "objdump"), "-p", str(binary)],
        text=True, errors="replace"
    )
    return re.findall(r"^\s*DLL Name:\s*(\S+)\s*$", text, re.MULTILINE)


def collect(exe: Path, search_dirs: list[Path]) -> None:
    available = {
        p.name.lower(): p
        for directory in search_dirs
        for p in directory.rglob("*.dll")
    }
    seen: set[str] = set()
    todo = [exe]
    while todo:
        binary = todo.pop()
        for name in imports(binary):
            key = name.lower()
            if key in seen:
                continue
            seen.add(key)
            if key in SYSTEM or key.startswith(("api-ms-win-", "ext-ms-")):
                continue
            source = available.get(key)
            if source is None:
                raise SystemExit(f"Missing DLL {name} imported by {binary}")
            destination = exe.parent / source.name
            shutil.copy2(source, destination)
            print(f"Bundled {source.name}")
            todo.append(destination)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(f"Usage: {sys.argv[0]} EXE DLL_SEARCH_DIR [DLL_SEARCH_DIR ...]")
    collect(Path(sys.argv[1]).resolve(), [Path(p) for p in sys.argv[2:]])
