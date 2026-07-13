#!/usr/bin/env python3
"""Rasterize the launcher's feature-icon SVGs into PNGs it can load.

  source : cerf/assets/icons_sources/<stem>.svg   (the legal icon sources)
  output : launcher/assets/icons/<stem>.png        (32x32 RGBA, tracked)

The launcher renders icons with tk.PhotoImage, which reads PNG but not SVG, and
its two shipped builds run on CPython 3.15 and 3.7-x86 (Vista) where a runtime
SVG rasterizer has no reliable wheels. So the SVG stays the single source of
truth and this tool renders the launcher's subset at build-authoring time -- the
same committed-generated-raster pattern as tools/make_icons.py (.ico) and
tools/make_badges.py (badge PNGs). Re-run when a feature SVG changes.

The stem set is FEATURE_SPECS in launcher/supported_devices.py, so it tracks
whatever the side panel and README reference. Renderer: resvg (resvg-py) + Pillow.

Setup:  python -m pip install resvg-py pillow
Usage:  python tools/make_launcher_icons.py
"""
import sys
from io import BytesIO
from pathlib import Path

SIZE = 32  # rendered 1:1 at the side-panel display size
REPO = Path(__file__).resolve().parent.parent
SRC_DIR = REPO / "cerf" / "assets" / "icons_sources"
OUT_DIR = REPO / "launcher" / "assets" / "icons"


def render_png(svg_path, size):
    import resvg_py
    from PIL import Image
    raw = resvg_py.svg_to_bytes(svg_path=str(svg_path), width=size, height=size)
    im = Image.open(BytesIO(bytes(raw))).convert("RGBA")
    if im.size != (size, size):
        im = im.resize((size, size), Image.LANCZOS)
    buf = BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()


def main():
    try:
        import resvg_py  # noqa: F401
        from PIL import Image  # noqa: F401
    except ImportError as e:
        sys.exit(f"missing dependency ({e.name}). Run: "
                 f"python -m pip install resvg-py pillow")

    sys.path.insert(0, str(REPO / "launcher"))
    from supported_devices import FEATURE_SPECS

    stems = sorted({stem for _key, stem, _label in FEATURE_SPECS})
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for stem in stems:
        svg = SRC_DIR / f"{stem}.svg"
        if not svg.exists():
            sys.exit(f"source not found: {svg}")
        blob = render_png(svg, SIZE)
        out = OUT_DIR / f"{stem}.png"
        if not (out.exists() and out.read_bytes() == blob):
            out.write_bytes(blob)
        print(f"{svg.name} -> {out.relative_to(REPO)}  ({SIZE}x{SIZE})")


if __name__ == "__main__":
    main()
