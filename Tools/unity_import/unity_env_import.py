"""Unity 환경 에셋 (나무 · 풀 · 덤불 · 바위 · 잔해 — FBX + HDRP / URP 재질) → NOVA 프로젝트.

FBX 를 복사하고, FBX 옆 Unity .meta 의 externalObjects (FBX 재질 이름 → Unity .mat) 로 재질을 바꿔
NOVA 가 FBX 를 읽을 때 쓰는 자리 <파일>_FBX.Materials/<재질 이름>.mat 에 미리 둔다 (있으면 NOVA 는 그대로 쓴다).
Unity 프리팹의 LOD Group (screenRelativeHeight) 과 종류 (나무 · 풀 · 덤불 · 바위 …) 는 catalog.json 에 — PCG 가 읽는다.

  python -I unity_env_import.py --src <Unity Assets/.../SeedMesh> --project E:/NovaTest/OpenWorld --dest Assets/Environment/SeedMesh [--max-texture 1024]

원본 Unity 프로젝트는 읽기만 한다.
"""
import argparse
import json
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unity_character_import import guid_map, parse_mat   # noqa: E402

SKIP_DIRS = re.compile(r'(^|/)(demo[ _]?scene|demo_scene|scene|sound|sounds|effects|sky|animation|anim|asset_list|asset_showcase|diffusion|shader graph|shader_graph)(/|$)', re.I)

# 종류: (이름, 정규식 — 경로 소문자)
KINDS = [
    ('Tree', r'tree(?!_mushroom)|cedrus_tree_var|sweetgum_.*tree|oreopanax'),
    ('Cactus', r'cactus|nopal|biznaga|agave'),
    ('Bush', r'bush|shrub|roldana'),
    ('Fern', r'fern'),
    ('Grass', r'grass|weed|flower|plants?_cover|ground_cover|cover_plant|moss_cover|clover|dandelion|plant'),
    ('Mushroom', r'mushroom'),
    ('Rock', r'rock|stone|boulder|cliff'),
    ('Debris', r'branch|log|stump|debris|leaves|twig|root'),
    ('Prop', r'.'),
]


def kind_of(rel):
    s = rel.lower().replace('\\', '/')
    for name, rx in KINDS:
        if re.search(rx, s):
            return name
    return 'Prop'


def fbx_materials(meta_path):
    """FBX .meta externalObjects: [(FBX 재질 이름, Unity .mat guid)]"""
    out = []
    try:
        text = open(meta_path, encoding='utf-8', errors='replace').read()
    except OSError:
        return out
    for m in re.finditer(r'type: UnityEngine:Material\s+assembly: [^\n]+\s+name: ([^\n]+)\s+second: \{fileID: \d+, guid: ([0-9a-f]+)', text):
        out.append((m.group(1).strip(), m.group(2)))
    return out


def safe(name):
    s = re.sub(r'[\\/:*?"<>|]', '_', name)
    return s or 'Material'


class Importer:
    def __init__(self, a):
        self.a = a
        self.guids = guid_map(a.src)
        self.copied = {}
        self.stats = {'fbx': 0, 'materials': 0, 'textures': 0, 'textureMB': 0.0, 'missing': set()}

    def abs(self, rel):
        return os.path.join(self.a.project, *rel.replace('\\', '/').split('/'))

    def texture(self, guid):
        src = self.guids.get(guid)
        if not src or not os.path.isfile(src):
            return ''
        if src in self.copied:
            return self.copied[src]
        sub = os.path.relpath(os.path.dirname(src), self.a.src)
        stem = os.path.splitext(os.path.basename(src))[0]
        rel = '/'.join([self.a.dest, 'Textures'] + [p for p in sub.replace('\\', '/').split('/') if p] + [stem + '.png'])
        dst = self.abs(rel)
        if not os.path.exists(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            from PIL import Image
            Image.MAX_IMAGE_PIXELS = None
            im = Image.open(src)
            im.load()
            # TIFF 의 4 번째 채널이 '지정 안 된 추가 샘플' 이면 PIL 이 버린다 (SeedMesh 잎 · 빌보드의 알파 = 불투명도, 마스크의 알파 = 매끄러움)
            if src.lower().endswith(('.tif', '.tiff')) and im.mode == 'RGB' and getattr(im, 'tag_v2', {}).get(277) == 4:
                import tifffile
                arr = tifffile.imread(src)
                if arr.ndim == 3 and arr.shape[2] == 4:
                    if arr.dtype != 'uint8':
                        arr = (arr / (65535.0 if arr.dtype == 'uint16' else 1.0) * (255.0 if arr.dtype == 'uint16' else 255.0)).clip(0, 255).astype('uint8')
                    im = Image.fromarray(arr, 'RGBA')
            if im.mode not in ('RGB', 'RGBA', 'L'):
                im = im.convert('RGBA' if 'A' in im.getbands() else 'RGB')
            w, h = im.size
            limit = self.a.max_texture
            if limit > 0 and max(w, h) > limit:
                sc = limit / float(max(w, h))
                im = im.resize((max(1, int(w * sc)), max(1, int(h * sc))), Image.LANCZOS)
            im.save(dst)
            self.stats['textures'] += 1
            self.stats['textureMB'] += os.path.getsize(dst) / 1048576.0
        self.copied[src] = rel
        return rel

    def material(self, guid, out_rel, foliage):
        src = self.guids.get(guid)
        if not src or not os.path.isfile(src):
            self.stats['missing'].add(guid)
            return False
        tex, fl, col, kw = parse_mat(src)

        def t(*names):
            for n in names:
                if n in tex:
                    p = self.texture(tex[n])
                    if p:
                        return p
            return ''

        def f(default, *names):
            for n in names:
                if n in fl:
                    return fl[n]
            return default
        base = t('_BaseColorMap', '_BaseMap', '_MainTex', '_Albedo', '_BaseColor_Map')
        normal = t('_NormalMap', '_BumpMap', '_Normal', 'Normal_vegetation')
        mask = t('_MaskMap', 'mask_vegetation')
        color = col.get('_BaseColor') or col.get('_Color') or [1, 1, 1, 1]
        clip = f(0, '_AlphaCutoffEnable', '_AlphaClip') > 0.5 or '_ALPHATEST_ON' in kw or foliage
        smooth = f(0.4, '_SmoothnessRemapMax', '_Smoothness', '_Glossiness') if mask else f(0.3, '_Smoothness', '_Glossiness')
        if foliage:
            smooth = min(smooth, 0.35)   # 잎 · 빌보드: 마스크 알파가 커도 거울처럼 번들거리지 않게
        doc = {
            'Shader': 'Universal Render Pipeline/Lit',
            'ResourcePath': out_rel,
            'BaseMapPath': base,
            'NormalMapPath': normal,
            'MetallicMapPath': mask,
            'OcclusionMapPath': mask,
            'EmissionMapPath': '',
            'BaseColor': [round(c, 6) for c in color],
            'Metallic': 0.0,   # 식생 · 바위: HDRP 마스크의 R 은 거의 0 (금속 아님)
            'Smoothness': round(min(max(smooth, 0.0), 1.0), 4),
            'SmoothnessSource': 0,
            'NormalScale': round(f(1.0, '_NormalScale', '_BumpScale'), 4),
            'OcclusionStrength': 1.0,
            'Tiling': [1, 1], 'Offset': [0, 0], 'WorldSpaceUV': 0,
            'AlphaClipping': 1 if clip else 0,
            'Cutoff': round(f(0.5, '_AlphaCutoff', '_Cutoff'), 4),
            'ReceiveShadows': 1, 'SpecularHighlights': 1, 'EnvironmentReflections': 1,
            'Emission': False, 'EmissionColor': [0, 0, 0], 'EmissionIntensity': 1.0,
            'Priority': 0, 'UseShadowMap': 1,
        }
        dst = self.abs(out_rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w', encoding='utf-8') as fh:
            json.dump(doc, fh, indent=4, ensure_ascii=False)
        self.stats['materials'] += 1
        return True

    def lod_heights(self, fbx_src):
        """같은 이름 프리팹의 LOD Group screenRelativeHeight (없으면 [])"""
        stem = os.path.splitext(os.path.basename(fbx_src))[0]
        for dp, _, files in os.walk(os.path.dirname(os.path.dirname(fbx_src))):
            for fn in files:
                if fn == stem + '.prefab':
                    text = open(os.path.join(dp, fn), encoding='utf-8', errors='replace').read()
                    return [float(x) for x in re.findall(r'screenRelativeHeight: ([\d.eE+-]+)', text)]
        return []

    def run(self):
        catalog = []
        for dp, dirs, files in os.walk(self.a.src):
            rel_dir = os.path.relpath(dp, self.a.src).replace('\\', '/')
            if rel_dir != '.' and SKIP_DIRS.search(rel_dir):
                dirs[:] = []
                continue
            for fn in sorted(files):
                if not fn.lower().endswith('.fbx'):
                    continue
                src = os.path.join(dp, fn)
                rel = (rel_dir + '/' + fn) if rel_dir != '.' else fn
                kind = kind_of(rel)
                pack = rel.split('/')[0]
                pack_short = re.sub(r'[^A-Za-z0-9]+', '', pack)
                stem = os.path.splitext(fn)[0]
                out_rel = '/'.join([self.a.dest, kind, pack_short, fn])
                dst = self.abs(out_rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src):
                    shutil.copyfile(src, dst)
                self.stats['fbx'] += 1
                foliage = kind in ('Tree', 'Bush', 'Fern', 'Grass', 'Cactus')
                mats = []
                for name, guid in fbx_materials(src + '.meta'):
                    mrel = '/'.join([self.a.dest, kind, pack_short, stem + '_FBX.Materials', safe(name) + '.mat'])
                    leafy = foliage and not re.search(r'bark|trunk|wood|stem', name, re.I)
                    if self.material(guid, mrel, leafy):
                        mats.append(mrel)
                heights = self.lod_heights(src)
                if heights:
                    # NOVA PCG 가 읽는 LOD 전환 화면 높이 (Unity LOD Group 의 screenRelativeHeight)
                    with open(dst + '.lod.json', 'w', encoding='utf-8') as fh:
                        json.dump(heights, fh)
                catalog.append({'file': out_rel, 'kind': kind, 'pack': pack, 'name': stem, 'materials': mats,
                                'lodHeights': heights, 'source': rel})
        # 지형 레이어 (.terrainlayer — 그림 · 노멀 · 타일 크기) → NOVA .terrainlayer
        layers = []
        for dp, dirs, files in os.walk(self.a.src):
            for fn in sorted(files):
                if not fn.endswith('.terrainlayer'):
                    continue
                text = open(os.path.join(dp, fn), encoding='utf-8', errors='replace').read()
                def tex_guid(key):
                    m = re.search(key + r': \{fileID: \d+, guid: ([0-9a-f]+)', text)
                    return m.group(1) if m else ''
                ts = re.search(r'm_TileSize: \{x: ([\d.]+), y: ([\d.]+)\}', text)
                diffuse = self.texture(tex_guid('m_DiffuseTexture')) if tex_guid('m_DiffuseTexture') else ''
                if not diffuse:
                    continue
                doc = {'diffuse': diffuse.replace('/', chr(92)), 'tileSize': [float(ts.group(1)), float(ts.group(2))] if ts else [2.0, 2.0],
                       'tileOffset': [0.0, 0.0], 'tint': [1.0, 1.0, 1.0, 1.0]}
                normal = self.texture(tex_guid('m_NormalMapTexture')) if tex_guid('m_NormalMapTexture') else ''
                if normal:
                    doc['normalMap'] = normal.replace('/', chr(92))
                    doc['normalScale'] = 1.0
                lrel = '/'.join([self.a.dest, 'TerrainLayers', os.path.splitext(fn)[0] + '.terrainlayer'])
                os.makedirs(os.path.dirname(self.abs(lrel)), exist_ok=True)
                with open(self.abs(lrel), 'w', encoding='utf-8') as fh:
                    json.dump(doc, fh, indent=4)
                layers.append(lrel)
        print('terrain layers', len(layers))
        cat_path = self.abs(self.a.dest + '/catalog.json')
        with open(cat_path, 'w', encoding='utf-8') as fh:
            json.dump(catalog, fh, indent=1, ensure_ascii=False)
        kinds = {}
        for c in catalog:
            kinds[c['kind']] = kinds.get(c['kind'], 0) + 1
        print('fbx %d, materials %d, textures %d (%.0f MB), missing materials %d' % (
            self.stats['fbx'], self.stats['materials'], self.stats['textures'], self.stats['textureMB'], len(self.stats['missing'])))
        print('kinds', json.dumps(kinds, ensure_ascii=False))
        print('catalog', cat_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--dest', default='Assets/Environment/SeedMesh')
    ap.add_argument('--max-texture', type=int, default=1024)
    Importer(ap.parse_args()).run()


if __name__ == '__main__':
    main()
