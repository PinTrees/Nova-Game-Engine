# 기본 패키지 Prototype (Resources/Packages/Prototype) 을 만든다: 미터 격자 텍스처 · 재질 · 모듈 블록아웃 메시 (GLB).
#  python Tools/prototype/make_prototype.py
#  - 텍스처: 1024 px = 1 m. 0.25 m 가는 선, 0.5 m 중간 선, 1 m 테두리, 모서리에 "1m". 흰색 ~ 어두운 회색 5 단계
#  - 메시: UV 가 미터 단위 (면마다 그 면의 평면에 투영) — 조각 크기와 상관없이 격자 한 칸이 1 m 로 보인다
#  - 좌표: glTF 그대로 (오른손 · Y 위 · 반시계 — 엔진이 가져올 때 왼손으로 바꾼다). 단위 m, 바닥 가운데가 원점 (벽은 X 를 따라, 두께는 Z 가운데)
import math, os, json, struct
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', 'Resources', 'Packages', 'Prototype'))

# ------------------------------------------------------------------ 텍스처
TONES = [  # 이름, 바탕, 선 (밝은 바탕은 어두운 선, 어두운 바탕은 밝은 선)
    ('Light', (232, 232, 230), (150, 150, 148)),
    ('LightGray', (196, 196, 194), (128, 128, 126)),
    ('Gray', (150, 150, 149), (96, 96, 95)),
    ('DarkGray', (100, 100, 100), (150, 150, 150)),
    ('Charcoal', (62, 62, 63), (112, 112, 113)),
]

def make_texture(name, bg, line, size=1024):
    img = Image.new('RGB', (size, size), bg)
    d = ImageDraw.Draw(img)
    mix = lambda a, b, t: tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
    minor, mid = mix(bg, line, 0.45), mix(bg, line, 0.75)
    for k in range(1, 4):   # 0.25 m
        p = k * size // 4
        w = 3 if k == 2 else 2
        c = mid if k == 2 else minor
        d.rectangle([p - w // 2, 0, p - w // 2 + w - 1, size], fill=c)
        d.rectangle([0, p - w // 2, size, p - w // 2 + w - 1], fill=c)
    for k in range(1, 10):   # 0.1 m 눈금 (테두리 쪽 짧게)
        p = k * size // 10
        for a, b in ((0, 18), (size - 18, size)):
            d.rectangle([p - 1, a, p, b], fill=minor)
            d.rectangle([a, p - 1, b, p], fill=minor)
    e = 6   # 1 m 테두리 (칸 둘레 — 이어 붙이면 12 px 선)
    d.rectangle([0, 0, size, e - 1], fill=line); d.rectangle([0, size - e, size, size], fill=line)
    d.rectangle([0, 0, e - 1, size], fill=line); d.rectangle([size - e, 0, size, size], fill=line)
    try:
        font = ImageFont.truetype(r'C:\Windows\Fonts\arialbd.ttf', 64)
        small = ImageFont.truetype(r'C:\Windows\Fonts\arial.ttf', 36)
    except OSError:
        font = small = ImageFont.load_default()
    d.text((34, 26), '1m', font=font, fill=line)
    d.text((size // 2 + 18, size // 2 + 12), '0.5', font=small, fill=mid)
    path = os.path.join(ROOT, 'Textures', f'Prototype_{name}.png')
    img.save(path, optimize=True)

def make_material(name):
    m = {
        "Shader": "UniversalRenderPipeline/Lit",
        "ResourcePath": f"Resources\\Packages\\Prototype\\Materials\\Prototype_{name}.mat",
        "BaseMapPath": f"Resources\\Packages\\Prototype\\Textures\\Prototype_{name}.png",
        "NormalMapPath": "", "MetallicMapPath": "", "OcclusionMapPath": "", "EmissionMapPath": "",
        "BaseColor": [1, 1, 1, 1], "Metallic": 0.0, "Smoothness": 0.25, "SmoothnessSource": 0,
        "NormalScale": 1.0, "OcclusionStrength": 1.0, "Tiling": [1, 1], "Offset": [0, 0],
        "AlphaClipping": 0, "Cutoff": 0.5, "ReceiveShadows": 1, "SpecularHighlights": 1, "EnvironmentReflections": 1,
        "Emission": False, "EmissionColor": [0, 0, 0], "EmissionIntensity": 1.0, "Priority": 0, "UseShadowMap": 1,
    }
    with open(os.path.join(ROOT, 'Materials', f'Prototype_{name}.mat'), 'w', encoding='utf-8') as f:
        json.dump(m, f, indent=4)

# ------------------------------------------------------------------ 메시
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a):
    l = math.sqrt(dot(a, a))
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-12 else (0.0, 1.0, 0.0)

class Mesh:
    def __init__(self):
        self.v, self.n, self.t, self.f = [], [], [], []   # f: ((vi, ti, ni) x 3)

    def _vert(self, p, uv, n):
        self.v.append(p); self.t.append(uv); self.n.append(n)
        return len(self.v)

    # 평평한 다각형 (볼록, 바깥에서 볼 때 반시계). UV = 그 면의 평면에 미터로 투영 (가로 = 수평, 세로 = 위로)
    def poly(self, pts, uvs=None, normal=None):
        n = normal or norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
        if len(pts) > 3 and normal is None:   # 첫 세 점이 한 줄이면 다른 점으로
            for k in range(2, len(pts)):
                c = cross(sub(pts[1], pts[0]), sub(pts[k], pts[0]))
                if dot(c, c) > 1e-12:
                    n = norm(c); break
        if uvs is None:
            up = (0.0, 1.0, 0.0) if abs(n[1]) < 0.999 else (0.0, 0.0, -1.0 if n[1] > 0 else 1.0)
            t = norm(cross(up, n))
            b = cross(n, t)
            uvs = [(dot(p, t), dot(p, b)) for p in pts]
        ids = [self._vert(p, uv, n) for p, uv in zip(pts, uvs)]
        for k in range(1, len(ids) - 1):
            self.f.append((ids[0], ids[k], ids[k + 1]))

    def quad(self, a, b, c, d, uvs=None): self.poly([a, b, c, d], uvs)

    def box(self, mn, mx):
        x0, y0, z0 = mn; x1, y1, z1 = mx
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))   # +Z
        self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0))   # -Z
        self.quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1))   # +X
        self.quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0))   # -X
        self.quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0))   # +Y
        self.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1))   # -Y

    # 2D 외곽 (XY, 반시계) 을 Z 로 두께 th 만큼 뽑는다 (z = -th/2 .. th/2). 앞뒤 면은 front 함수로 (구멍 있는 벽은 따로 조각)
    def extrude_convex(self, pts2, th):
        z0, z1 = -th / 2, th / 2
        self.poly([(x, y, z1) for x, y in pts2])
        self.poly([(x, y, z0) for x, y in reversed(pts2)])
        for i in range(len(pts2)):
            (ax, ay), (bx, by) = pts2[i], pts2[(i + 1) % len(pts2)]
            self.quad((ax, ay, z0), (bx, by, z0), (bx, by, z1), (ax, ay, z1)) if False else self.quad((ax, ay, z1), (ax, ay, z0), (bx, by, z0), (bx, by, z1))

    # 원기둥 옆면 (매끈한 노멀, U = 둘레 길이, V = 높이)
    def cylinder(self, r, y0, y1, seg=24, cx=0.0, cz=0.0, caps=True, r1=None, a0=0.0, a1=2 * math.pi, inward=False):
        r1 = r if r1 is None else r1
        slope = (r - r1) / max(1e-6, (y1 - y0))
        for i in range(seg):
            t0, t1 = a0 + (a1 - a0) * i / seg, a0 + (a1 - a0) * (i + 1) / seg
            def P(t, rr, y): return (cx + rr * math.cos(t), y, cz - rr * math.sin(t))
            def N(t):
                n = norm((math.cos(t), slope, -math.sin(t)))
                return mul(n, -1) if inward else n
            u0, u1 = t0 * r, t1 * r
            pts = [P(t0, r, y0), P(t1, r, y0), P(t1, r1, y1), P(t0, r1, y1)]
            uvs = [(u0, y0), (u1, y0), (u1, y1), (u0, y1)]
            ns = [N(t0), N(t1), N(t1), N(t0)]
            if inward:
                pts, uvs, ns = pts[::-1], uvs[::-1], ns[::-1]
            if r1 < 1e-6:   # 원뿔 끝: 세모
                pts, uvs, ns = pts[:3], uvs[:3], ns[:3]
                pts[2] = (cx, y1, cz); uvs[2] = ((u0 + u1) / 2, y1)
            ids = [self._vert(p, uv, n) for p, uv, n in zip(pts, uvs, ns)]
            for k in range(1, len(ids) - 1):
                self.f.append((ids[0], ids[k], ids[k + 1]))
        if caps and abs(a1 - a0 - 2 * math.pi) < 1e-6:
            ring = lambda rr, y: [(cx + rr * math.cos(a0 + 2 * math.pi * i / seg), y, cz - rr * math.sin(a0 + 2 * math.pi * i / seg)) for i in range(seg)]
            if r1 > 1e-6:
                self.poly(ring(r1, y1))
            self.poly(list(reversed(ring(r, y0))))

    def merge(self, other, offset=(0, 0, 0), rot_y=0.0):
        c, s = math.cos(rot_y), math.sin(rot_y)
        R = lambda p: (p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c)
        base = len(self.v)
        for p, uv, n in zip(other.v, other.t, other.n):
            self.v.append(add(R(p), offset)); self.t.append(uv); self.n.append(R(n))
        for tri in other.f:
            self.f.append(tuple(i + base for i in tri))

    def save(self, path, name):
        # GLB 하나 = 메시 하나 (POSITION · NORMAL · TEXCOORD_0 · 인덱스). glTF 의 V 는 아래로 → 높이를 위로 보이게 -v
        os.makedirs(os.path.dirname(path), exist_ok=True)
        pos = b''.join(struct.pack('<3f', *p) for p in self.v)
        nrm = b''.join(struct.pack('<3f', *n) for n in self.n)
        uvs = b''.join(struct.pack('<2f', u, -v) for u, v in self.t)
        idx = b''.join(struct.pack('<3I', a - 1, b - 1, c - 1) for a, b, c in self.f)
        views, blob = [], b''
        for data in (pos, nrm, uvs, idx):
            views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)})
            blob += data + bytes((4 - len(data) % 4) % 4)
        mn = [min(p[i] for p in self.v) for i in range(3)]
        mx = [max(p[i] for p in self.v) for i in range(3)]
        n = len(self.v)
        gltf = {
            "asset": {"version": "2.0", "generator": "NOVA Prototype (Tools/prototype/make_prototype.py)"},
            "scene": 0, "scenes": [{"nodes": [0]}],
            "nodes": [{"name": name, "mesh": 0}],
            "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3}]}],
            "buffers": [{"byteLength": len(blob)}],
            "bufferViews": [dict(v, target=34962) for v in views[:3]] + [dict(views[3], target=34963)],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3", "min": mn, "max": mx},
                {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
                {"bufferView": 2, "componentType": 5126, "count": n, "type": "VEC2"},
                {"bufferView": 3, "componentType": 5125, "count": len(self.f) * 3, "type": "SCALAR"},
            ],
        }
        js = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
        js += b' ' * ((4 - len(js) % 4) % 4)
        total = 12 + 8 + len(js) + 8 + len(blob)
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, total))
            f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
            f.write(struct.pack('<II', len(blob), 0x004E4942)); f.write(blob)

# ---- 조각 만들기 도우미
def wall_with_opening(w, h, th, ox0, ox1, oy0, oy1, arch=False, seg=12):
    """벽 (X: -w/2..w/2, Y: 0..h, 두께 th) 에 구멍 (X ox0..ox1, Y oy0..oy1, arch 면 위가 반원 — 반지름 (ox1-ox0)/2, 반원 꼭대기가 oy1)."""
    m = Mesh()
    x0, x1 = -w / 2, w / 2
    z0, z1 = -th / 2, th / 2
    r = (ox1 - ox0) / 2
    cx = (ox0 + ox1) / 2
    ry = oy1 - r if arch else oy1   # 반원이 시작하는 높이 (아치)
    # 앞 · 뒤 면을 세로 띠로 (구멍 왼쪽 · 오른쪽 · 아래 · 위)
    for z, front in ((z1, True), (z0, False)):
        def face(pts):
            m.poly(pts if front else list(reversed(pts)))
        face([(x0, 0, z), (ox0, 0, z), (ox0, h, z), (x0, h, z)])
        face([(ox1, 0, z), (x1, 0, z), (x1, h, z), (ox1, h, z)])
        if oy0 > 0:
            face([(ox0, 0, z), (ox1, 0, z), (ox1, oy0, z), (ox0, oy0, z)])
        if arch:
            # 반원과 벽 위 사이: 세로로 쪼갠 사다리꼴 (x 가 단조라 그대로 붙는다)
            pts = [(cx - r * math.cos(math.pi * i / seg), ry + r * math.sin(math.pi * i / seg)) for i in range(seg + 1)]
            for i in range(seg):
                (ax, ay), (bx, by) = pts[i], pts[i + 1]
                face([(ax, ay, z), (bx, by, z), (bx, h, z), (ax, h, z)])
        elif oy1 < h:
            face([(ox0, oy1, z), (ox1, oy1, z), (ox1, h, z), (ox0, h, z)])
    # 바깥 테두리 (위 · 양옆 · 아래)
    m.quad((x0, h, z1), (x1, h, z1), (x1, h, z0), (x0, h, z0))
    m.quad((x1, 0, z1), (x1, 0, z0), (x1, h, z0), (x1, h, z1))
    m.quad((x0, 0, z0), (x0, 0, z1), (x0, h, z1), (x0, h, z0))
    for a, b in (((x0, 0), (ox0, 0)), ((ox1, 0), (x1, 0))) + ((((ox0, 0), (ox1, 0)),) if oy0 > 0 else ()):
        m.quad((a[0], 0, z0), (b[0], 0, z0), (b[0], 0, z1), (a[0], 0, z1))
    # 구멍 안쪽 면 (안을 향해)
    m.quad((ox0, oy0, z1), (ox0, oy0, z0), (ox0, ry, z0), (ox0, ry, z1))       # 왼쪽 (노멀 +X)
    m.quad((ox1, oy0, z0), (ox1, oy0, z1), (ox1, ry, z1), (ox1, ry, z0))       # 오른쪽 (노멀 -X)
    if oy0 > 0:
        m.quad((ox0, oy0, z0), (ox0, oy0, z1), (ox1, oy0, z1), (ox1, oy0, z0))   # 아래 (창턱, 노멀 +Y)
    if arch:
        for i in range(seg):
            t0, t1 = math.pi * i / seg, math.pi * (i + 1) / seg
            a = (cx - r * math.cos(t0), ry + r * math.sin(t0))
            b = (cx - r * math.cos(t1), ry + r * math.sin(t1))
            m.quad((b[0], b[1], z1), (a[0], a[1], z1), (a[0], a[1], z0), (b[0], b[1], z0))
    else:
        m.quad((ox0, oy1, z1), (ox0, oy1, z0), (ox1, oy1, z0), (ox1, oy1, z1))   # 위 (노멀 -Y)
    return m

def box(mn, mx):
    m = Mesh(); m.box(mn, mx); return m

def wall(w, h=3.0, th=0.2):
    return box((-w / 2, 0, -th / 2), (w / 2, h, th / 2))

def stairs(width, rise, run, steps):
    """계단: X 폭 width, +Z 쪽으로 올라간다 (Z 0..run), 계단마다 단단한 블록 (아래가 채워진 계단)."""
    m = Mesh()
    sr, sd = rise / steps, run / steps
    for i in range(steps):
        m.box((-width / 2, 0, i * sd), (width / 2, (i + 1) * sr, (i + 1) * sd))
    return m

def wedge(w, h, d):
    """쐐기 (경사로): X 폭 w, Z 0..d 동안 0 → h 로 올라간다."""
    m = Mesh()
    x0, x1 = -w / 2, w / 2
    m.quad((x0, 0, 0), (x1, 0, 0), (x1, h, d), (x0, h, d))                        # 경사면
    m.quad((x1, 0, d), (x0, 0, d), (x0, h, d), (x1, h, d))                        # 뒤 (세로)
    m.quad((x0, 0, 0), (x0, 0, d), (x1, 0, d), (x1, 0, 0))                        # 바닥
    m.poly([(x1, 0, 0), (x1, 0, d), (x1, h, d)])                                  # 옆
    m.poly([(x0, 0, d), (x0, 0, 0), (x0, h, d)])
    return m

def gable_roof(span, length, rise, th=0.15, overhang=0.3, gable_ends=False):
    """박공 지붕: X 가 폭 span (처마 사이), Z 가 길이, 용마루가 Z 를 따라. 바닥 (처마 높이) 이 y = 0."""
    m = Mesh()
    hs = span / 2 + overhang
    slope = rise / (span / 2)
    z0, z1 = -length / 2 - overhang, length / 2 + overhang
    ang = math.atan(slope)
    dy = th / math.cos(ang)   # 판 두께의 세로 몫
    yo = -overhang * slope    # 처마 끝 높이 (벽 밖으로 내려온 만큼)
    for side in (-1, 1):
        ex = side * hs
        # 바깥 (위) 면 · 안 (아래) 면
        top = [(ex, yo + dy, z0), (0, rise + dy, z0), (0, rise + dy, z1), (ex, yo + dy, z1)]
        bot = [(ex, yo, z0), (0, rise, z0), (0, rise, z1), (ex, yo, z1)]
        if side > 0:
            m.poly(top); m.poly(list(reversed(bot)))
        else:
            m.poly(list(reversed(top))); m.poly(bot)
        # 처마 끝면
        e = [(ex, yo, z0), (ex, yo, z1), (ex, yo + dy, z1), (ex, yo + dy, z0)]
        m.poly(e if side > 0 else list(reversed(e)))
        # 박공 쪽 끝면 (판 단면)
        for z, s in ((z0, -1), (z1, 1)):
            q = [(ex, yo, z), (0, rise, z), (0, rise + dy, z), (ex, yo + dy, z)]
            m.poly(q if (s * side) < 0 else list(reversed(q)))
    if gable_ends:   # 박공 벽 (지붕 아래 세모 — 벽 위를 막는다)
        for z, s in ((-length / 2 + 0.1, -1), (length / 2 - 0.1, 1)):
            t = [(-span / 2, 0, z), (span / 2, 0, z), (0, rise, z)]
            m.poly(t if s > 0 else list(reversed(t)))
    return m

def gable_wall(w, rise, th=0.2):
    """박공 벽 (세모): 폭 w, 높이 rise — 벽 위에 올려 지붕 끝을 막는다."""
    m = Mesh()
    m.extrude_convex([(-w / 2, 0), (w / 2, 0), (0, rise)], th)
    return m

def hip_roof(w, d, rise, overhang=0.3):
    """모임 지붕 (사각뿔): 바닥 w x d (+ 처마), 꼭대기 rise. 처마 바닥이 y = 0."""
    m = Mesh()
    x, z = w / 2 + overhang, d / 2 + overhang
    a, b, c, e = (-x, 0, z), (x, 0, z), (x, 0, -z), (-x, 0, -z)
    top = (0, rise, 0)
    for p, q in ((a, b), (b, c), (c, e), (e, a)):
        m.poly([p, q, top])
    m.poly([e, c, b, a])
    return m

def battlement_wall(w, h, th, merlon=0.6, gap=0.5, mh=0.7):
    """성벽: 벽 + 위에 총안 (머를론). X 폭 w."""
    m = box((-w / 2, 0, -th / 2), (w / 2, h, th / 2))
    n = int((w + gap) // (merlon + gap))
    total = n * merlon + (n - 1) * gap
    x = -total / 2
    for i in range(n):
        m.box((x, h, -th / 2), (x + merlon, h + mh, th / 2))
        x += merlon + gap
    return m

def round_tower(r, h, seg=24, battlements=False):
    m = Mesh()
    m.cylinder(r, 0, h, seg)
    if battlements:
        k = 12
        for i in range(k):
            if i % 2: continue
            t0, t1 = 2 * math.pi * i / k, 2 * math.pi * (i + 1) / k
            part = Mesh()
            part.cylinder(r, h, h + 0.7, 3, a0=t0, a1=t1, caps=False)
            part.cylinder(r - 0.4, h, h + 0.7, 3, a0=t0, a1=t1, caps=False, inward=True)
            # 위 · 양끝
            def P(t, rr, y): return (rr * math.cos(t), y, -rr * math.sin(t))
            for j in range(3):
                a, b = t0 + (t1 - t0) * j / 3, t0 + (t1 - t0) * (j + 1) / 3
                part.quad(P(a, r - 0.4, h + 0.7), P(a, r, h + 0.7), P(b, r, h + 0.7), P(b, r - 0.4, h + 0.7))
            part.quad(P(t0, r - 0.4, h), P(t0, r, h), P(t0, r, h + 0.7), P(t0, r - 0.4, h + 0.7))
            part.quad(P(t1, r, h), P(t1, r - 0.4, h), P(t1, r - 0.4, h + 0.7), P(t1, r, h + 0.7))
            m.merge(part)
    return m

def cone(r, h, seg=24, y0=0.0):
    m = Mesh(); m.cylinder(r, y0, y0 + h, seg, r1=0.0); return m

def icosphere(r, sub_n=1, jitter=0.0, seed=1, squash=1.0):
    import random
    rnd = random.Random(seed)
    t = (1 + 5 ** 0.5) / 2
    vs = [norm(v) for v in [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]]
    fs = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
          (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(sub_n):
        cache, nf = {}, []
        def mid(a, b):
            k = (min(a, b), max(a, b))
            if k not in cache:
                vs.append(norm(mul(add(vs[a], vs[b]), 0.5))); cache[k] = len(vs) - 1
            return cache[k]
        for a, b, c in fs:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        fs = nf
    pts = [mul(v, r * (1 + rnd.uniform(-jitter, jitter))) for v in vs]
    pts = [(p[0], p[1] * squash, p[2]) for p in pts]
    m = Mesh()
    for a, b, c in fs:   # 각진 면 (low poly)
        m.poly([pts[a], pts[b], pts[c]])
    return m

def lift(m, dy):
    out = Mesh(); out.merge(m, (0, dy, 0)); return out

# ------------------------------------------------------------------ 조각 목록
def pieces():
    P = {}
    # 기본 도형 (1 m)
    P['Shapes/Cube_1m'] = box((-0.5, 0, -0.5), (0.5, 1, 0.5))
    P['Shapes/Wedge_1m'] = wedge(1, 1, 1)
    m = Mesh(); m.cylinder(0.5, 0, 1, 24); P['Shapes/Cylinder_1m'] = m
    P['Shapes/Cone_1m'] = cone(0.5, 1)
    m = Mesh(); m.cylinder(0.5, 0, 1, 12, a0=0, a1=math.pi, caps=False)
    m.quad((0.5, 0, 0), (-0.5, 0, 0), (-0.5, 1, 0), (0.5, 1, 0))   # 평평한 면 (-Z)
    for y, up in ((0, False), (1, True)):
        ring = [(0.5 * math.cos(math.pi * i / 12), y, -0.5 * math.sin(math.pi * i / 12)) for i in range(13)]
        m.poly(ring if up else list(reversed(ring)))
    P['Shapes/HalfCylinder_1m'] = m
    P['Shapes/Pyramid_1m'] = hip_roof(1, 1, 1, overhang=0)
    P['Shapes/Sphere_1m'] = lift(icosphere(0.5, 2), 0.5)
    P['Shapes/Stairs_1m'] = stairs(1, 1, 1, 5)

    # 바닥 (윗면이 y = 0)
    for s in (1, 2, 4):
        P[f'Floor/Floor_{s}x{s}'] = box((-s / 2, -0.2, -s / 2), (s / 2, 0, s / 2))
    P['Floor/Platform_4x4_1m'] = box((-2, 0, -2), (2, 1, 2))

    # 벽 (높이 3 m, 두께 0.2 m, X 를 따라)
    for w in (1, 2, 4):
        P[f'Walls/Wall_{w}m'] = wall(w)
    P['Walls/Wall_Half_2m'] = wall(2, 1.0)
    P['Walls/Wall_Window_2m'] = wall_with_opening(2, 3, 0.2, -0.5, 0.5, 0.9, 2.1)
    P['Walls/Wall_Window_Arch_2m'] = wall_with_opening(2, 3, 0.2, -0.45, 0.45, 0.9, 2.4, arch=True)
    P['Walls/Wall_Window_Small_1m'] = wall_with_opening(1, 3, 0.2, -0.25, 0.25, 1.4, 2.1)
    P['Walls/Wall_Door_2m'] = wall_with_opening(2, 3, 0.2, -0.5, 0.5, 0.0, 2.2)
    P['Walls/Wall_Door_Arch_2m'] = wall_with_opening(2, 3, 0.2, -0.6, 0.6, 0.0, 2.6, arch=True)
    P['Walls/Wall_Archway_4m'] = wall_with_opening(4, 3, 0.2, -1.2, 1.2, 0.0, 2.8, arch=True, seg=16)
    P['Walls/Wall_Gable_4m'] = gable_wall(4, 2)
    P['Walls/Wall_Gable_6m'] = gable_wall(6, 3)

    # 기둥 · 보
    P['Structure/Pillar_Square_3m'] = box((-0.2, 0, -0.2), (0.2, 3, 0.2))
    m = Mesh(); m.cylinder(0.22, 0.25, 2.75, 16)
    m.box((-0.32, 0, -0.32), (0.32, 0.25, 0.32)); m.box((-0.32, 2.75, -0.32), (0.32, 3, 0.32))
    P['Structure/Pillar_Round_3m'] = m
    P['Structure/Beam_4m'] = box((-2, -0.1, -0.1), (2, 0.1, 0.1))
    P['Structure/Chimney'] = box((-0.35, 0, -0.35), (0.35, 2.0, 0.35))
    m = Mesh()   # 울타리 2 m: 기둥 두 개 + 가로대 두 개
    for x in (-0.95, 0.95):
        m.box((x - 0.06, 0, -0.06), (x + 0.06, 1.1, 0.06))
    for y in (0.4, 0.85):
        m.box((-1, y, -0.04), (1, y + 0.1, 0.04))
    P['Structure/Fence_2m'] = m
    P['Structure/Door_Slab'] = box((-0.48, 0, -0.04), (0.48, 2.18, 0.04))
    m = Mesh()   # 다리 4 m (판 + 난간)
    m.box((-1, -0.2, -2), (1, 0, 2))
    for x in (-0.95, 0.95):
        for z in (-1.9, 0, 1.9):
            m.box((x - 0.06, 0, z - 0.06), (x + 0.06, 1.0, z + 0.06))
        m.box((x - 0.05, 0.9, -2), (x + 0.05, 1.0, 2))
    P['Structure/Bridge_4m'] = m

    # 계단 · 경사로
    P['Stairs/Stairs_2m_Rise1'] = stairs(2, 1, 1.5, 5)
    P['Stairs/Stairs_2m_Rise3'] = stairs(2, 3, 4.5, 15)
    P['Stairs/Ramp_2x4'] = wedge(2, 1, 4)

    # 지붕 (처마 바닥이 y = 0 — 3 m 벽 위에 y = 3 으로)
    P['Roof/Roof_Gable_4x4'] = gable_roof(4, 4, 2)
    P['Roof/Roof_Gable_4x8'] = gable_roof(4, 8, 2)
    P['Roof/Roof_Gable_6x8'] = gable_roof(6, 8, 3)
    P['Roof/Roof_Hip_4x4'] = hip_roof(4, 4, 2)
    P['Roof/Roof_Hip_6x6'] = hip_roof(6, 6, 2.5)
    m = Mesh()   # 한쪽 경사 (외쪽 지붕) 4 m 길이, 2 m 깊이, 1 m 높이
    m.merge(lift(wedge(4.4, 1, 2.3), 0), (0, 0, -1.15))
    P['Roof/Roof_Shed_4x2'] = m
    P['Roof/Roof_Cone_Tower'] = cone(2.6, 4.0, 24)
    P['Roof/Roof_Cone_Small'] = cone(1.4, 2.5, 16)

    # 성 · 탑
    P['Castle/Castle_Wall_4m'] = battlement_wall(4, 4, 1.0)
    P['Castle/Castle_Gate_4m'] = wall_with_opening(4, 4, 1.0, -1.2, 1.2, 0.0, 3.2, arch=True, seg=16)
    P['Castle/Tower_Round_4m'] = round_tower(2.0, 4.0)
    P['Castle/Tower_Round_Top'] = round_tower(2.0, 1.0, battlements=True)
    P['Castle/Tower_Square_4x4'] = box((-2, 0, -2), (2, 4, 2))

    # 소품 · 자연 (블록아웃용 자리 표시)
    m = Mesh(); m.cylinder(0.3, 0, 0.9, 16); P['Props/Barrel'] = m
    P['Props/Crate_1m'] = box((-0.5, 0, -0.5), (0.5, 1, 0.5))
    m = Mesh(); m.box((-0.8, 0.7, -0.45), (0.8, 0.8, 0.45))
    for x in (-0.7, 0.7):
        for z in (-0.35, 0.35):
            m.box((x - 0.05, 0, z - 0.05), (x + 0.05, 0.7, z + 0.05))
    P['Props/Table'] = m
    m = Mesh(); m.cylinder(0.25, 0, 2.5, 8); m.merge(cone(1.6, 4.5, 10, y0=1.8)); P['Nature/Tree_Pine'] = m
    m = Mesh(); m.cylinder(0.22, 0, 2.2, 8); m.merge(lift(icosphere(1.7, 1, 0.12, seed=3), 3.4)); P['Nature/Tree_Round'] = m
    P['Nature/Rock_Large'] = lift(icosphere(1.4, 1, 0.25, seed=7, squash=0.7), 0.5)
    P['Nature/Rock_Small'] = lift(icosphere(0.5, 1, 0.25, seed=11, squash=0.75), 0.2)
    P['Nature/Bush'] = lift(icosphere(0.7, 1, 0.15, seed=5, squash=0.8), 0.45)
    return P

def main():
    for d in ('Textures', 'Materials', 'Meshes'):
        os.makedirs(os.path.join(ROOT, d), exist_ok=True)
    for name, bg, line in TONES:
        make_texture(name, bg, line)
        make_material(name)
    ps = pieces()
    for key, mesh in ps.items():
        cat, name = key.split('/')
        mesh.save(os.path.join(ROOT, 'Meshes', cat, name + '.glb'), name)
    print(f'textures {len(TONES)}, materials {len(TONES)}, meshes {len(ps)} -> {ROOT}')

if __name__ == '__main__':
    main()
