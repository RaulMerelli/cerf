#!/usr/bin/env python3
"""Convert SVG icon sources into multi-size Windows .ico files.

  source : cerf/assets/icons_sources/*.svg   (authored 16x16, vector-scalable)
  output : cerf/assets/<name>.ico            (16/20/24/32/48/64/256, 32-bit alpha)

Each .ico carries every size as its own resvg-rendered frame (vector rendered
natively at each pixel size, not one bitmap downscaled), so small sizes stay
crisp and large sizes stay smooth. CERF loads them per-DPI via
LoadIconWithScaleDown (see HostIconCache).

Renderer: resvg (resvg-py) — faithful SVG including filters/gradients, no system
deps. Setup:  python -m pip install resvg-py pillow

Usage:
  python tools/make_icons.py                 # convert every source
  python tools/make_icons.py ga_autoresize   # convert one (stem or path)
  python tools/make_icons.py --sizes 16,24,32,48,256
"""
import argparse
import struct
import sys
from io import BytesIO
from pathlib import Path

DEFAULT_SIZES = [16, 20, 24, 32, 48, 64, 256]
REPO = Path(__file__).resolve().parent.parent
SRC_DIR = REPO / "cerf" / "assets" / "icons_sources"
OUT_DIR = REPO / "cerf" / "assets"


def render_png(svg_path, size):
    """resvg-render svg_path at size*size, return a clean RGBA PNG blob."""
    import resvg_py
    from PIL import Image
    raw = resvg_py.svg_to_bytes(svg_path=str(svg_path), width=size, height=size)
    im = Image.open(BytesIO(bytes(raw))).convert("RGBA")
    if im.size != (size, size):
        im = im.resize((size, size), Image.LANCZOS)
    buf = BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()


def build_ico(frames):
    """Pack [(size, png_bytes), ...] into a PNG-frame .ico (ICONDIR layout).

    PNG-compressed frames are read by Windows Vista+ at every size; bWidth/
    bHeight 0 means 256."""
    frames = sorted(frames, key=lambda f: f[0])
    out = bytearray(struct.pack("<HHH", 0, 1, len(frames)))  # reserved, type=icon, count
    offset = 6 + 16 * len(frames)
    for size, png in frames:
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
    for _, png in frames:
        out += png
    return bytes(out)


def convert(svg_path, sizes, out_dir):
    frames = [(s, render_png(svg_path, s)) for s in sizes]
    out_path = out_dir / (svg_path.stem + ".ico")
    out_path.write_bytes(build_ico(frames))
    return out_path


def resolve_sources(names, src_dir):
    if not names:
        return sorted(src_dir.glob("*.svg"))
    svgs = []
    for n in names:
        p = Path(n)
        if not p.exists():
            p = src_dir / (n if n.endswith(".svg") else n + ".svg")
        if not p.exists():
            sys.exit(f"source not found: {n}")
        svgs.append(p)
    return svgs


def main():
    ap = argparse.ArgumentParser(description="SVG -> multi-size Windows .ico")
    ap.add_argument("names", nargs="*",
                    help="source stems or paths (default: every .svg in icons_sources)")
    ap.add_argument("--sizes", default=",".join(map(str, DEFAULT_SIZES)),
                    help="comma-separated pixel sizes")
    ap.add_argument("--src", default=str(SRC_DIR))
    ap.add_argument("--out", default=str(OUT_DIR))
    args = ap.parse_args()

    try:
        import resvg_py  # noqa: F401
        from PIL import Image  # noqa: F401
    except ImportError as e:
        sys.exit(f"missing dependency ({e.name}). Run: "
                 f"python -m pip install resvg-py pillow")

    sizes = [int(x) for x in args.sizes.split(",") if x.strip()]
    out_dir = Path(args.out)
    svgs = resolve_sources(args.names, Path(args.src))
    if not svgs:
        sys.exit(f"no .svg sources in {args.src}")

    out_dir.mkdir(parents=True, exist_ok=True)
    for svg in svgs:
        out_path = convert(svg, sizes, out_dir)
        print(f"{svg.name} -> {out_path.relative_to(REPO)}  "
              f"({len(sizes)} sizes: {','.join(map(str, sizes))})")


if __name__ == "__main__":
    main()
