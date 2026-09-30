#!/usr/bin/env python3
"""아주 작은 SVG 래스터라이저 (에디터 아이콘용).

지원: path(M L H V C Q Z, 상대좌표 포함), circle, ellipse, rect(rx), line, polyline, polygon,
      g/transform(translate, scale, rotate), fill / stroke / stroke-width / opacity,
      fill-opacity / stroke-opacity, stroke-linecap(round), fill-rule(evenodd)
외부 라이브러리 없이 Pillow + numpy 만 사용한다. 아이콘은 단순한 도형으로만 구성한다.

사용: python Tools/svg_raster.py <svg 폴더> <출력 폴더> [배율=4]
      (viewBox 크기 × 배율 픽셀의 PNG 를 만든다)
"""
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageDraw

SS = 4  # 슈퍼샘플링


def parse_color(value, default=None):
    if value is None:
        return default
    value = value.strip()
    if value in ("none", "transparent"):
        return None
    if value == "currentColor":
        return (255, 255, 255)
    m = re.fullmatch(r"#([0-9a-fA-F]{3})", value)
    if m:
        h = m.group(1)
        return tuple(int(c * 2, 16) for c in h)
    m = re.fullmatch(r"#([0-9a-fA-F]{6})", value)
    if m:
        h = m.group(1)
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    return default


def mat_mul(a, b):
    return (
        a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],
        a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
        a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5],
    )


def parse_transform(text):
    m = (1, 0, 0, 1, 0, 0)
    if not text:
        return m
    for name, args in re.findall(r"(\w+)\(([^)]*)\)", text):
        v = [float(x) for x in re.split(r"[ ,]+", args.strip()) if x]
        if name == "translate":
            t = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif name == "scale":
            t = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif name == "rotate":
            a = math.radians(v[0])
            c, s = math.cos(a), math.sin(a)
            t = (c, s, -s, c, 0, 0)
            if len(v) == 3:
                t = mat_mul(mat_mul((1, 0, 0, 1, v[1], v[2]), t), (1, 0, 0, 1, -v[1], -v[2]))
        else:
            continue
        m = mat_mul(m, t)
    return m


def apply(m, p):
    return (m[0] * p[0] + m[2] * p[1] + m[4], m[1] * p[0] + m[3] * p[1] + m[5])


def flatten_path(d):
    """경로를 서브패스(점 리스트, 닫힘 여부)들로 펼친다."""
    tokens = re.findall(r"[MmLlHhVvCcQqZz]|-?\d*\.?\d+(?:[eE][-+]?\d+)?", d)
    i = 0
    cmd = None
    cur = (0.0, 0.0)
    start = (0.0, 0.0)
    subs = []
    pts = []
    closed = False

    def num():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    def flush():
        nonlocal pts, closed
        if len(pts) > 1:
            subs.append((pts, closed))
        pts = []
        closed = False

    while i < len(tokens):
        t = tokens[i]
        if re.fullmatch(r"[A-Za-z]", t):
            cmd = t
            i += 1
            if cmd in "Zz":
                closed = True
                cur = start
                flush()
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            flush()
            x, y = num(), num()
            cur = (cur[0] + x, cur[1] + y) if rel else (x, y)
            start = cur
            pts = [cur]
            cmd = "l" if rel else "L"
        elif c == "L":
            x, y = num(), num()
            cur = (cur[0] + x, cur[1] + y) if rel else (x, y)
            pts.append(cur)
        elif c == "H":
            x = num()
            cur = (cur[0] + x if rel else x, cur[1])
            pts.append(cur)
        elif c == "V":
            y = num()
            cur = (cur[0], cur[1] + y if rel else y)
            pts.append(cur)
        elif c == "Q":
            cx, cy, x, y = num(), num(), num(), num()
            if rel:
                cx, cy, x, y = cur[0] + cx, cur[1] + cy, cur[0] + x, cur[1] + y
            for s in range(1, 25):
                u = s / 24.0
                pts.append(((1 - u) ** 2 * cur[0] + 2 * (1 - u) * u * cx + u * u * x,
                            (1 - u) ** 2 * cur[1] + 2 * (1 - u) * u * cy + u * u * y))
            cur = (x, y)
        elif c == "C":
            x1, y1, x2, y2, x, y = (num() for _ in range(6))
            if rel:
                x1, y1, x2, y2, x, y = cur[0] + x1, cur[1] + y1, cur[0] + x2, cur[1] + y2, cur[0] + x, cur[1] + y
            for s in range(1, 33):
                u = s / 32.0
                a, b, cc, dd = (1 - u) ** 3, 3 * (1 - u) ** 2 * u, 3 * (1 - u) * u * u, u ** 3
                pts.append((a * cur[0] + b * x1 + cc * x2 + dd * x, a * cur[1] + b * y1 + cc * y2 + dd * y))
            cur = (x, y)
        else:
            i += 1
    flush()
    return subs


def shape_subpaths(el):
    tag = el.tag.split("}")[-1]
    g = el.attrib.get
    if tag == "path":
        return flatten_path(g("d", ""))
    if tag == "circle":
        cx, cy, r = float(g("cx", 0)), float(g("cy", 0)), float(g("r", 0))
        return [([(cx + r * math.cos(a * math.pi / 32), cy + r * math.sin(a * math.pi / 32)) for a in range(64)], True)]
    if tag == "ellipse":
        cx, cy, rx, ry = float(g("cx", 0)), float(g("cy", 0)), float(g("rx", 0)), float(g("ry", 0))
        return [([(cx + rx * math.cos(a * math.pi / 32), cy + ry * math.sin(a * math.pi / 32)) for a in range(64)], True)]
    if tag == "rect":
        x, y, w, h = float(g("x", 0)), float(g("y", 0)), float(g("width", 0)), float(g("height", 0))
        rx = float(g("rx", g("ry", 0)))
        if rx <= 0:
            return [([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], True)]
        pts = []
        for cx, cy, a0 in ((x + w - rx, y + rx, -90), (x + w - rx, y + h - rx, 0), (x + rx, y + h - rx, 90), (x + rx, y + rx, 180)):
            for s in range(0, 10):
                a = math.radians(a0 + s * 10)
                pts.append((cx + rx * math.cos(a), cy + rx * math.sin(a)))
        return [(pts, True)]
    if tag == "line":
        return [([(float(g("x1", 0)), float(g("y1", 0))), (float(g("x2", 0)), float(g("y2", 0)))], False)]
    if tag in ("polygon", "polyline"):
        nums = [float(v) for v in re.split(r"[ ,]+", g("points", "").strip()) if v]
        pts = list(zip(nums[0::2], nums[1::2]))
        return [(pts, tag == "polygon")]
    return None


def render_svg(path, scale=4):
    tree = ET.parse(path)
    root = tree.getroot()
    vb = [float(v) for v in re.split(r"[ ,]+", root.attrib.get("viewBox", "0 0 16 16").strip())]
    W = int(round(vb[2] * scale))
    H = int(round(vb[3] * scale))
    k = scale * SS
    canvas = np.zeros((H * SS, W * SS, 4), dtype=np.float32)  # 프리멀티플라이드 아님(straight)

    def composite(mask, color, opacity):
        a = (mask.astype(np.float32) / 255.0) * opacity
        for ch in range(3):
            canvas[..., ch] = canvas[..., ch] * (1 - a) + color[ch] * a
        canvas[..., 3] = canvas[..., 3] + a * (1 - canvas[..., 3])

    def walk(el, m, inherited):
        style = dict(inherited)
        for key in ("fill", "stroke", "stroke-width", "opacity", "fill-opacity", "stroke-opacity", "stroke-linecap", "fill-rule"):
            if key in el.attrib:
                style[key] = el.attrib[key]
        m2 = mat_mul(m, parse_transform(el.attrib.get("transform")))
        tag = el.tag.split("}")[-1]
        if tag in ("g", "svg"):
            for child in el:
                walk(child, m2, style)
            return
        subs = shape_subpaths(el)
        if subs is None:
            return
        opacity = float(style.get("opacity", 1))
        fill = parse_color(style.get("fill", "#000000"))
        stroke = parse_color(style.get("stroke"), None)
        sw = float(style.get("stroke-width", 1))
        world = [([apply(m2, p) for p in pts], closed) for pts, closed in subs]
        px = lambda p: ((p[0] - vb[0]) * k, (p[1] - vb[1]) * k)

        if fill is not None:
            masks = []
            for pts, closed in world:
                if len(pts) < 3:
                    continue
                im = Image.new("L", (W * SS, H * SS), 0)
                ImageDraw.Draw(im).polygon([px(p) for p in pts], fill=255)
                masks.append(np.asarray(im, dtype=np.uint8))
            if masks:
                if style.get("fill-rule") == "evenodd":
                    combined = masks[0]
                    for extra in masks[1:]:
                        combined = np.bitwise_xor(combined, extra)
                else:
                    combined = masks[0]
                    for extra in masks[1:]:
                        combined = np.maximum(combined, extra)
                composite(combined, fill, opacity * float(style.get("fill-opacity", 1)))
        if stroke is not None and sw > 0:
            im = Image.new("L", (W * SS, H * SS), 0)
            d = ImageDraw.Draw(im)
            width = max(1, int(round(sw * k)))
            for pts, closed in world:
                ps = [px(p) for p in pts]
                if closed:
                    ps = ps + [ps[0]]
                d.line(ps, fill=255, width=width, joint="curve")
                if style.get("stroke-linecap") == "round" or True:
                    r = width / 2.0
                    ends = ps if not closed else []
                    for e in (ends[:1] + ends[-1:]):
                        d.ellipse([e[0] - r, e[1] - r, e[0] + r, e[1] + r], fill=255)
            composite(np.asarray(im, dtype=np.uint8), stroke, opacity * float(style.get("stroke-opacity", 1)))

    base = {"fill": "#000000"}
    walk(root, (1, 0, 0, 1, 0, 0), base)
    out = np.clip(canvas, 0, 255)
    out[..., 3] = np.clip(canvas[..., 3] * 255.0, 0, 255)
    img = Image.fromarray(out.astype(np.uint8), "RGBA")
    return img.resize((W, H), Image.LANCZOS)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        if name.lower().endswith(".svg"):
            img = render_svg(os.path.join(src, name), scale)
            img.save(os.path.join(dst, os.path.splitext(name)[0] + ".png"))
            print("rendered", name, img.size)


if __name__ == "__main__":
    main()
