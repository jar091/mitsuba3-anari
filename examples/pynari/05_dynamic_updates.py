#!/usr/bin/env python3
# mitsuba-anari PyNARI example: retained-mode updates — re-render after
# changing the camera and the material without recreating any object.
# SPDX-License-Identifier: Apache-2.0

import sys

import numpy as np
import pynari as anari

WIDTH, HEIGHT = 256, 256


def build_scene(device):
    camera = device.newCamera("perspective")
    camera.setParameter("aspect", anari.FLOAT32, WIDTH / HEIGHT)
    camera.setParameter("position", anari.FLOAT32_VEC3, (0.0, 0.0, 2.0))
    camera.setParameter("direction", anari.FLOAT32_VEC3, (0.0, 0.0, -1.0))
    camera.setParameter("up", anari.FLOAT32_VEC3, (0.0, 1.0, 0.0))
    camera.commitParameters()

    vertex = np.array(
        [-0.8, -0.8, 0.0, 0.8, -0.8, 0.0, 0.0, 0.8, 0.0], dtype=np.float32
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

    return camera, material, frame


def center_pixel(frame):
    frame.render()
    color = frame.get("channel.color")
    return color[HEIGHT // 2, WIDTH // 2]


def main() -> int:
    device = anari.newDevice("mitsuba")
    camera, material, frame = build_scene(device)

    base = center_pixel(frame)
    print("baseline center:", base)
    if not int(base[0]) > 20:
        print("FAIL: baseline triangle not visible", file=sys.stderr)
        return 1

    # Update the material color: red -> green.
    material.setParameter("color", anari.FLOAT32_VEC3, (0.1, 0.8, 0.1))
    material.commitParameters()
    green = center_pixel(frame)
    print("after material update:", green)
    if not int(green[1]) > int(green[0]):
        print("FAIL: material update not visible", file=sys.stderr)
        return 1

    # Update the camera: look away, the triangle disappears from the center.
    camera.setParameter("direction", anari.FLOAT32_VEC3, (0.0, 0.0, 1.0))
    camera.commitParameters()
    away = center_pixel(frame)
    print("after camera update:", away)
    if int(away[0]) > 5 or int(away[1]) > 5:
        print("FAIL: camera update not visible", file=sys.stderr)
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
