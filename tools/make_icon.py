#!/usr/bin/env python3
"""Gera res/app.ico e os ícones da extensão: quadrado azul arredondado com uma seta branca para baixo.

    python3 tools/make_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

BLUE = (37, 99, 235, 255)
WHITE = (255, 255, 255, 255)


def draw(size: int) -> Image.Image:
    scale = 8  # desenha grande e reduz, para bordas suaves
    s = size * scale
    image = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    margin = s * 0.06
    d.rounded_rectangle([margin, margin, s - margin, s - margin], radius=s * 0.22, fill=BLUE)

    cx = s / 2
    shaft = s * 0.11
    d.rectangle([cx - shaft / 2, s * 0.20, cx + shaft / 2, s * 0.56], fill=WHITE)
    head = s * 0.21
    d.polygon([(cx - head, s * 0.48), (cx + head, s * 0.48), (cx, s * 0.70)], fill=WHITE)
    d.rounded_rectangle([s * 0.26, s * 0.75, s * 0.74, s * 0.83], radius=s * 0.04, fill=WHITE)
    return image.resize((size, size), Image.LANCZOS)


def main() -> None:
    sizes = [16, 20, 24, 32, 40, 48, 64, 256]
    out = Path(__file__).resolve().parent.parent / "res" / "app.ico"
    images = [draw(size) for size in sizes]
    images[-1].save(out, format="ICO", sizes=[(size, size) for size in sizes], append_images=images[:-1])
    print(out, out.stat().st_size, "bytes")

    icons = out.parent.parent / "extension" / "icons"
    icons.mkdir(parents=True, exist_ok=True)
    for size in (16, 32, 48, 128):
        draw(size).save(icons / f"icon-{size}.png")


if __name__ == "__main__":
    main()
