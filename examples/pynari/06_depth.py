#!/usr/bin/env python3
# mitsuba-anari PyNARI example: read back the depth channel.
# Note: reading 'channel.depth' through pynari requires the patched pynari
# build shipped with this project (see PYNARI.md) — upstream pynari@a860d0b
# only reads 'channel.color'.
# SPDX-License-Identifier: Apache-2.0

import sys

import numpy as np
import pynari as anari

WIDTH, HEIGHT = 128, 128


def main() -> int:
    device = anari.newDevice("mitsuba")

    camera = device.newCamera("perspective")
    camera.setParameter("aspect", anari.FLOAT32, WIDTH / HEIGHT)
    camera.setParameter("position", anari.FLOAT32_VEC3, (0.0, 0.0, 2.0))
    camera.setParameter("direction", anari.FLOAT32_VEC3, (0.0, 0.0, -1.0))
    camera.setParameter("up", anari.FLOAT32_VEC3, (0.0, 1.0, 0.0))
    camera.commitParameters()

    # Camera-facing quad at z=0.
    vertex = np.array(
        [-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 1.0, 1.0, 0.0, -1.0, 1.0, 0.0],
        dtype=np.float32,
    )
    index = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)
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
    material.commitParameters()

    surface = device.newSurface()
    surface.setParameter("geometry", anari.GEOMETRY, mesh)
    surface.setParameter("material", anari.MATERIAL, material)
    surface.commitParameters()

    light = device.newLight("directional")
    light.setParameter("direction", anari.FLOAT32_VEC3, (0.0, 0.0, -1.0))
    light.commitParameters()

    world = device.newWorld()
    world.setParameterArray1D("surface", anari.SURFACE, [surface])
    world.setParameterArray1D("light", anari.LIGHT, [light])
    world.commitParameters()

    renderer = device.newRenderer("default")
    renderer.commitParameters()

    frame = device.newFrame()
    frame.setParameter("size", anari.uint2, (WIDTH, HEIGHT))
    frame.setParameter("channel.color", anari.DATA_TYPE, anari.UFIXED8_RGBA_SRGB)
    frame.setParameter("channel.depth", anari.DATA_TYPE, anari.FLOAT32)
    frame.setParameter("renderer", anari.RENDERER, renderer)
    frame.setParameter("camera", anari.CAMERA, camera)
    frame.setParameter("world", anari.WORLD, world)
    frame.commitParameters()

    frame.render()
    depth = frame.get("channel.depth")

    assert isinstance(depth, np.ndarray)
    assert depth.shape == (HEIGHT, WIDTH)
    center = float(depth[HEIGHT // 2, WIDTH // 2])
    corner = float(depth[0, 0])
    print(f"depth: center {center:.3f}, corner {corner}")
    if not (1.9 < center < 2.1 and np.isinf(corner)):
        print("FAIL: unexpected depth values", file=sys.stderr)
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
