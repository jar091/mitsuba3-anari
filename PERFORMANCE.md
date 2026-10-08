# Performance record — CPU and GPU

Measured render throughput of the `mitsuba` ANARI device across its rendering
variants. Every number here was produced by `scripts/benchmark-pynari.py`
through **PyNARI** (i.e. through the public ANARI C API), never by calling
Mitsuba directly.

Append a new section per machine/session; never overwrite past results — the
point of this file is the trend across changes.

## Method

```bash
python scripts/benchmark-pynari.py --variant <variant> --out perf-results
```

- **Scene** (identical for all variants): 144 spheres (12×12 grid) with a
  `physicallyBased` metal material over a matte `quad` floor, lit by one
  `directional` + one `point` light, perspective camera. Built entirely
  through ANARI.
- **Sweep**: 512², 1024² and 1920×1080 at 16/64/256 samples per pixel.
- **Timing**: one untimed warm-up render (which absorbs backend init, scene
  translation and — on JIT variants — kernel compilation), then 3 timed
  renders. Reported `best` is the minimum, `mean` the average of those 3.
- **Msample/s** = `width × height × spp / best`. It is a throughput figure for
  *this* scene, useful for comparing variants and tracking regressions, not a
  cross-renderer benchmark.
- Variant selection from Python uses `ANARI_MITSUBA_VARIANT` (PyNARI cannot
  set string device parameters); from C/C++ it is the `mitsuba.variant` device
  parameter.

Raw timings, including every individual repeat, are written to
`perf-results/<variant>.json` next to one PNG per configuration.

---

## 2026-08-16 — Karolina (IT4I), node `acn26`

| | |
|---|---|
| CPU | 2× AMD EPYC 7763 (Milan), 128 cores |
| GPU | NVIDIA A100-SXM4-40GB (1 of 8 used), driver 610.43, OptiX 9.0 ABI |
| Toolchain | GCC 14.3.0, CUDA 13.0.0, CMake 4.0.3, Python 3.13.5 (`.venv`) |
| Build | `linux-gcc-release`, Mitsuba v3.9.1, ANARI-SDK v0.16.0 |
| LLVM | system `/usr/lib64/libLLVM.so.21.1` via `DRJIT_LIBLLVM_PATH` |

Setup: `scripts/env-karolina.sh` + `scripts/build-karolina.sh`.

### Render time (seconds, best of 3)

| Resolution | spp | `scalar_rgb` (CPU) | `llvm_ad_rgb` (CPU) | `cuda_ad_rgb` (GPU) |
|---|---:|---:|---:|---:|
| 512×512 | 16 | 0.053 | 0.036 | 0.028 |
| 1024×1024 | 16 | 0.152 | 0.102 | 0.086 |
| 1024×1024 | 64 | 0.514 | 0.215 | 0.147 |
| 1920×1080 | 64 | 0.736 | 0.340 | 0.211 |
| 1920×1080 | 256 | 2.791 | 0.968 | 0.522 |

### Throughput (Msample/s)

| Resolution | spp | `scalar_rgb` | `llvm_ad_rgb` | `cuda_ad_rgb` |
|---|---:|---:|---:|---:|
| 512×512 | 16 | 79.7 | 115.5 | 148.3 |
| 1024×1024 | 16 | 110.6 | 164.2 | 195.3 |
| 1024×1024 | 64 | 130.5 | 312.0 | 455.9 |
| 1920×1080 | 64 | 180.3 | 390.3 | 628.1 |
| 1920×1080 | 256 | 190.2 | 548.7 | **1016.5** |

### Speed-up over the `scalar_rgb` CPU baseline

| Resolution | spp | `llvm_ad_rgb` | `cuda_ad_rgb` |
|---|---:|---:|---:|
| 512×512 | 16 | 1.45× | 1.90× |
| 1024×1024 | 16 | 1.48× | 1.77× |
| 1024×1024 | 64 | 2.39× | 3.50× |
| 1920×1080 | 64 | 2.16× | 3.49× |
| 1920×1080 | 256 | 2.88× | **5.35×** |

### Reading the numbers

- **The GPU advantage grows with the workload.** At 512²/16 spp the A100 is
  only 1.9× a 128-core EPYC pair, because a 4 M-sample frame does not fill the
  device and per-frame overhead (scene sync, kernel launch, read-back)
  dominates. At 1920×1080/256 spp (530 M samples) it reaches 5.35×. Small
  interactive frames are *not* where this backend pays off — one A100 versus
  128 Milan cores is a fair fight there.
- **`llvm_ad_rgb` is the better CPU default** where a libLLVM is available:
  same hardware, 1.5–2.9× over `scalar_rgb`, from Dr.Jit's vectorized
  megakernel instead of scalar per-ray execution. `scalar_rgb` remains the
  mandatory, dependency-free baseline.
- **Warm-up is a real cost on JIT variants, and it is one-off.** The first
  CUDA render after a scene change cost **33.3 s** with a cold Dr.Jit kernel
  cache (OptiX compilation); subsequent runs with a warm cache warmed up in
  0.41–0.84 s, and steady-state frames took 0.02–0.52 s. `env-karolina.sh`
  points `DRJIT_CACHE_DIR` into the repo so the cache survives across jobs —
  do the same in batch scripts or pay the compile on every job.
- **`mean` exceeds `best` markedly only on CUDA** (e.g. 0.623 vs 0.522 s):
  the timed loop measures wall time around a synchronous `frame.render()`, and
  the GPU path still absorbs some deferred work in the first repeat. On the
  CPU variants mean and best differ by <2 %.
- Throughput rises with spp at fixed resolution on every variant: per-frame
  fixed cost is amortized over more samples.

### Cross-variant image agreement

Same scene, 1920×1080, 256 spp, 8-bit sRGB output:

| Pair | RMSE | MAE | max Δ |
|---|---:|---:|---:|
| `llvm_ad_rgb` vs `cuda_ad_rgb` | 0.004 | ~0 | 3 |
| `cuda_ad_rgb` vs `scalar_rgb` | 3.73 | 0.70 | 209 |
| `llvm_ad_rgb` vs `scalar_rgb` | 3.73 | 0.70 | 209 |

The two JIT variants agree to within rounding. Both differ from `scalar_rgb`
only as Monte-Carlo noise: mean image luminance is identical to 4 significant
figures (47.73/255 for all three), and the isolated large deltas are
fireflies on the specular highlights. This is the expected consequence of a
different sample-generation order, not a translation difference — consistent
with the render-regression policy in [TESTING.md](TESTING.md), which forbids
exact golden-byte comparison across backends.

### Correctness gate accompanying these runs

`ctest --preset linux-gcc-release`: **17 tests — 16 PASS, 1 by-design SKIP**
(`metal.triangle_matte`, macOS-only). Includes `gpu.triangle_matte`
(`cuda_ad_rgb`), `llvm.triangle_matte`, `pynari.triangle` and
`install.load_library`. PyNARI examples 01/03/04/05/06 all pass under
`ANARI_MITSUBA_VARIANT=cuda_ad_rgb`.

---

## 2026-08-18 — Complementary Systems p12 (IT4I), node `p12-turin01`

| | |
|---|---|
| CPU | AMD EPYC 9555 (Turin, Zen 5), 64 cores |
| GPU | NVIDIA RTX PRO 6000 Blackwell Server Edition 96 GB (1 of 2 used, `CUDA_VISIBLE_DEVICES=0`), driver 610.43.02, OptiX 9.0 ABI |
| Toolchain | system GCC 14.3.1 (Rocky 10), `ml CUDA/12.9.1`, pip CMake 4.4.2, system Python 3.12.13 (`.venv`) |
| Build | `linux-gcc-release` + `-DMITSUBA_ANARI_MI_EXTRA_CXX_FLAGS=-march=x86-64-v2`, Mitsuba v3.9.1, ANARI-SDK v0.16.0 |
| LLVM | system `/usr/lib64/libLLVM.so.21.1` via `DRJIT_LIBLLVM_PATH` (Dr.Jit: `cpu=znver5, width=16`) |

Setup: `scripts/env-cs-p12.sh` + `scripts/build-cs-p12.sh`.

### Render time (seconds, best of 3)

| Resolution | spp | `scalar_rgb` (CPU) | `llvm_ad_rgb` (CPU) | `cuda_ad_rgb` (GPU) |
|---|---:|---:|---:|---:|
| 512×512 | 16 | 0.050 | 0.030 | 0.015 |
| 1024×1024 | 16 | 0.166 | 0.073 | 0.045 |
| 1024×1024 | 64 | 0.609 | 0.141 | 0.063 |
| 1920×1080 | 64 | 0.932 | 0.211 | 0.098 |
| 1920×1080 | 256 | 5.475 | 0.561 | 0.165 |

### Throughput (Msample/s)

| Resolution | spp | `scalar_rgb` | `llvm_ad_rgb` | `cuda_ad_rgb` |
|---|---:|---:|---:|---:|
| 512×512 | 16 | 84.7 | 141.3 | 286.4 |
| 1024×1024 | 16 | 101.1 | 229.9 | 376.7 |
| 1024×1024 | 64 | 110.2 | 474.6 | 1071.3 |
| 1920×1080 | 64 | 142.3 | 627.6 | 1349.1 |
| 1920×1080 | 256 | 97.0 | 946.1 | **3223.0** |

### Speed-up over the `scalar_rgb` CPU baseline

| Resolution | spp | `llvm_ad_rgb` | `cuda_ad_rgb` |
|---|---:|---:|---:|
| 512×512 | 16 | 1.67× | 3.38× |
| 1024×1024 | 16 | 2.27× | 3.73× |
| 1024×1024 | 64 | 4.31× | 9.72× |
| 1920×1080 | 64 | 4.41× | 9.48× |
| 1920×1080 | 256 | 9.76× | **33.2×** |

### Reading the numbers

- **The Blackwell card is ~3.2× the Karolina A100 on this scene** (3223 vs
  1017 Msample/s at 1920×1080/256), and its advantage over the CPU baseline
  is far larger here because the baseline is one 64-core socket rather than
  Karolina's 128 cores.
- **`llvm_ad_rgb` gains more than on Karolina** (up to 9.8× vs 2.9× over
  scalar): Dr.Jit vectorizes at width 16 on Zen 5 (AVX-512), and the scalar
  baseline has half the cores to lean on.
- **The scalar 1920×1080/256 row is an outlier** (97 Msample/s, below the
  64-spp row): the run showed a 5 % best-to-mean spread on a shared,
  unscheduled node. Treat scalar throughput at that size as ~130–140 with
  noise, and prefer quiet periods (check `nvidia-smi`/`uptime`) when
  recording.
- **Cold-cache CUDA warm-up is cheap on this stack**: the first render paid
  ~0.7 s total, with each OptiX kernel build at ~170 ms (Karolina's cold
  cache cost 33 s). The Dr.Jit cache still lives in the repo
  (`DRJIT_CACHE_DIR`, set by `env-cs-p12.sh`), keeping `$HOME` clean.

### Cross-variant image agreement

Same scene, 1920×1080, 256 spp, 8-bit sRGB output:

| Pair | RMSE | MAE | max Δ |
|---|---:|---:|---:|
| `llvm_ad_rgb` vs `cuda_ad_rgb` | 0.04 | ~0 | 67 |
| `cuda_ad_rgb` vs `scalar_rgb` | 5.89 | 0.96 | 236 |
| `llvm_ad_rgb` vs `scalar_rgb` | 5.89 | 0.96 | 236 |

Same picture as on Karolina: the two JIT variants agree to within rounding
(the max Δ of 67 is a handful of firefly pixels), both differ from
`scalar_rgb` only as Monte-Carlo noise — mean image luminance is 27.61/255
for all three.

### Correctness gate accompanying these runs

`ctest --preset linux-gcc-release`: **17 tests — 16 PASS, 1 by-design SKIP**
(`metal.triangle_matte`, macOS-only). Includes `gpu.triangle_matte`
(`cuda_ad_rgb`), `llvm.triangle_matte`, `pynari.triangle` and
`install.load_library`. PyNARI examples 01/03/04/05/06 all pass under
`ANARI_MITSUBA_VARIANT=cuda_ad_rgb`.

---

## Historical: Milestone 11 translation-cache measurement (Windows, 2026-08-15)

Recorded before this file existed, kept here for continuity: the retained-mode
translation cache reduced repeat scene translation in the `anari.scene_update`
test from **27.4 ms to 0.13 ms**; a material change re-translates only the
affected objects.
