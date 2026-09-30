#!/usr/bin/env python3
"""NOVA Game Engine 로고 생성기.

같은 도형(4갈래 노바 스타 + 스파클)을 SVG 로 쓰고, 같은 좌표로 PIL 에서 래스터화해
PNG / ICO 를 만든다. (외부 SVG 렌더러 없이 재현 가능)

출력
  ProjectSetting/logo/nova-logo.svg               아이콘(정사각)
  ProjectSetting/logo/nova-logo-horizontal.svg    아이콘 + NOVA 워드마크
  ProjectSetting/logo/nova-logo-{32,64,128,256,512}.png
  ProjectSetting/icon.png, ProjectSetting/icon.ico, Source/Platform/icon.ico

실행: python Tools/make_logo.py   (Pillow, numpy 필요)
"""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGO_DIR = os.path.join(ROOT, "ProjectSetting", "logo")
os.makedirs(LOGO_DIR, exist_ok=True)

# ---- 디자인 상수 (512 기준 좌표) ----
BG_TOP = (0x1E, 0x2A, 0x55)
BG_BOTTOM = (0x08, 0x0C, 0x1A)
N_STOPS = [(0.0, (0x5C, 0xF0, 0xE6)), (0.5, (0x3D, 0x8B, 0xFF)), (1.0, (0x9B, 0x5C, 0xFF))]

import math

# "N" 글자 (굵은 기하학적 N) 와 우상단의 노바 버스트(8갈래 별)
N_POLY = [(120, 400), (120, 112), (176, 112), (336, 312), (336, 112), (392, 112), (392, 400), (336, 400), (176, 200), (176, 400)]
BURST_C = (392, 112)


def burst_points(cx, cy, r_long=50, r_diag=30, r_in=13):
    pts = []
    for i in range(16):
        ang = math.radians(i * 22.5 - 90)
        if i % 4 == 0:
            r = r_long
        elif i % 2 == 0:
            r = r_diag
        else:
            r = r_in
        pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    return pts


def pts_attr(points):
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in points)


N_ATTR = pts_attr(N_POLY)
BURST_ATTR = pts_attr(burst_points(*BURST_C))

SVG_ICON = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512" role="img" aria-label="NOVA Game Engine">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#1E2A55"/>
      <stop offset="1" stop-color="#080C1A"/>
    </linearGradient>
    <linearGradient id="n" gradientUnits="userSpaceOnUse" x1="120" y1="112" x2="392" y2="400">
      <stop offset="0" stop-color="#5CF0E6"/>
      <stop offset="0.5" stop-color="#3D8BFF"/>
      <stop offset="1" stop-color="#9B5CFF"/>
    </linearGradient>
    <radialGradient id="burstGlow" gradientUnits="userSpaceOnUse" cx="392" cy="112" r="110">
      <stop offset="0" stop-color="#FFE9A8" stop-opacity="0.75"/>
      <stop offset="1" stop-color="#FFE9A8" stop-opacity="0"/>
    </radialGradient>
  </defs>
  <rect width="512" height="512" rx="112" fill="url(#bg)"/>
  <rect x="1.5" y="1.5" width="509" height="509" rx="110.5" fill="none" stroke="#FFFFFF" stroke-opacity="0.08" stroke-width="3"/>
  <polygon points="{N_ATTR}" fill="#3D8BFF" fill-opacity="0.35" transform="translate(0 10)"/>
  <polygon points="{N_ATTR}" fill="url(#n)"/>
  <circle cx="392" cy="112" r="110" fill="url(#burstGlow)"/>
  <polygon points="{BURST_ATTR}" fill="#FFFFFF"/>
  <circle cx="132" cy="140" r="5" fill="#FFFFFF" fill-opacity="0.5"/>
  <circle cx="420" cy="430" r="4" fill="#FFFFFF" fill-opacity="0.35"/>
</svg>
"""

SVG_HORIZONTAL = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1180 512" width="1180" height="512" role="img" aria-label="NOVA Game Engine">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#222E5E"/>
      <stop offset="1" stop-color="#090D1C"/>
    </linearGradient>
    <linearGradient id="n" gradientUnits="userSpaceOnUse" x1="120" y1="112" x2="392" y2="400">
      <stop offset="0" stop-color="#5CF0E6"/>
      <stop offset="0.5" stop-color="#3D8BFF"/>
      <stop offset="1" stop-color="#9B5CFF"/>
    </linearGradient>
    <linearGradient id="word" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#5CF0E6"/>
      <stop offset="1" stop-color="#9B5CFF"/>
    </linearGradient>
    <radialGradient id="burstGlow" gradientUnits="userSpaceOnUse" cx="392" cy="112" r="110">
      <stop offset="0" stop-color="#FFE9A8" stop-opacity="0.75"/>
      <stop offset="1" stop-color="#FFE9A8" stop-opacity="0"/>
    </radialGradient>
  </defs>
  <g>
    <rect width="512" height="512" rx="112" fill="url(#bg)"/>
    <polygon points="{N_ATTR}" fill="url(#n)"/>
    <circle cx="392" cy="112" r="110" fill="url(#burstGlow)"/>
    <polygon points="{BURST_ATTR}" fill="#FFFFFF"/>
  </g>
  <text x="580" y="290" font-family=\"'Segoe UI','Inter','Helvetica Neue',Arial,sans-serif\" font-size="210" font-weight="800" letter-spacing="14" fill="url(#word)">NOVA</text>
  <text x="586" y="378" font-family=\"'Segoe UI','Inter','Helvetica Neue',Arial,sans-serif\" font-size="64" font-weight="500" letter-spacing="22" fill="#9FB0D8">GAME ENGINE</text>
</svg>
"""


# ---- 래스터화 (PIL) ----
def parse_path(d):
    """M / Q / Z 만 쓰는 경로를 이차 베지어 점열로 펼친다."""
    tokens = d.replace(",", " ").replace("M", " M ").replace("Q", " Q ").replace("Z", " Z ").split()
    pts, cur, i = [], None, 0
    while i < len(tokens):
        t = tokens[i]
        if t == "M":
            cur = (float(tokens[i + 1]), float(tokens[i + 2]))
            pts.append(cur)
            i += 3
        elif t == "Q":
            c = (float(tokens[i + 1]), float(tokens[i + 2]))
            e = (float(tokens[i + 3]), float(tokens[i + 4]))
            for s in range(1, 41):
                u = s / 40.0
                x = (1 - u) ** 2 * cur[0] + 2 * (1 - u) * u * c[0] + u * u * e[0]
                y = (1 - u) ** 2 * cur[1] + 2 * (1 - u) * u * c[1] + u * u * e[1]
                pts.append((x, y))
            cur = e
            i += 5
        else:
            i += 1
    return pts


def lerp_stops(t, stops):
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,), dtype=np.float32)
    for k in range(len(stops) - 1):
        t0, c0 = stops[k]
        t1, c1 = stops[k + 1]
        m = (t >= t0) & (t <= t1)
        u = ((t - t0) / (t1 - t0))[m]
        for ch in range(3):
            out[..., ch][m] = c0[ch] + (c1[ch] - c0[ch]) * u
    return out


def render(size=512, ss=4):
    S = size * ss
    k = S / 512.0
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
    u, v = xx / S, yy / S

    # 배경 그라디언트 (대각선)
    t = (u + v) / 2.0
    bg = np.zeros((S, S, 3), dtype=np.float32)
    for ch in range(3):
        bg[..., ch] = BG_TOP[ch] + (BG_BOTTOM[ch] - BG_TOP[ch]) * t
    img = np.dstack([bg, np.ones((S, S), dtype=np.float32) * 255])

    # 라운드 사각형 마스크
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=112 * k, fill=255)
    round_mask = np.asarray(mask, dtype=np.float32) / 255.0

    def over(base, color, alpha):
        a = alpha[..., None]
        return base * (1 - a) + np.asarray(color, dtype=np.float32) * a

    rgb = img[..., :3]

    # N 뒤쪽의 부드러운 파란 그림자
    shadow = Image.new("L", (S, S), 0)
    ImageDraw.Draw(shadow).polygon([(x * k, (y + 10) * k) for x, y in N_POLY], fill=255)
    shadow = shadow.filter(ImageFilter.GaussianBlur(14 * k))
    rgb = over(rgb, (0x3D, 0x8B, 0xFF), np.asarray(shadow, dtype=np.float32) / 255.0 * 0.45)

    # N (대각선 그라디언트: (120,112) → (392,400))
    n_mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(n_mask).polygon([(x * k, y * k) for x, y in N_POLY], fill=255)
    n_a = np.asarray(n_mask, dtype=np.float32) / 255.0
    dx, dy = 392 - 120, 400 - 112
    tg = ((xx / k - 120) * dx + (yy / k - 112) * dy) / (dx * dx + dy * dy)
    n_rgb = lerp_stops(tg, N_STOPS)
    rgb = rgb * (1 - n_a[..., None]) + n_rgb * n_a[..., None]

    # 버스트 글로우 + 8갈래 별
    bd = np.sqrt((xx - BURST_C[0] * k) ** 2 + (yy - BURST_C[1] * k) ** 2) / (110 * k)
    rgb = over(rgb, (0xFF, 0xE9, 0xA8), np.clip(1 - bd, 0, 1) ** 1.3 * 0.75)
    burst = Image.new("L", (S, S), 0)
    ImageDraw.Draw(burst).polygon([(x * k, y * k) for x, y in burst_points(*BURST_C)], fill=255)
    rgb = over(rgb, (255, 255, 255), np.asarray(burst, dtype=np.float32) / 255.0)

    # 작은 점
    dots = Image.new("L", (S, S), 0)
    dd = ImageDraw.Draw(dots)
    dd.ellipse([(132 - 5) * k, (140 - 5) * k, (132 + 5) * k, (140 + 5) * k], fill=int(255 * 0.5))
    dd.ellipse([(420 - 4) * k, (430 - 4) * k, (420 + 4) * k, (430 + 4) * k], fill=int(255 * 0.35))
    rgb = over(rgb, (255, 255, 255), np.asarray(dots, dtype=np.float32) / 255.0)

    out = np.dstack([np.clip(rgb, 0, 255), round_mask * 255]).astype(np.uint8)
    return Image.fromarray(out, "RGBA").resize((size, size), Image.LANCZOS)


def main():
    with open(os.path.join(LOGO_DIR, "nova-logo.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(SVG_ICON)
    with open(os.path.join(LOGO_DIR, "nova-logo-horizontal.svg"), "w", encoding="utf-8", newline="\n") as f:
        f.write(SVG_HORIZONTAL)

    base = render(512)
    for s in (32, 64, 128, 256, 512):
        (base if s == 512 else render(s)).save(os.path.join(LOGO_DIR, f"nova-logo-{s}.png"))

    base.save(os.path.join(ROOT, "ProjectSetting", "icon.png"))
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    # 작은 크기는 직접 렌더링한 이미지를 사용해 선명도를 유지한다.
    frames = [render(s[0]) for s in sizes]
    for target in (os.path.join(ROOT, "ProjectSetting", "icon.ico"), os.path.join(ROOT, "Source", "Platform", "icon.ico")):
        frames[-1].save(target, format="ICO", sizes=sizes, append_images=frames[:-1])
    print("logo generated")


if __name__ == "__main__":
    main()
