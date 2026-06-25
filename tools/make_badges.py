#!/usr/bin/env python3
"""Render small CPU-architecture badge PNGs (e.g. "ARM", "MIPS").

  output : launcher/assets/icons/badge_<key>.png

Each badge is a rounded-rectangle box with centred bold white text on a
brand-tinted background. Every badge is rendered to the SAME pixel box (sized
to fit the widest label) so they line up wherever they appear - the launcher
device table and the README "Supported boards" table both consume them.

Rendered at a supersampled scale then downsampled (LANCZOS) so the rounded
corners and text stay crisp at the small display size.

Font: a bold system sans-serif if one is found, else Pillow's bundled DejaVu
Sans (scalable) so the tool runs with no external font dependency.

Setup:  python -m pip install pillow
Usage:  python tools/make_badges.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parent.parent
OUT_DIR = REPO / "launcher" / "assets" / "icons"

# (filename key, label, background RGB). White text on each.
BADGES = [
    ("arm", "ARM", (14, 124, 157)),    # ARM brand teal
    ("mips", "MIPS", (192, 57, 43)),   # MIPS brand red
]

SS = 4              # supersampling factor
FONT_PX = 11        # display-pixel font size
PAD_X = 5           # display-pixel horizontal padding inside the box
PAD_Y = 3           # display-pixel vertical padding inside the box
RADIUS = 2          # display-pixel corner radius
GAP_X = 4           # transparent margin after the box so following text
                    # (table cell / Label / README) isn't flush against it
TEXT_RGB = (255, 255, 255)

# Bold sans-serif candidates, most-preferred first; falls back to DejaVu.
BOLD_FONT_CANDIDATES = [
    "arialbd.ttf", "segoeuib.ttf", "DejaVuSans-Bold.ttf",
    "Arial Bold.ttf", "LiberationSans-Bold.ttf",
]


def load_font(px):
    for name in BOLD_FONT_CANDIDATES:
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            continue
    return ImageFont.load_default(size=px)


def text_size(font, text):
    l, t, r, b = font.getbbox(text)
    return r - l, b - t


def render_badge(label, bg, box_w, box_h, font):
    """Render one badge at supersampled scale, return a (box_w + GAP_X, box_h)
    RGBA: the coloured box on the left, then a transparent right margin so text
    placed after the badge keeps a gap."""
    big = Image.new("RGBA", (box_w * SS, box_h * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(big)
    draw.rounded_rectangle(
        [0, 0, box_w * SS - 1, box_h * SS - 1],
        radius=RADIUS * SS, fill=bg + (255,))
    # Centre by the glyph bbox (anchor="mm" centres on the text's own box).
    draw.text((box_w * SS / 2, box_h * SS / 2), label, font=font,
              fill=TEXT_RGB + (255,), anchor="mm")
    badge = big.resize((box_w, box_h), Image.LANCZOS)
    out = Image.new("RGBA", (box_w + GAP_X, box_h), (0, 0, 0, 0))
    out.paste(badge, (0, 0))
    return out


def main():
    font_big = load_font(FONT_PX * SS)
    # One shared box, sized to the widest label so all badges match.
    widest = max(text_size(font_big, label)[0] / SS for _, label, _ in BADGES)
    box_w = round(widest) + 2 * PAD_X
    box_h = FONT_PX + 2 * PAD_Y

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for key, label, bg in BADGES:
        img = render_badge(label, bg, box_w, box_h, font_big)
        out = OUT_DIR / f"badge_{key}.png"
        img.save(out, format="PNG")
        print(f"{label:5s} -> {out.relative_to(REPO)}  ({img.width}x{img.height})")


if __name__ == "__main__":
    main()
