#!/usr/bin/env python3
"""Generate default equirectangular sky-sphere panorama assets.

The generated ``sphere.png`` files are 2:1 panoramas intended for a future
single-mode sky-sphere renderer.  They deliberately avoid cubemap face splits,
so the renderer can remove sky seams by construction instead of relying on
six-face edge matching.
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image


WIDTH = 2048
HEIGHT = 1024
ROOTS = (Path("media/skyboxes"), Path("apps/slayer3d_editor/data/skyboxes"))
PRESETS = ("afternoon", "sunrise", "sunset", "midnight", "matrix", "outer_space", "under_the_sea", "sky_17")


def clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
    return max(low, min(high, value))


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = clamp((value - edge0) / (edge1 - edge0))
    return t * t * (3.0 - 2.0 * t)


def mix(a: tuple[float, float, float], b: tuple[float, float, float], t: float) -> tuple[float, float, float]:
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def fract(value: float) -> float:
    return value - math.floor(value)


def hash2(x: int, y: int) -> float:
    return fract(math.sin(x * 127.1 + y * 311.7) * 43758.5453123)


def value_noise(x: float, y: float) -> float:
    ix = math.floor(x)
    iy = math.floor(y)
    fx = x - ix
    fy = y - iy
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)

    a = hash2(ix, iy)
    b = hash2(ix + 1, iy)
    c = hash2(ix, iy + 1)
    d = hash2(ix + 1, iy + 1)
    ab = a + (b - a) * ux
    cd = c + (d - c) * ux
    return ab + (cd - ab) * uy


def fbm(x: float, y: float, octaves: int = 6) -> float:
    total = 0.0
    amplitude = 0.5
    frequency = 1.0
    for _ in range(octaves):
        total += value_noise(x * frequency, y * frequency) * amplitude
        frequency *= 2.03
        amplitude *= 0.5
    return total


def normalize(v: tuple[float, float, float]) -> tuple[float, float, float]:
    length = max(math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), 0.000001)
    return (v[0] / length, v[1] / length, v[2] / length)


def direction_from_equirect(x: int, y: int) -> tuple[float, float, float]:
    u = (x + 0.5) / WIDTH
    v = (y + 0.5) / HEIGHT
    azimuth = (u - 0.5) * math.tau
    elevation = (0.5 - v) * math.pi
    ce = math.cos(elevation)
    return (math.sin(azimuth) * ce, math.sin(elevation), math.cos(azimuth) * ce)


def cloud_amount(direction: tuple[float, float, float], seed: float, density: float, high_fade: float = 0.25) -> float:
    x, y, z = direction
    elevation = clamp((y + 0.15) / 1.15)
    cloud_x = x * 2.1 + z * 0.44 + seed
    cloud_y = z * 1.55 - x * 0.38 + y * 0.45 - seed * 0.37
    broad = fbm(cloud_x, cloud_y)
    fine = fbm(cloud_x * 2.8 - seed * 0.7, cloud_y * 2.45 + seed * 1.3)
    cloud = smoothstep(0.42, 0.88, broad * 0.80 + fine * 0.35 + density)
    cloud *= smoothstep(-0.22, 0.28, y)
    cloud *= 1.0 - smoothstep(0.78, 1.0, y) * high_fade
    return clamp(cloud)


def natural_sky(
    direction: tuple[float, float, float],
    horizon: tuple[float, float, float],
    zenith: tuple[float, float, float],
    sun_dir: tuple[float, float, float],
    sun_color: tuple[float, float, float],
    seed: float,
    density: float,
) -> tuple[float, float, float]:
    x, y, z = direction
    elevation = clamp((y + 0.10) / 1.10)
    base = mix(horizon, zenith, smoothstep(0.02, 1.0, elevation))
    sun = clamp(x * sun_dir[0] + y * sun_dir[1] + z * sun_dir[2])
    base = mix(base, sun_color, smoothstep(0.50, 1.0, sun) * 0.34)
    cloud = cloud_amount(direction, seed, density)
    cloud_tint = mix((0.82, 0.88, 0.95), (1.0, 0.98, 0.92), smoothstep(0.0, 0.8, sun))
    color = mix(base, cloud_tint, cloud * 0.68)
    haze = smoothstep(-0.18, 0.06, -abs(y) + 0.10)
    return mix(color, horizon, haze * 0.12)


def color_for_preset(preset: str, direction: tuple[float, float, float]) -> tuple[int, int, int, int]:
    x, y, z = direction
    if preset == "afternoon":
        color = natural_sky(direction, (0.64, 0.82, 0.95), (0.03, 0.38, 0.88), normalize((-0.28, 0.30, 0.91)),
                            (1.0, 0.95, 0.76), 4.3, -0.02)
    elif preset == "sunrise":
        color = natural_sky(direction, (0.98, 0.60, 0.38), (0.18, 0.36, 0.75), normalize((-0.82, 0.05, 0.56)),
                            (1.0, 0.72, 0.36), 7.9, 0.05)
    elif preset == "sunset":
        color = natural_sky(direction, (0.94, 0.42, 0.26), (0.13, 0.18, 0.48), normalize((0.78, 0.03, 0.62)),
                            (1.0, 0.55, 0.28), 11.6, 0.03)
    elif preset == "midnight":
        elevation = clamp((y + 0.08) / 1.08)
        color = mix((0.04, 0.06, 0.16), (0.0, 0.01, 0.05), smoothstep(0.0, 1.0, elevation))
        moon = smoothstep(0.988, 1.0, x * -0.35 + y * 0.36 + z * 0.86)
        color = mix(color, (0.62, 0.72, 0.92), moon * 0.8)
        star = value_noise((math.atan2(x, z) + math.pi) * 180.0, (y + 1.0) * 220.0)
        if y > -0.05 and star > 0.992:
            color = mix(color, (0.95, 0.98, 1.0), 0.8)
    elif preset == "outer_space":
        nebula = fbm(x * 3.4 + 13.0, z * 3.1 + y * 1.5 - 4.0)
        color = mix((0.0, 0.0, 0.025), (0.11, 0.03, 0.20), smoothstep(0.52, 0.90, nebula))
        star = value_noise((math.atan2(x, z) + math.pi) * 260.0, (y + 1.0) * 260.0)
        if star > 0.989:
            color = mix(color, (0.90, 0.94, 1.0), 0.85)
    elif preset == "matrix":
        elevation = clamp((y + 0.15) / 1.15)
        color = mix((0.02, 0.09, 0.05), (0.0, 0.02, 0.01), smoothstep(0.0, 1.0, elevation))
        rain = smoothstep(0.84, 0.98, value_noise((math.atan2(x, z) + math.pi) * 80.0, y * 20.0))
        color = mix(color, (0.08, 0.75, 0.24), rain * 0.28)
    elif preset == "under_the_sea":
        elevation = clamp((y + 0.20) / 1.20)
        color = mix((0.02, 0.22, 0.32), (0.02, 0.52, 0.70), smoothstep(0.0, 1.0, elevation))
        caustic = fbm(x * 5.0 + z * 2.0, z * 4.0 - y * 1.6)
        color = mix(color, (0.40, 0.90, 0.95), smoothstep(0.70, 0.92, caustic) * 0.18)
    else:
        color = natural_sky(direction, (0.62, 0.74, 0.88), (0.18, 0.30, 0.62), normalize((0.22, 0.24, 0.95)),
                            (0.86, 0.84, 0.75), 17.0, 0.02)

    return (
        int(clamp(color[0]) * 255.0 + 0.5),
        int(clamp(color[1]) * 255.0 + 0.5),
        int(clamp(color[2]) * 255.0 + 0.5),
        255,
    )


def generate_preset(preset: str) -> Image.Image:
    image = Image.new("RGBA", (WIDTH, HEIGHT))
    pixels = image.load()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            pixels[x, y] = color_for_preset(preset, direction_from_equirect(x, y))
    return image


def main() -> None:
    for preset in PRESETS:
        image = generate_preset(preset)
        for root in ROOTS:
            directory = root / preset
            directory.mkdir(parents=True, exist_ok=True)
            image.save(directory / "sphere.png")


if __name__ == "__main__":
    main()
