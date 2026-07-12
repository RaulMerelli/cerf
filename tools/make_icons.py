#!/usr/bin/env python3
"""Convert SVG icon sources into multi-size Windows .ico files.

  source : cerf/assets/icons_sources/*.svg   (authored 16x16, vector-scalable)
  output : cerf/assets/<name>.ico            (16/20/24/32/48/64/256, 32-bit alpha)

Each .ico carries every size as its own resvg-rendered frame (vector rendered
natively at each pixel size, not one bitmap downscaled), so small sizes stay
crisp and large sizes stay smooth. CERF loads them per-DPI via
LoadIconWithScaleDown (see HostIconCache).

Renderer: resvg (resvg-py) - faithful SVG including filters/gradients, no system
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


def render_dib(svg_path, size):
    """resvg-render svg_path at size*size as an ICO BMP frame (BITMAPINFOHEADER
    + bottom-up 32bpp BGRA XOR bitmap + AND mask).

    PNG-compressed frames are a Vista+ icon format: Windows XP's icon loader
    parses a frame only as a DIB, so LoadImage returns NULL for a PNG-only .ico
    and every CERF icon disappears. Alpha comes from the 32bpp XOR bitmap, so the
    AND mask is all-zero (fully opaque) - it exists only because the format
    requires it."""
    import resvg_py
    from PIL import Image
    raw = resvg_py.svg_to_bytes(svg_path=str(svg_path), width=size, height=size)
    im = Image.open(BytesIO(bytes(raw))).convert("RGBA")
    if im.size != (size, size):
        im = im.resize((size, size), Image.LANCZOS)

    r, g, b, a = im.split()
    bgra = Image.merge("RGBA", (b, g, r, a))
    xor = bgra.transpose(Image.FLIP_TOP_BOTTOM).tobytes()

    and_stride = ((size + 31) // 32) * 4
    and_mask = b"\x00" * (and_stride * size)

    header = struct.pack("<IiiHHIIiiII",
                         40,                        # biSize
                         size, size * 2,            # biWidth, biHeight (XOR+AND)
                         1, 32,                     # biPlanes, biBitCount
                         0,                         # biCompression = BI_RGB
                         len(xor) + len(and_mask),  # biSizeImage
                         0, 0, 0, 0)
    return header + xor + and_mask


def build_ico(frames):
    """Pack [(size, blob), ...] into an .ico (ICONDIR layout). bWidth/bHeight 0
    means 256."""
    frames = sorted(frames, key=lambda f: f[0])
    out = bytearray(struct.pack("<HHH", 0, 1, len(frames)))  # reserved, type=icon, count
    offset = 6 + 16 * len(frames)
    for size, blob in frames:
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    for _, blob in frames:
        out += blob
    return bytes(out)


def convert(svg_path, sizes, out_dir):
    # 256 stays PNG: a 256x256 DIB frame is a quarter-megabyte per icon, and the
    # size is never selected for the status-bar / menu requests XP makes.
    frames = [(s, render_png(svg_path, s) if s >= 256 else render_dib(svg_path, s))
              for s in sizes]
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
