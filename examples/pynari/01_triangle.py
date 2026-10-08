#!/usr/bin/env python3
# mitsuba-anari PyNARI example: render one matte triangle with the "mitsuba"
# ANARI device and save the image. The scene is created solely through
# ANARI/PyNARI — Mitsuba is never imported.
# SPDX-License-Identifier: Apache-2.0
#
# Prerequisites (see PYNARI.md): pynari importable, anari_library_mitsuba and
# mitsuba runtime discoverable by the OS loader (PATH / LD_LIBRARY_PATH).

import os
import sys
import time

import numpy as np
import pynari as anari

WIDTH, HEIGHT = 256, 256


def main() -> int:
    device = anari.newDevice("mitsuba")
    if device is None:
        print("FAIL: could not create 'mitsuba' ANARI device", file=sys.stderr)
        return 1

    # Rendering-variant selection (vendor extension
    # ANARI_MITSUBA_DEVICE_VARIANT): pynari cannot set string device
    # parameters, so the device also honors the ANARI_MITSUBA_VARIANT
    # environment variable — e.g. set it to "cuda_ad_rgb" for GPU rendering.
    # The device falls back to scalar_rgb with a warning when the requested
    # backend is unavailable at runtime.
    variant = os.environ.get("ANARI_MITSUBA_VARIANT", "")
    if variant:
        print(f"requested mitsuba variant (via env): {variant}")

    camera = device.newCamera("perspective")
    camera.setParameter("aspect", anari.FLOAT32, WIDTH / HEIGHT)
    camera.setParameter("position", anari.FLOAT32_VEC3, (0.0, 0.0, 2.0))
    camera.setParameter("direction", anari.FLOAT32_VEC3, (0.0, 0.0, -1.0))
    camera.setParameter("up", anari.FLOAT32_VEC3, (0.0, 1.0, 0.0))
    camera.commitParameters()

    vertex = np.array(
        [-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 0.0, 1.0, 0.0], dtype=np.float32
    )
    index = np.array([0, 1, 2], dtype=np.uint32)

    mesh = device.newGeometry("triangle")
    mesh.setParameter(
        "vertex.position",
        anari.ARRAY1D,
        device.newArray1D(anari.FLOAT32_VEC3, vertex),
    )
    mesh.setParameter(
        "primitive.index",
        anari.ARRAY1D,
        device.newArray1D(anari.UINT32_VEC3, index),
    )
    mesh.commitParameters()

    material = device.newMaterial("matte")
    material.setParameter("color", anari.FLOAT32_VEC3, (0.8, 0.1, 0.1))
    material.commitParameters()

    surface = device.newSurface()
    surface.setParameter("geometry", anari.GEOMETRY, mesh)
    surface.setParameter("material", anari.MATERIAL, material)
    surface.commitParameters()

    light = device.newLight("directional")
    light.setParameter("direction", anari.FLOAT32_VEC3, (0.0, 0.0, -1.0))
    light.setParameter("irradiance", anari.FLOAT32, 3.0)
    light.commitParameters()

    world = device.newWorld()
    world.setParameterArray1D("surface", anari.SURFACE, [surface])
    world.setParameterArray1D("light", anari.LIGHT, [light])
    world.commitParameters()

    renderer = device.newRenderer("default")
    # pixelSamples (INT32) is left at its default: pynari built against
    # pybind11 2.13 (Python 3.13) mis-dispatches ints to its float overload
    # (see PYNARI.md).
    renderer.setParameter("background", anari.FLOAT32_VEC4, (0.0, 0.0, 0.2, 1.0))
    renderer.commitParameters()

    frame = device.newFrame()
    frame.setParameter("size", anari.uint2, (WIDTH, HEIGHT))
    frame.setParameter(
        "channel.color", anari.DATA_TYPE, anari.UFIXED8_RGBA_SRGB
    )
    frame.setParameter("renderer", anari.RENDERER, renderer)
    frame.setParameter("camera", anari.CAMERA, camera)
    frame.setParameter("world", anari.WORLD, world)
    frame.commitParameters()

    t0 = time.perf_counter()
    frame.render()
    print(f"first frame (includes backend warm-up): {time.perf_counter() - t0:.2f} s")
    t0 = time.perf_counter()
    frame.render()
    print(f"second frame: {time.perf_counter() - t0:.2f} s")
    color = frame.get("channel.color")

    assert isinstance(color, np.ndarray)
    assert color.shape == (HEIGHT, WIDTH, 4)
    assert np.isfinite(color.astype(np.float64)).all()
    # The lit red triangle must be visible at the image center.
    center = color[HEIGHT // 2, WIDTH // 2]
    print("center pixel:", center)
    if not (int(center[0]) > 20 and int(center[0]) > int(center[1])):
        print("FAIL: expected a lit red triangle at the center", file=sys.stderr)
        return 1

    try:
        from PIL import Image

        Image.fromarray(color, "RGBA").transpose(
            Image.Transpose.FLIP_TOP_BOTTOM
        ).save("01_triangle.png")
        print("wrote 01_triangle.png")
    except ImportError:
        print("PIL not installed; skipping image save")

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
