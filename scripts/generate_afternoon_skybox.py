#!/usr/bin/env python3
"""Generate the default afternoon skybox with seam-safe cubemap faces.

The hardware renderer draws skyboxes as six independent faces. To keep linear
filtering from revealing cube boundaries, border texels are sampled exactly on
the shared cube edges so neighboring faces agree byte-for-byte.
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image


ROOTS = [
    Path("media/skyboxes/afternoon"),
    Path("apps/slayer3d_editor/data/skyboxes/afternoon"),
]
FACE_NAMES = ("px", "nx", "py", "ny", "pz", "nz")
SIZE = 512


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
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy


def fbm(x: float, y: float) -> float:
    total = 0.0
    amplitude = 0.5
    frequency = 1.0
    for _ in range(6):
        total += value_noise(x * frequency, y * frequency) * amplitude
        frequency *= 2.04
        amplitude *= 0.5
    return total


def normalize(v: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return (v[0] / length, v[1] / length, v[2] / length)


def face_direction(face: str, u: float, v: float) -> tuple[float, float, float]:
    if face == "px":
        return normalize((2.0 * u - 1.0, 1.0 - 2.0 * v, 1.0))
    if face == "nx":
        return normalize((1.0 - 2.0 * u, 1.0 - 2.0 * v, -1.0))
    if face == "nz":
        return normalize((1.0, 1.0 - 2.0 * v, 1.0 - 2.0 * u))
    if face == "pz":
        return normalize((-1.0, 1.0 - 2.0 * v, 2.0 * u - 1.0))
    if face == "py":
        return normalize((1.0 - 2.0 * v, 1.0, 2.0 * u - 1.0))
    if face == "ny":
        return normalize((1.0 - 2.0 * v, -1.0, 1.0 - 2.0 * u))
    raise ValueError(face)


def sky_color(direction: tuple[float, float, float]) -> tuple[int, int, int, int]:
    x, y, z = direction
    elevation = clamp((y + 0.10) / 1.10)
    horizon = (0.68, 0.84, 0.95)
    zenith = (0.08, 0.45, 0.92)
    base = mix(horizon, zenith, smoothstep(0.02, 1.0, elevation))

    sun_dir = normalize((-0.28, 0.30, 0.91))
    sun_dot = clamp(x * sun_dir[0] + y * sun_dir[1] + z * sun_dir[2])
    sun_glow = smoothstep(0.58, 1.0, sun_dot)
    base = mix(base, (1.0, 0.94, 0.76), sun_glow * 0.26)

    # Directional coordinates keep the same cloud field continuous across cube
    # faces. The two scales create broad soft banks and finer wisps.
    cloud_x = x * 2.2 + z * 0.42
    cloud_y = z * 1.65 - x * 0.36 + y * 0.55
    broad = fbm(cloud_x + 4.3, cloud_y - 1.7)
    fine = fbm(cloud_x * 2.7 - 8.1, cloud_y * 2.5 + 3.4)
    cloud = smoothstep(0.46, 0.86, broad * 0.78 + fine * 0.34 + (0.30 - elevation) * 0.18)
    cloud *= smoothstep(-0.18, 0.28, y) * (1.0 - smoothstep(0.78, 1.0, y) * 0.25)

    cloud_tint = mix((0.86, 0.92, 0.98), (1.0, 1.0, 1.0), smoothstep(0.2, 0.8, elevation))
    color = mix(base, cloud_tint, cloud * 0.72)

    haze = smoothstep(-0.20, 0.06, -abs(y) + 0.10)
    color = mix(color, (0.74, 0.86, 0.94), haze * 0.18)

    return (
        int(clamp(color[0]) * 255.0 + 0.5),
        int(clamp(color[1]) * 255.0 + 0.5),
        int(clamp(color[2]) * 255.0 + 0.5),
        255,
    )


def generate_face(face: str) -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE))
    pixels = image.load()
    for y in range(SIZE):
        v = y / (SIZE - 1)
        for x in range(SIZE):
            u = x / (SIZE - 1)
            pixels[x, y] = sky_color(face_direction(face, u, v))
    return image


def generate_layer(seed_offset: float, opacity: int) -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE))
    pixels = image.load()
    for y in range(SIZE):
        for x in range(SIZE):
            # Tileable cloud-noise layer for the scrolling Quake-style overlay.
            u = x / SIZE
            v = y / SIZE
            nx = math.cos(u * math.tau)
            ny = math.sin(u * math.tau)
            nz = math.cos(v * math.tau)
            nw = math.sin(v * math.tau)
            cloud = fbm(nx * 1.35 + nz * 0.85 + seed_offset, ny * 1.35 + nw * 0.85 - seed_offset)
            cloud = smoothstep(0.42, 0.90, cloud)
            a = int(clamp(cloud) * opacity + 0.5)
            pixels[x, y] = (235, 246, 255, a)
    return image


def save_layout(faces: dict[str, Image.Image], path: Path) -> None:
    layout = Image.new("RGBA", (SIZE * 4, SIZE * 3), (0, 0, 0, 255))
    positions = {
        "py": (SIZE, 0),
        "pz": (0, SIZE),
        "px": (SIZE, SIZE),
        "nz": (SIZE * 2, SIZE),
        "nx": (SIZE * 3, SIZE),
        "ny": (SIZE, SIZE * 2),
    }
    for face, xy in positions.items():
        layout.paste(faces[face], xy)
    layout.save(path)


def main() -> None:
    faces = {face: generate_face(face) for face in FACE_NAMES}
    layer_outer = generate_layer(1.7, 54)
    layer_inner = generate_layer(8.9, 36)

    for root in ROOTS:
        root.mkdir(parents=True, exist_ok=True)
        for face, image in faces.items():
            image.save(root / f"{face}.png")
        layer_outer.save(root / "layer_outer.png")
        layer_inner.save(root / "layer_inner.png")
        if root.parts[:2] == ("media", "skyboxes"):
            save_layout(faces, root / "cubemap_layout.png")


if __name__ == "__main__":
    main()
