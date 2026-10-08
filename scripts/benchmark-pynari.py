#!/usr/bin/env python3
"""Render-throughput benchmark for the mitsuba ANARI device, driven by PyNARI.

Builds one deterministic scene (a sphere grid over a quad floor, one
directional + one point light, physicallyBased material) and renders it at a
sweep of resolutions and sample counts, timing each render. The scene is
created solely through the ANARI API — Mitsuba is never imported.

The rendering backend is chosen with ANARI_MITSUBA_VARIANT (scalar_rgb,
llvm_ad_rgb, cuda_ad_rgb); PyNARI cannot set string device parameters, so the
device reads that environment variable (see PERFORMANCE.md).

Usage:
    python scripts/benchmark-pynari.py --variant cuda_ad_rgb --out results/
    python scripts/benchmark-pynari.py --variant scalar_rgb --quick

Each run writes <out>/<variant>.json with the raw timings and one PNG per
configuration, so CPU and GPU results can be diffed image-wise as well.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import sys
import time

# The variant must be set before the device library is loaded.
_PRESET_VARIANT = os.environ.get("ANARI_MITSUBA_VARIANT")


def build_scene(anari, device, args, width: int, height: int, samples: int):
    """Create the benchmark scene. Returns the committed frame."""
    import numpy as np

    camera = device.newCamera("perspective")
    camera.setParameter("aspect", anari.FLOAT32, width / height)
    camera.setParameter("position", anari.FLOAT32_VEC3, (0.0, 4.0, 6.5))
    camera.setParameter("direction", anari.FLOAT32_VEC3, (0.0, -0.55, -1.0))
    camera.setParameter("up", anari.FLOAT32_VEC3, (0.0, 1.0, 0.0))
    camera.setParameter("fovy", anari.FLOAT32, 0.6)
    camera.commitParameters()

    # --- sphere grid -------------------------------------------------------
    # Spheres sit on the floor and never touch each other, so the sample cost
    # per pixel stays comparable as --grid changes.
    n = args.grid
    step = 4.0 / max(n - 1, 1)
    r = 0.35 * step
    centers = np.array(
        [
            (-2.0 + i * step, r, -2.0 + j * step)
            for j in range(n)
            for i in range(n)
        ],
        dtype=np.float32,
    ).reshape(-1)
    radius = np.full(n * n, r, dtype=np.float32)

    spheres = device.newGeometry("sphere")
    spheres.setParameter(
        "vertex.position", anari.ARRAY1D,
        device.newArray1D(anari.FLOAT32_VEC3, centers))
    spheres.setParameter(
        "vertex.radius", anari.ARRAY1D,
        device.newArray1D(anari.FLOAT32, radius))
    spheres.commitParameters()

    metal = device.newMaterial("physicallyBased")
    metal.setParameter("baseColor", anari.FLOAT32_VEC3, (0.85, 0.55, 0.20))
    metal.setParameter("metallic", anari.FLOAT32, 0.9)
    metal.setParameter("roughness", anari.FLOAT32, 0.25)
    metal.commitParameters()

    sphere_surface = device.newSurface()
    sphere_surface.setParameter("geometry", anari.GEOMETRY, spheres)
    sphere_surface.setParameter("material", anari.MATERIAL, metal)
    sphere_surface.commitParameters()

    # --- floor -------------------------------------------------------------
    floor_v = np.array(
        [-6.0, 0.0, -6.0, 6.0, 0.0, -6.0, 6.0, 0.0, 6.0, -6.0, 0.0, 6.0],
        dtype=np.float32)
    floor_i = np.array([0, 1, 2, 3], dtype=np.uint32)

    floor = device.newGeometry("quad")
    floor.setParameter(
        "vertex.position", anari.ARRAY1D,
        device.newArray1D(anari.FLOAT32_VEC3, floor_v))
    floor.setParameter(
        "primitive.index", anari.ARRAY1D,
        device.newArray1D(anari.UINT32_VEC4, floor_i))
    floor.commitParameters()

    matte = device.newMaterial("matte")
    matte.setParameter("color", anari.FLOAT32_VEC3, (0.35, 0.36, 0.40))
    matte.commitParameters()

    floor_surface = device.newSurface()
    floor_surface.setParameter("geometry", anari.GEOMETRY, floor)
    floor_surface.setParameter("material", anari.MATERIAL, matte)
    floor_surface.commitParameters()

    # --- lights ------------------------------------------------------------
    sun = device.newLight("directional")
    sun.setParameter("direction", anari.FLOAT32_VEC3, (-0.4, -1.0, -0.5))
    sun.setParameter("irradiance", anari.FLOAT32, 2.5)
    sun.commitParameters()

    lamp = device.newLight("point")
    lamp.setParameter("position", anari.FLOAT32_VEC3, (3.0, 4.0, 3.0))
    lamp.setParameter("intensity", anari.FLOAT32, 30.0)
    lamp.setParameter("color", anari.FLOAT32_VEC3, (0.6, 0.7, 1.0))
    lamp.commitParameters()

    world = device.newWorld()
    world.setParameterArray1D(
        "surface", anari.SURFACE, [sphere_surface, floor_surface])
    world.setParameterArray1D("light", anari.LIGHT, [sun, lamp])
    world.commitParameters()

    renderer = device.newRenderer("default")
    renderer.setParameter("background", anari.FLOAT32_VEC4, (0.05, 0.06, 0.10, 1.0))
    renderer.setParameter("pixelSamples", anari.INT32, samples)
    renderer.commitParameters()

    frame = device.newFrame()
    frame.setParameter("size", anari.uint2, (width, height))
    frame.setParameter("channel.color", anari.DATA_TYPE, anari.UFIXED8_RGBA_SRGB)
    frame.setParameter("renderer", anari.RENDERER, renderer)
    frame.setParameter("camera", anari.CAMERA, camera)
    frame.setParameter("world", anari.WORLD, world)
    frame.commitParameters()
    return frame


def save_png(color, path: str) -> bool:
    try:
        from PIL import Image
    except ImportError:
        return False
    Image.fromarray(color, "RGBA").transpose(
        Image.Transpose.FLIP_TOP_BOTTOM).save(path)
    return True


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--variant", default=_PRESET_VARIANT or "scalar_rgb",
                   help="mitsuba variant (scalar_rgb, llvm_ad_rgb, cuda_ad_rgb)")
    p.add_argument("--out", default="perf-results", help="output directory")
    p.add_argument("--grid", type=int, default=12,
                   help="sphere grid edge length (grid^2 spheres, default 12)")
    p.add_argument("--repeat", type=int, default=3,
                   help="timed renders per configuration after the warm-up")
    p.add_argument("--quick", action="store_true",
                   help="one small configuration only (smoke test)")
    p.add_argument("--configs", default="",
                   help="explicit sweep, comma-separated WxHxSPP "
                        "(e.g. 512x512x16,1024x1024x64); overrides the default")
    p.add_argument("--label", default="", help="free-form note stored in the JSON")
    args = p.parse_args()

    if _PRESET_VARIANT and _PRESET_VARIANT != args.variant:
        print(f"ERROR: ANARI_MITSUBA_VARIANT={_PRESET_VARIANT} was already set "
              f"in the environment but --variant={args.variant} was requested. "
              f"The variant is read at device creation; re-run with a matching "
              f"environment.", file=sys.stderr)
        return 2
    os.environ["ANARI_MITSUBA_VARIANT"] = args.variant

    import numpy as np
    import pynari as anari

    if args.configs:
        configs = []
        for spec in args.configs.split(","):
            w, h, spp = spec.lower().split("x")
            configs.append((int(w), int(h), int(spp)))
    elif args.quick:
        configs = [(256, 256, 16)]
    else:
        configs = [
            (512, 512, 16),
            (1024, 1024, 16),
            (1024, 1024, 64),
            (1920, 1080, 64),
            (1920, 1080, 256),
        ]

    os.makedirs(args.out, exist_ok=True)

    device = anari.newDevice("mitsuba")
    if device is None:
        print("FAIL: could not create the 'mitsuba' ANARI device", file=sys.stderr)
        return 1

    results = []
    for (width, height, samples) in configs:
        frame = build_scene(anari, device, args, width, height, samples)

        t0 = time.perf_counter()
        frame.render()
        warmup = time.perf_counter() - t0

        times = []
        for _ in range(args.repeat):
            t0 = time.perf_counter()
            frame.render()
            times.append(time.perf_counter() - t0)

        color = frame.get("channel.color")
        assert color.shape == (height, width, 4)
        assert np.isfinite(color.astype(np.float64)).all()

        png = os.path.join(
            args.out, f"{args.variant}_{width}x{height}_spp{samples}.png")
        wrote = save_png(color, png)

        best = min(times)
        mean = sum(times) / len(times)
        mrays = width * height * samples / best / 1e6
        results.append({
            "width": width, "height": height, "samples": samples,
            "spheres": args.grid * args.grid,
            "warmup_s": warmup, "best_s": best, "mean_s": mean,
            "times_s": times, "msamples_per_s": mrays,
            "png": png if wrote else None,
        })
        print(f"{args.variant}  {width}x{height} spp={samples:<4} "
              f"warmup={warmup:8.3f}s  best={best:8.3f}s  mean={mean:8.3f}s  "
              f"{mrays:7.2f} Msample/s" + ("" if wrote else "  (no PNG: Pillow missing)"))

    payload = {
        "variant": args.variant,
        "label": args.label,
        "host": platform.node(),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "grid": args.grid,
        "repeat": args.repeat,
        "results": results,
    }
    json_path = os.path.join(args.out, f"{args.variant}.json")
    with open(json_path, "w") as f:
        json.dump(payload, f, indent=2)
    print(f"wrote {json_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
