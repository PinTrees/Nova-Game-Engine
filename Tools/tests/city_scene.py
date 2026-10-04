# 도시 장면 (오클루전 컬링 쇼케이스 · 성능 검사): 재질 (.mat) + C# 생성 코드 (exec --file) + 재질 지정 batch 파일 (nova batch)
#   python Tools/tests/city_scene.py <프로젝트> <출력 폴더>  → 출력 폴더의 city.cs · city_mats.txt, 프로젝트의 Assets/Materials/City/*.mat
#  - 블록 8 x 8 (블록 40 m + 길 14 m), 블록마다 건물 4 ~ 6 채 (높이 12 ~ 70 m), 길 위 차 · 가로등, 인도 덤불 · 낮은 건물 옥상 돔
#  - 큰길 (x = 0) 의 사람 눈높이에서 보면 길 양쪽 건물이 옆 블록을 가린다
import json, math, os, random, sys
proj = sys.argv[1] if len(sys.argv) > 1 else r'E:\NovaTest\ScriptTest'
out = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(__file__))
os.makedirs(out, exist_ok=True)
matdir = os.path.join(proj, 'Assets', 'Materials', 'City')
os.makedirs(matdir, exist_ok=True)
tmpl = json.load(open(os.path.join(proj, 'Assets', 'Materials', 'Red Plastic.mat'), encoding='utf-8'))

mats = {
    'Concrete':  ([0.62, 0.60, 0.57, 1], 0.0, 0.2),
    'Sandstone': ([0.78, 0.66, 0.50, 1], 0.0, 0.25),
    'Brick':     ([0.55, 0.24, 0.18, 1], 0.0, 0.15),
    'GlassBlue': ([0.20, 0.35, 0.52, 1], 0.6, 0.85),
    'GlassTeal': ([0.18, 0.45, 0.45, 1], 0.6, 0.85),
    'DarkSteel': ([0.18, 0.19, 0.21, 1], 0.8, 0.55),
    'Asphalt':   ([0.12, 0.12, 0.13, 1], 0.0, 0.3),
    'Sidewalk':  ([0.50, 0.50, 0.48, 1], 0.0, 0.2),
    'CarRed':    ([0.75, 0.08, 0.06, 1], 0.3, 0.7),
    'CarYellow': ([0.92, 0.70, 0.08, 1], 0.3, 0.7),
    'CarWhite':  ([0.88, 0.88, 0.86, 1], 0.3, 0.7),
    'CarBlack':  ([0.05, 0.05, 0.06, 1], 0.4, 0.75),
    'Lamp':      ([0.10, 0.11, 0.12, 1], 0.7, 0.5),
    'Planter':   ([0.20, 0.42, 0.18, 1], 0.0, 0.3),
}
for name, (color, metallic, smooth) in mats.items():
    m = dict(tmpl)
    m['ResourcePath'] = 'Assets\\Materials\\City\\%s.mat' % name
    m['BaseColor'] = color
    m['Metallic'] = metallic
    m['Smoothness'] = smooth
    json.dump(m, open(os.path.join(matdir, name + '.mat'), 'w', encoding='utf-8'), indent=4)

random.seed(7)
objs = []   # (이름, 모양, 위치, 크기, 재질)
N, BLOCK, STREET = 8, 40.0, 14.0
PITCH = BLOCK + STREET
x0 = -(N * PITCH) / 2 + STREET / 2
# 큰길 (x = 0): 블록 열 사이 길이 x = 0 이 되게 (N 짝수 → 가운데 길)
def bx(i): return x0 + i * PITCH + BLOCK / 2 - 0.0
for i in range(N):
    for j in range(N):
        cx = bx(i); cz = 20 + j * PITCH
        # 인도 (블록 바닥, 조금 높다)
        objs.append(('Walk_%d_%d' % (i, j), 'cube', (cx, 0.1, cz), (BLOCK + 4, 0.2, BLOCK + 4), 'Sidewalk'))
        # 건물: 블록을 2 x 2 또는 2 x 3 칸으로
        cols, rows = 2, random.choice([2, 3])
        for a in range(cols):
            for b in range(rows):
                w = BLOCK / cols - random.uniform(2, 5)
                d = BLOCK / rows - random.uniform(2, 4)
                h = random.choice([12, 18, 24, 30, 40, 55, 70]) * random.uniform(0.85, 1.15)
                px = cx - BLOCK / 2 + (a + 0.5) * BLOCK / cols
                pz = cz - BLOCK / 2 + (b + 0.5) * BLOCK / rows
                mat = random.choice(['Concrete', 'Sandstone', 'Brick', 'GlassBlue', 'GlassTeal', 'DarkSteel', 'Concrete', 'Brick'])
                objs.append(('Bld_%d_%d_%d%d' % (i, j, a, b), 'cube', (px, h / 2, pz), (w, h, d), mat))
                # 옥상 설비
                if h > 30:
                    objs.append(('Roof_%d_%d_%d%d' % (i, j, a, b), 'cube', (px + random.uniform(-2, 2), h + 1.5, pz + random.uniform(-2, 2)), (w * 0.35, 3, d * 0.35), 'DarkSteel'))
                elif h > 15:
                    dm = min(w, d) * 0.6
                    objs.append(('Dome_%d_%d_%d%d' % (i, j, a, b), 'sphere', (px, h, pz), (dm, dm * 0.7, dm), random.choice(['GlassTeal', 'DarkSteel', 'Sandstone'])))
        # 인도 화분 · 상자 (블록 둘레)
        for k in range(10):
            t = k / 10.0
            side = k % 4
            if side == 0: p = (cx - BLOCK / 2 - 1, cz - BLOCK / 2 + t * BLOCK)
            elif side == 1: p = (cx + BLOCK / 2 + 1, cz - BLOCK / 2 + t * BLOCK)
            elif side == 2: p = (cx - BLOCK / 2 + t * BLOCK, cz - BLOCK / 2 - 1)
            else: p = (cx - BLOCK / 2 + t * BLOCK, cz + BLOCK / 2 + 1)
            objs.append(('Pot_%d_%d_%d' % (i, j, k), 'sphere', (p[0], 0.9, p[1]), (1.4, 1.4, 1.4), 'Planter'))
        # 덤불 (구) 을 인도에 더: 가려지는 쪽의 무게 (구 하나 = 삼각형 수백)
        for k in range(14):
            side = random.randrange(4)
            t = random.uniform(0.05, 0.95)
            if side == 0: p = (cx - BLOCK / 2 - 1.2, cz - BLOCK / 2 + t * BLOCK)
            elif side == 1: p = (cx + BLOCK / 2 + 1.2, cz - BLOCK / 2 + t * BLOCK)
            elif side == 2: p = (cx - BLOCK / 2 + t * BLOCK, cz - BLOCK / 2 - 1.2)
            else: p = (cx - BLOCK / 2 + t * BLOCK, cz + BLOCK / 2 + 1.2)
            r = random.uniform(0.9, 1.6)
            objs.append(('Bush_%d_%d_%d' % (i, j, k), 'sphere', (p[0], r * 0.5 + 0.2, p[1]), (r, r, r), 'Planter'))
# 길: 세로 길 (x 방향 사이) · 가로 길
for i in range(N + 1):
    x = x0 + i * PITCH - STREET / 2
    objs.append(('RoadX_%d' % i, 'cube', (x, 0.0, 20 + (N * PITCH) / 2 - PITCH / 2), (STREET, 0.05, N * PITCH), 'Asphalt'))
    # 가로등 · 차
    for k in range(N * 4):
        z = 20 - PITCH / 2 + k * (PITCH / 4) + 3
        if k % 2 == 0:
            objs.append(('LampL_%d_%d' % (i, k), 'cylinder', (x - STREET / 2 + 0.6, 3.0, z), (0.25, 3.0, 0.25), 'Lamp'))
            objs.append(('LampR_%d_%d' % (i, k), 'cylinder', (x + STREET / 2 - 0.6, 3.0, z), (0.25, 3.0, 0.25), 'Lamp'))
        if random.random() < 0.55:
            lane = random.choice([-3.2, 3.2])
            objs.append(('Car_%d_%d' % (i, k), 'cube', (x + lane, 0.8, z + random.uniform(-4, 4)), (2.0, 1.4, 4.4), random.choice(['CarRed', 'CarYellow', 'CarWhite', 'CarBlack', 'CarWhite'])))
            objs.append(('CarTop_%d_%d' % (i, k), 'cube', (x + lane, 1.8, z), (1.8, 0.7, 2.4), 'CarBlack'))
for j in range(N + 1):
    z = 20 - BLOCK / 2 - STREET / 2 + j * PITCH
    objs.append(('RoadZ_%d' % j, 'cube', (0, 0.01, z), (N * PITCH, 0.05, STREET), 'Asphalt'))

# C#: 모양 · 위치 · 크기 (재질은 batch 의 set 으로)
prim = {'cube': 'PrimitiveType.Cube', 'cylinder': 'PrimitiveType.Cylinder', 'sphere': 'PrimitiveType.Sphere'}
lines = ['var root = new GameObject("City");']
for n, shape, p, s, m in objs:
    lines.append('{ var g = GameObject.CreatePrimitive(%s); g.name = "%s"; g.transform.position = new Vector3(%.2ff, %.2ff, %.2ff); g.transform.localScale = new Vector3(%.2ff, %.2ff, %.2ff); }'
                 % (prim[shape], n, p[0], p[1], p[2], s[0], s[1], s[2]))
lines.append('return %d;' % len(objs))
open(os.path.join(out, 'city.cs'), 'w', encoding='utf-8').write('\n'.join(lines))
with open(os.path.join(out, 'city_mats.txt'), 'w', encoding='utf-8') as f:
    for n, shape, p, s, m in objs:
        v = json.dumps({'m_MaterialPaths': ['Assets/Materials/City/%s.mat' % m]}).replace('"', '\\"')
        f.write('set %s --component MeshRenderer --values "%s"\n' % (n, v))
print(len(objs), 'objects; spheres', sum(1 for o in objs if o[1] == 'sphere'), '; buildings', sum(1 for o in objs if o[0].startswith('Bld')), '; cars', sum(1 for o in objs if o[0].startswith('Car_')))
