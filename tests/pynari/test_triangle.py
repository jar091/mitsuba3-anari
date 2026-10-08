# mitsuba-anari PyNARI test: triangle render through the public PyNARI API.
# Run via CTest (pynari.triangle) with MITSUBA_ANARI_BUILD_PYNARI_TESTS=ON,
# or directly with pytest once pynari + the device are on the loader path.
# SPDX-License-Identifier: Apache-2.0

import numpy as np
import pytest

anari = pytest.importorskip("pynari")

WIDTH, HEIGHT = 128, 128


def _make_device():
    return anari.newDevice("mitsuba")


def test_load_mitsuba_device():
    device = _make_device()
    assert device is not None


def test_triangle():
    device = _make_device()

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

    # World bounds come from the device (PyNARI warns and substitutes a unit
    # cube if the property is missing — the triangle spans exactly [-1,1] in
    # x/y and z=0, so check x extents which must reach +-1).
    bounds = world.getBounds()
    assert bounds[0] <= -1.0 and bounds[3] >= 1.0

    renderer = device.newRenderer("default")
    # Note: integer parameters are not set here — pynari@a860d0b built against
    # pybind11 2.13 (needed for Python 3.13) mis-dispatches int values to its
    # float overload (see PYNARI.md). The device default (16 spp) is used; the
    # INT32 path is covered by the native C++ render tests.
    renderer.setParameter(
        "background", anari.FLOAT32_VEC4, (0.0, 0.0, 0.2, 1.0)
    )
    renderer.commitParameters()

    frame = device.newFrame()
    # Tuple, not list: pynari's set_uint2 takes a tuple, and only the exact
    # match avoids the pybind11-2.13 float-overload mis-dispatch (PYNARI.md).
    frame.setParameter("size", anari.uint2, (WIDTH, HEIGHT))
    frame.setParameter(
        "channel.color", anari.DATA_TYPE, anari.UFIXED8_RGBA_SRGB
    )
    frame.setParameter("renderer", anari.RENDERER, renderer)
    frame.setParameter("camera", anari.CAMERA, camera)
    frame.setParameter("world", anari.WORLD, world)
    frame.commitParameters()

    frame.render()
    color = frame.get("channel.color")

    assert isinstance(color, np.ndarray)
    assert color.shape == (HEIGHT, WIDTH, 4)
    assert np.isfinite(color.astype(np.float64)).all()

    # Lit red triangle at the center; background at the corner.
    center = color[HEIGHT // 2, WIDTH // 2]
    assert int(center[0]) > 20
    assert int(center[0]) > int(center[1])
    corner = color[0, 0]
    assert int(corner[2]) > int(corner[0])  # bluish background
