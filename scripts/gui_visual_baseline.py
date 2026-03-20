#!/usr/bin/env python3
"""Deterministic framebuffer visual-baseline hashing for fb-shell profiles.

This script intentionally hashes only static chrome regions so it remains
stable across small runtime text/timing shifts (uptime counter, timeline).
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from typing import Dict, Iterable, List, Tuple


@dataclass(frozen=True)
class Rect:
    x: int
    y: int
    w: int
    h: int


def _read_ppm_p6(path: str) -> Tuple[int, int, bytes]:
    with open(path, "rb") as f:
        data = f.read()

    if not data.startswith(b"P6"):
        raise ValueError(f"{path}: expected P6 PPM")

    idx = 2
    tokens: List[bytes] = []

    def skip_ws_and_comments(pos: int) -> int:
        while pos < len(data):
            b = data[pos]
            if b in b" \t\r\n":
                pos += 1
                continue
            if b == ord("#"):
                while pos < len(data) and data[pos] not in b"\r\n":
                    pos += 1
                continue
            break
        return pos

    while len(tokens) < 3:
        idx = skip_ws_and_comments(idx)
        if idx >= len(data):
            raise ValueError(f"{path}: truncated PPM header")
        start = idx
        while idx < len(data) and data[idx] not in b" \t\r\n#":
            idx += 1
        if start == idx:
            raise ValueError(f"{path}: malformed PPM header token")
        tokens.append(data[start:idx])

    idx = skip_ws_and_comments(idx)
    width = int(tokens[0])
    height = int(tokens[1])
    maxval = int(tokens[2])
    if width <= 0 or height <= 0:
        raise ValueError(f"{path}: invalid geometry {width}x{height}")
    if maxval != 255:
        raise ValueError(f"{path}: unsupported maxval={maxval} (expected 255)")

    pixel_bytes = width * height * 3
    if idx + pixel_bytes > len(data):
        raise ValueError(f"{path}: truncated pixel payload")
    pixels = data[idx : idx + pixel_bytes]
    return width, height, pixels


def _clip_rect(r: Rect, width: int, height: int) -> Rect | None:
    x0 = max(0, r.x)
    y0 = max(0, r.y)
    x1 = min(width, r.x + r.w)
    y1 = min(height, r.y + r.h)
    if x0 >= x1 or y0 >= y1:
        return None
    return Rect(x0, y0, x1 - x0, y1 - y0)


def _fnv1a32_u32(h: int, value: int) -> int:
    for shift in (0, 8, 16, 24):
        h ^= (value >> shift) & 0xFF
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def _fnv1a32_bytes(h: int, payload: bytes) -> int:
    for b in payload:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def _load_display_constants(display_c_path: str) -> Dict[str, int]:
    wanted = {
        "DISPLAY_FB_CHAR_W",
        "DISPLAY_FB_CHAR_H",
        "DISPLAY_FB_HEADER_ROWS",
        "DISPLAY_FB_FOOTER_ROWS",
        "DISPLAY_FB_PANEL_MARGIN_X",
        "DISPLAY_FB_PANEL_MARGIN_Y",
        "DISPLAY_FB_PANEL_BORDER",
        "DISPLAY_FB_LINE_GUTTER_WIDTH",
        "DISPLAY_FB_LINE_GUTTER_GAP",
    }
    found: Dict[str, int] = {}
    define_re = re.compile(r"^#define\s+([A-Z0-9_]+)\s+([0-9]+)U?\s*$")

    with open(display_c_path, "r", encoding="utf-8") as f:
        for line in f:
            m = define_re.match(line.strip())
            if not m:
                continue
            name = m.group(1)
            if name not in wanted:
                continue
            found[name] = int(m.group(2))

    missing = sorted(wanted - set(found.keys()))
    if missing:
        raise ValueError(
            f"{display_c_path}: missing required DISPLAY_FB constants: {', '.join(missing)}"
        )
    return found


def _regions_fb_shell_v4(width: int, height: int, consts: Dict[str, int]) -> List[Rect]:
    char_w = consts["DISPLAY_FB_CHAR_W"]
    char_h = consts["DISPLAY_FB_CHAR_H"]
    header_rows = consts["DISPLAY_FB_HEADER_ROWS"]
    footer_rows = consts["DISPLAY_FB_FOOTER_ROWS"]
    panel_margin_x = consts["DISPLAY_FB_PANEL_MARGIN_X"]
    panel_margin_y = consts["DISPLAY_FB_PANEL_MARGIN_Y"]
    panel_border = consts["DISPLAY_FB_PANEL_BORDER"]
    line_gutter_width = consts["DISPLAY_FB_LINE_GUTTER_WIDTH"]
    line_gutter_gap = consts["DISPLAY_FB_LINE_GUTTER_GAP"]
    line_gutter_total = line_gutter_width + line_gutter_gap

    text_cols = (width - (panel_margin_x * 2)) // char_w
    if text_cols <= 0:
        return []
    content_width = text_cols * char_w
    content_left = (width - content_width) // 2
    content_top = (header_rows * char_h) + panel_margin_y + panel_border
    content_bottom = height - (footer_rows * char_h) - panel_margin_y - panel_border
    text_rows = (content_bottom - content_top) // char_h
    if text_rows < 2:
        return []
    content_bottom = content_top + (text_rows * char_h)

    panel_left = content_left - panel_border - line_gutter_total
    panel_top = content_top - panel_border
    panel_width = content_width + (panel_border * 2) + line_gutter_total
    panel_height = (content_bottom - content_top) + (panel_border * 2)

    footer_top = height - (footer_rows * char_h)
    static_outer_h = max(0, footer_top - char_h)

    return [
        # Title area with static label/badge only (exclude right metrics text).
        Rect(0, 0, min(width, 20 * char_w), char_h),
        # Accent bar under title.
        Rect(0, char_h - 2, width, 2),
        # Panel border rails.
        Rect(panel_left, panel_top, panel_width, 1),
        Rect(panel_left, panel_top + panel_height - 1, panel_width, 1),
        Rect(panel_left, panel_top, 1, panel_height),
        Rect(panel_left + panel_width - 1, panel_top, 1, panel_height),
        # Static background margins outside panel and outside footer row.
        Rect(0, char_h, panel_left, static_outer_h),
        Rect(panel_left + panel_width, char_h, width - (panel_left + panel_width), static_outer_h),
    ]


def _iter_region_payloads(pixels: bytes, width: int, rects: Iterable[Rect]) -> Tuple[int, int]:
    # Return (hash, sampled_pixel_count).
    row_stride = width * 3
    h = 2166136261
    sampled_pixels = 0

    for r in rects:
        h = _fnv1a32_u32(h, r.x)
        h = _fnv1a32_u32(h, r.y)
        h = _fnv1a32_u32(h, r.w)
        h = _fnv1a32_u32(h, r.h)
        for y in range(r.y, r.y + r.h):
            row_start = (y * row_stride) + (r.x * 3)
            row_end = row_start + (r.w * 3)
            h = _fnv1a32_bytes(h, pixels[row_start:row_end])
            sampled_pixels += r.w
    return h, sampled_pixels


def compute_hash(ppm_path: str, profile: str, display_c_path: str) -> Tuple[int, int, int, int, int]:
    width, height, pixels = _read_ppm_p6(ppm_path)
    if profile not in ("fb-shell-v4", "fb-shell-v5"):
        raise ValueError(f"unsupported profile: {profile}")
    consts = _load_display_constants(display_c_path)
    rects = _regions_fb_shell_v4(width, height, consts)
    clipped: List[Rect] = []
    for r in rects:
        c = _clip_rect(r, width, height)
        if c is not None:
            clipped.append(c)
    if not clipped:
        raise ValueError(f"{ppm_path}: no valid ROI regions for {profile} at {width}x{height}")
    h, sampled_pixels = _iter_region_payloads(pixels, width, clipped)
    return h, width, height, sampled_pixels, len(clipped)


def _parse_hash(text: str) -> int:
    s = text.strip().lower()
    if s.startswith("0x"):
        return int(s, 16)
    return int(s, 10)


def cmd_hash(args: argparse.Namespace) -> int:
    h, width, height, sampled_pixels, rect_count = compute_hash(
        args.ppm, args.profile, args.display_c
    )
    print(
        "GUI_VISUAL_BASELINE "
        f"profile={args.profile} "
        f"roi_hash=0x{h:08X} "
        f"width={width} "
        f"height={height} "
        f"roi_pixels={sampled_pixels} "
        f"roi_rects={rect_count}"
    )
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    expected = _parse_hash(args.expect_hash)
    h, width, height, sampled_pixels, rect_count = compute_hash(
        args.ppm, args.profile, args.display_c
    )
    if h != expected:
        print(
            "[gui-visual-baseline] mismatch "
            f"profile={args.profile} "
            f"expected=0x{expected:08X} "
            f"actual=0x{h:08X} "
            f"width={width} height={height} "
            f"roi_pixels={sampled_pixels} roi_rects={rect_count}",
            file=sys.stderr,
        )
        return 1
    print(
        "[gui-visual-baseline] PASS "
        f"profile={args.profile} hash=0x{h:08X} "
        f"width={width} height={height} "
        f"roi_pixels={sampled_pixels} roi_rects={rect_count}"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Framebuffer visual baseline hash helper")
    sub = p.add_subparsers(dest="cmd", required=True)

    p_hash = sub.add_parser("hash", help="compute ROI hash for a framebuffer PPM")
    p_hash.add_argument("--ppm", required=True, help="Input PPM path (P6)")
    p_hash.add_argument("--profile", default="fb-shell-v5", help="GUI profile name")
    p_hash.add_argument(
        "--display-c",
        default="kernel/display.c",
        help="Path to kernel display source for profile constants",
    )
    p_hash.set_defaults(func=cmd_hash)

    p_verify = sub.add_parser("verify", help="verify ROI hash for a framebuffer PPM")
    p_verify.add_argument("--ppm", required=True, help="Input PPM path (P6)")
    p_verify.add_argument("--profile", default="fb-shell-v5", help="GUI profile name")
    p_verify.add_argument("--expect-hash", required=True, help="Expected hash (hex or decimal)")
    p_verify.add_argument(
        "--display-c",
        default="kernel/display.c",
        help="Path to kernel display source for profile constants",
    )
    p_verify.set_defaults(func=cmd_verify)

    return p


def main(argv: List[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except Exception as exc:  # pragma: no cover - shell utility path
        print(f"[gui-visual-baseline] error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
