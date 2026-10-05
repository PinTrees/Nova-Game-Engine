# 재질 테셀레이션 검사용 텍스처 · 재질 (docs/TESSELLATION.md)
#   python Tools/tests/make_tess_textures.py <프로젝트>/Assets/TessTest
# 돌 벽돌 벽 (StoneWall) · 둥근 자갈 바닥 (Cobble): 높이 맵 (회색, 0..1) + 색 + .mat (Displacement Mode = Tessellation)
# 같은 씨앗 → 언제나 같은 그림 (DX11 · OpenGL · Vulkan · GLES 비교용)
import json
import os
import sys

import numpy as np
from PIL import Image

N = 512


def noise(scale, seed):
    r = np.random.default_rng(seed)
    g = r.random((scale + 1, scale + 1))
    g[-1, :] = g[0, :]
    g[:, -1] = g[:, 0]   # 이음매 없이 (타일링)
    x = np.linspace(0, scale, N, endpoint=False)
    i = x.astype(int)
    f = x - i
    f = f * f * (3 - 2 * f)
    a, b, c, d = g[i][:, i], g[i][:, i + 1], g[i + 1][:, i], g[i + 1][:, i + 1]
    fx, fy = f[None, :], f[:, None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def save_gray(path, h):
    Image.fromarray((np.clip(h, 0, 1) * 255).astype(np.uint8), 'L').save(path)


def save_rgb(path, c):
    Image.fromarray((np.clip(c, 0, 1) * 255).astype(np.uint8), 'RGB').save(path)


def material(name, tiling, amplitude):
    return {
        "Shader": "Universal Render Pipeline/Lit", "ResourcePath": f"Assets\\TessTest\\{name}.mat",
        "BaseMapPath": f"Assets\\TessTest\\{name}_Base.png", "NormalMapPath": "", "MetallicMapPath": "", "OcclusionMapPath": "", "EmissionMapPath": "",
        "HeightMapPath": f"Assets\\TessTest\\{name}_Height.png", "DisplacementMode": "Tessellation", "HeightAmplitude": amplitude, "HeightBase": 0.5,
        "TessellationFactor": 16.0, "TessellationTriangleSize": 12.0, "TessellationFadeDistance": 50.0,
        "BaseColor": [1, 1, 1, 1], "Metallic": 0.0, "Smoothness": 0.25, "SmoothnessSource": 0, "NormalScale": 1.0, "OcclusionStrength": 1.0,
        "Tiling": tiling, "Offset": [0, 0], "AlphaClipping": 0, "Cutoff": 0.5, "ReceiveShadows": 1, "SpecularHighlights": 1, "EnvironmentReflections": 1,
        "Emission": False, "EmissionColor": [0, 0, 0], "EmissionIntensity": 1.0, "Priority": 0, "UseShadowMap": 1,
    }


def main(out):
    os.makedirs(out, exist_ok=True)
    fbm = sum(noise(4 * 2 ** o, o) * 0.5 ** o for o in range(5))
    fbm /= fbm.max()
    y, x = np.mgrid[0:N, 0:N] / N

    # 벽: 8 줄 × 4 장, 줄마다 반 장 어긋나게. 줄눈 4 px, 모서리 10 px 둥글게
    rows, cols, mortar = 8, 4, 4
    r = np.random.default_rng(1)
    v = y * rows
    row = np.floor(v).astype(int)
    u = x * cols + (row % 2) * 0.5
    col = np.floor(u).astype(int)
    fu, fv = u - col, v - row
    d = np.minimum(np.minimum(fu, 1 - fu) / cols * N, np.minimum(fv, 1 - fv) / rows * N)
    edge = np.sqrt(np.clip((d - mortar) / 10.0, 0, 1))
    pick = r.random((rows + 1, cols + 2))[row % rows, col % cols]
    h = np.where(edge > 0, 0.55 + 0.3 * edge + pick * 0.15 + 0.12 * (fbm - 0.5), 0.08 + 0.05 * fbm)
    save_gray(os.path.join(out, 'StoneWall_Height.png'), h)
    brick = np.stack([0.55 + 0.15 * pick, 0.5 + 0.12 * pick, 0.45 + 0.1 * pick], -1) * (0.75 + 0.35 * fbm[..., None])
    grout = np.array([0.38, 0.36, 0.33]) * (0.8 + 0.3 * fbm[..., None])
    save_rgb(os.path.join(out, 'StoneWall_Base.png'), np.where((edge > 0)[..., None], brick, grout))

    # 바닥: 보로노이 자갈 60 개, 단면이 둥글게
    rng = np.random.default_rng(7)
    pts = rng.random((60, 2))
    tiled = np.concatenate([pts + np.array([dx, dy]) for dx in (-1, 0, 1) for dy in (-1, 0, 1)])
    dist = np.sqrt(((np.stack([x, y], -1).reshape(-1, 1, 2) - tiled[None]) ** 2).sum(-1))
    s = np.sort(dist, axis=1)
    gap = (s[:, 1] - s[:, 0]).reshape(N, N)
    idx = (np.argmin(dist, axis=1) % 60).reshape(N, N)
    g = np.clip((gap - 0.006) * 9, 0, 1)
    dome = np.sqrt(1 - (1 - g) ** 2)
    stone = rng.random(60)[idx]
    save_gray(os.path.join(out, 'Cobble_Height.png'), 0.08 + 0.7 * dome * (0.85 + 0.3 * stone) + 0.06 * (fbm - 0.5))
    cobble = np.stack([0.5 + 0.2 * stone, 0.47 + 0.15 * stone, 0.42 + 0.1 * stone], -1) * (0.7 + 0.4 * fbm[..., None])
    dirt = np.array([0.3, 0.26, 0.2]) * (0.8 + 0.3 * fbm[..., None])
    save_rgb(os.path.join(out, 'Cobble_Base.png'), np.where((g > 0.02)[..., None], cobble, dirt))

    for name, tiling, amp in (('StoneWall', [2, 1.5], 0.08), ('Cobble', [3, 3], 0.06)):
        with open(os.path.join(out, name + '.mat'), 'w', encoding='utf-8') as f:
            json.dump(material(name, tiling, amp), f, indent=4)
    print(out)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'TessTest')
