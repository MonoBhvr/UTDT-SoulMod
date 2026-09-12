#!/usr/bin/env python3
"""Shared fixtures for the execution-copy Python contract and hardening tests.

Mirrors ``converter/SoulMod.ExecutionCopy.Tests/TestSupport.cs``. Holds the path bootstrap,
the fake-install builder, the temp-tree fixture, and the assert helper so both test files stay
under the repository LOC ceiling without duplicating setup.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import execution_copy_contract as ecc  # noqa: E402

EXE_NAME = "FakeGame.exe"
DATA_WIN_NAME = "data.win"


def make_fake_install(root: str, with_extra: bool = False) -> ecc.GameInstall:
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, EXE_NAME), "wb") as handle:
        handle.write(bytes([0x4D, 0x5A, 0x90, 0x00]))
    with open(os.path.join(root, DATA_WIN_NAME), "w", encoding="utf-8") as handle:
        handle.write("FORM fake data.win payload")
    if with_extra:
        os.makedirs(os.path.join(root, "mus"), exist_ok=True)
        with open(os.path.join(root, "mus", "legend.ogg"), "w", encoding="utf-8") as handle:
            handle.write("audio")
        with open(os.path.join(root, "audiogroup1.dat"), "w", encoding="utf-8") as handle:
            handle.write("ag1")
    return ecc.GameInstall(root, EXE_NAME, DATA_WIN_NAME)


class TempTree:
    def __init__(self):
        self.path = tempfile.mkdtemp(prefix="soulmod-exec-test-")

    def cleanup(self):
        if os.path.isdir(self.path):
            shutil.rmtree(self.path, ignore_errors=True)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def run(tests) -> int:
    failures = 0
    for test in tests:
        try:
            test()
            print(f"PASS: {test.__name__}")
        except Exception as exc:
            failures += 1
            print(f"FAIL: {test.__name__}: {exc}")
    print(f"RESULT: {'PASS' if failures == 0 else 'FAIL'} ({len(tests) - failures}/{len(tests)} passed)")
    return 1 if failures else 0
