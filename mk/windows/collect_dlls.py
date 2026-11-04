#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
"""Copy the non-system PE DLL closure of a MinGW-built xanatoo.exe.

Usage: collect_dlls.py PATH/TO/xanatoo.exe PATH/TO/mingw/bin
Requires the MinGW objdump in PATH. Only reads imports; never runs the exe.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

SYSTEM = {
    "advapi32.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll",
    "crypt32.dll", "dwmapi.dll", "gdi32.dll", "imm32.dll",
    "kernel32.dll", "msvcrt.dll", "ntdll.dll", "ole32.dll",
    "oleaut32.dll", "opengl32.dll", "rpcrt4.dll", "secur32.dll",
    "setupapi.dll", "shell32.dll", "shlwapi.dll", "user32.dll",
    "userenv.dll", "ucrtbase.dll", "version.dll", "winmm.dll",
    "ws2_32.dll", "wtsapi32.dll",
}


def imports(binary: Path) -> list[str]:
    text = subprocess.check_output(
        ["objdump", "-p", str(binary)], text=True, errors="replace"
    )
    return re.findall(r"^\s*DLL Name:\s*(\S+)\s*$", text, re.MULTILINE)


def collect(exe: Path, mingw_bin: Path) -> None:
    available = {p.name.lower(): p for p in mingw_bin.glob("*.dll")}
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
    if len(sys.argv) != 3:
        raise SystemExit(f"Usage: {sys.argv[0]} EXE MINGW_BIN")
    collect(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
