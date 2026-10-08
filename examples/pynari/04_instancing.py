#!/usr/bin/env python3
# mitsuba-anari PyNARI example: one triangle group instanced twice with
# different transforms.
# SPDX-License-Identifier: Apache-2.0

import sys

import numpy as np
import pynari as anari

WIDTH, HEIGHT = 256, 256


def main() -> int:
    device = anari.newDevice("mitsuba")

    camera = device.newCamera("perspective")
    camera.setParameter("aspect", anari.FLOAT32, WIDTH / HEIGHT)
    camera.setParameter("position", anari.FLOAT32_VEC3, (0.0, 0.0, 2.5))
    camera.setParameter("direction", anari.FLOAT32_VEC3, (0.0, 0.0, -1.0))
    camera.setParameter("up", anari.FLOAT32_VEC3, (0.0, 1.0, 0.0))
    camera.commitParameters()

    vertex = np.array(
        [-0.6, -0.6, 0.0, 0.6, -0.6, 0.0, 0.0, 0.6, 0.0], dtype=np.float32
    )
    mesh = device.newGeometry("triangle")
    mesh.setParameter(
        "vertex.position",
        anari.ARRAY1D,
        device.newArray1D(anari.FLOAT32_VEC3, vertex),
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

    group = device.newGroup([surface])
    group.commitParameters()

    # Column-major 4x4 transforms translating the shared group left/right.
    def translate(tx):
        return (
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            tx, 0.0, 0.0, 1.0,
        )

    instances = []
    for tx in (-0.8, 0.8):
        inst = device.newInstance("transform")
        inst.setParameter("group", anari.GROUP, group)
        inst.setParameter("transform", anari.FLOAT32_MAT4, translate(tx))
        inst.commitParameters()
        instances.append(inst)

    world = device.newWorld()
    world.setParameterArray1D("instance", anari.INSTANCE, instances)
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

    left = color[HEIGHT // 2, WIDTH // 4]
    right = color[HEIGHT // 2, 3 * WIDTH // 4]
    print("left:", left, "right:", right)
    if not (int(left[0]) > 20 and int(right[0]) > 20):
        print("FAIL: expected both instances visible", file=sys.stderr)
        return 1

    try:
        from PIL import Image

        Image.fromarray(color, "RGBA").transpose(
            Image.Transpose.FLIP_TOP_BOTTOM
        ).save("04_instancing.png")
        print("wrote 04_instancing.png")
    except ImportError:
        pass

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
