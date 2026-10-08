# Third-Party Software

This project builds against and/or redistributes the following third-party
software. Versions are pinned in `DEPENDENCIES.md` / `cmake/Dependencies.cmake`.

| Component | License | Usage |
|-----------|---------|-------|
| ANARI-SDK (Khronos Group) | Apache-2.0 | Linked: ANARI frontend + `helium` device foundation; SDK tools used in tests |
| Mitsuba 3 (EPFL / Realistic Graphics Lab) | BSD-3-Clause | Linked rendering backend (from Milestone 3). Mitsuba bundles further dependencies (Dr.Jit and others) — a full transitive license audit is a Milestone 12 deliverable |
| PyNARI (Ingo Wald) | Apache-2.0 | Test/example client only; not linked into the device library |

The retained-object-model source under `src/` is derived from the ANARI-SDK's
`examples/empty_helium_device` skeleton (Copyright The Khronos Group,
Apache-2.0), which is published as a template for building new helium-based
devices. Derived files keep their original Khronos copyright/SPDX headers.

Reference-only repositories (VisRTX, anari-cycles, Barney) are studied for
architectural patterns; no code is copied from them.

## Transitive components shipped in the install tree (Milestone 12 audit)

The self-contained install co-locates the Mitsuba runtime; the following
vendored components therefore ship as binaries. License names below reflect
each upstream project's published license; **verify against the license files
in the checked-out submodules (`ext/mitsuba3/ext/...`) before tagging a
release** — that final file-level pass is the remaining Milestone 12 step.

| Component (binary) | Upstream | License (published) |
|--------------------|----------|---------------------|
| mitsuba.dll + plugins/ + data/ | jar091/mitsuba3 (fork of mitsuba-renderer/mitsuba3) | BSD-3-Clause |
| drjit-core.dll, drjit-extra.dll | mitsuba-renderer/drjit | BSD-3-Clause |
| nanothread.dll | mitsuba-renderer/nanothread | BSD-3-Clause |
| struct-jit.dll | mitsuba-renderer (ext) | BSD-3-Clause |
| embree3.dll | RenderKit/embree | Apache-2.0 |
| IlmImf/Iex/Half/Imath (OpenEXR family, *-mitsuba.dll) | AcademySoftwareFoundation/openexr | BSD-3-Clause |
| png-mitsuba.dll | libpng | libpng/zlib-style |
| jpeg62.dll | libjpeg-turbo | BSD-3-Clause + IJG |
| zlib1.dll | zlib | zlib |
| pugixml.dll | pugixml | MIT |
| anari.dll, anari_library_debug/sink | KhronosGroup/ANARI-SDK | Apache-2.0 |
| (headers only) nanobind + tsl/robin_map, tinyformat, fastfloat | various | BSD-3-Clause / MIT / Boost-style |

The `data/` directory (srgb.coeff, ior/, sunsky/) ships under Mitsuba's
BSD-3-Clause terms.
