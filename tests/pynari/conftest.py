# Windows: Python 3.8+ does not use PATH to resolve extension-module DLL
# dependencies (anari.dll for pynari*.pyd). Directories listed in
# MITSUBA_ANARI_DLL_DIRS (os.pathsep-separated) are registered explicitly.
# The ANARI device library itself is still resolved through the OS loader
# search (PATH), which the CTest environment also provides.
# SPDX-License-Identifier: Apache-2.0

import os

if hasattr(os, "add_dll_directory"):
    for _dir in os.environ.get("MITSUBA_ANARI_DLL_DIRS", "").split(os.pathsep):
        if _dir and os.path.isdir(_dir):
            os.add_dll_directory(_dir)
