#!/usr/bin/env python3
# mitsuba-anari PyNARI example: a quad textured through an image2D sampler.
# SPDX-License-Identifier: Apache-2.0

import sys

import numpy as np
import pynari as anari

WIDTH, HEIGHT = 256, 256


def main() -> int:
    device = anari.newDevice("mitsuba")

    camera = device.newCamera("perspective")
    camera.setParameter("aspect", anari.FLOAT32, WIDTH / HEIGHT)
    camera.setParameter("position", anari.FLOAT32_VEC3, (0.0, 0.0, 2.0))
    camera.setParameter("direction", anari.FLOAT32_VEC3, (0.0, 0.0, -1.0))
    camera.setParameter("up", anari.FLOAT32_VEC3, (0.0, 1.0, 0.0))
    camera.commitParameters()

    # Camera-facing unit quad with UVs.
    vertex = np.array(
        [-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 1.0, 1.0, 0.0, -1.0, 1.0, 0.0],
        dtype=np.float32,
    )
    uv = np.array([0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0], dtype=np.float32)
    index = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)

    mesh = device.newGeometry("triangle")
    mesh.setParameter(
        "vertex.position",
        anari.ARRAY1D,
        device.newArray1D(anari.FLOAT32_VEC3, vertex),
    )
    mesh.setParameter(
        "vertex.attribute0",
        anari.ARRAY1D,
        device.newArray1D(anari.FLOAT32_VEC2, uv),
    )
    mesh.setParameter(
        "primitive.index",
        anari.ARRAY1D,
        device.newArray1D(anari.UINT32_VEC3, index),
    )
    mesh.commitParameters()

    # 8x8 checkerboard texture.
    checker = np.zeros((8, 8, 4), dtype=np.float32)
    checker[..., 3] = 1.0
    for y in range(8):
        for x in range(8):
            if (x + y) % 2 == 0:
                checker[y, x, 0:3] = (0.9, 0.8, 0.2)
            else:
                checker[y, x, 0:3] = (0.1, 0.1, 0.4)
    image = device.newArray2D(anari.FLOAT32_VEC4, checker)

    sampler = device.newSampler("image2D")
    sampler.setParameter("image", anari.ARRAY2D, image)
    sampler.setParameter("inAttribute", anari.STRING, "attribute0")
    sampler.setParameter("filter", anari.STRING, "nearest")
    sampler.commitParameters()

    material = device.newMaterial("matte")
    material.setParameter("color", anari.SAMPLER, sampler)
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
    renderer.setParameter("background", anari.FLOAT32_VEC4, (0.0, 0.0, 0.0, 1.0))
    renderer.commitParameters()

    frame = device.newFrame()
    frame.setParameter("size", anari.uint2, (WIDTH, HEIGHT))
    frame.setParameter("channel.color", anari.DATA_TYPE, anari.UFIXED8_RGBA_SRGB)
    frame.setParameter("renderer", anari.RENDERER, renderer)
    frame.setParameter("camera", anari.CAMERA, camera)
    frame.setParameter("world", anari.WORLD, world)
    frame.commitParameters()

    frame.render()
    color = frame.get("channel.color")
    assert color.shape == (HEIGHT, WIDTH, 4)

    # Adjacent checker cells must differ.
    a = color[HEIGHT // 2, WIDTH // 2 - WIDTH // 8]
    b = color[HEIGHT // 2, WIDTH // 2 + WIDTH // 8]
    print("checker cells:", a, b)
    if abs(int(a[0]) - int(b[0])) < 20:
        print("FAIL: checkerboard not visible", file=sys.stderr)
        return 1

    try:
        from PIL import Image

        Image.fromarray(color, "RGBA").transpose(
            Image.Transpose.FLIP_TOP_BOTTOM
        ).save("03_textured_quad.png")
        print("wrote 03_textured_quad.png")
    except ImportError:
        pass

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
