"""Unity 캐릭터 (모듈형 프리팹 · HDRP / URP / Standard 재질) → NOVA 프리팹 · 재질.

순서 (docs/UNITY_IMPORT.md):
 1) Unity 가 프리팹을 푼다: 임시 Unity 프로젝트에 FBX · 프리팹 · .mat 를 복사하고
    Unity.exe -batchmode -nographics -projectPath <임시> -executeMethod NovaExport.Run -quit  (Tools/unity_import/NovaExport.cs)
    → <임시>/Export/<프리팹>.json (부위 경로 · 켜짐 · 메시 · 재질), _materials.json
 2) NOVA 편집기에서 FBX 를 한 번 놓고 모든 부위 프리팹을 저장: nova modelfile place <fbx> → nova prefab save (템플릿)
    + 모델 정보: nova modelfile info <fbx> --json > info.json (스킨 메시마다 서브셋의 파일 재질 번호 — 재질 칸 = 파일 재질 번호)
 3) 이 도구: 재질을 NOVA Lit .mat 로 (쓰는 텍스처만 복사), 변형 프리팹마다 켜진 부위만 남긴 NOVA 프리팹

  python -I unity_character_import.py --export <임시>/Export --unity-root <Unity Assets 의 패키지 폴더> --unity-prefix Assets/TheTalesFactory
      --project E:/NovaTest/TalesTest --dest Assets/Characters/MedievalWomen --template Assets/Characters/MedievalWomen/_AllParts.prefab
      [--prefix MedievalWomen_] [--lod0-only] [--controller <.controller>]

재질 대응 (NOVA = URP Lit):
  Base Map      : _BaseColorMap (HDRP) · _BaseMap (URP) · _MainTex
  Base Color    : _BaseColor · _Color
  Normal Map    : _NormalMap (HDRP) · _BumpMap   (세기 _NormalScale · _BumpScale)
  Mask Map (HDRP: R 금속성 · G AO · A 매끄러움) → Metallic Map (R · A) + Occlusion Map (G) — NOVA 의 Metallic Map 도 R · A, Occlusion 은 G
  _MetallicGlossMap · _OcclusionMap (URP · Standard) 은 그대로
  Alpha Clipping: _AlphaCutoffEnable · _AlphaClip · _ALPHATEST_ON → Cutoff = _AlphaCutoff · _Cutoff
"""
import argparse
import json
import os
import re
import shutil
import sys

LOD_RE = re.compile(r'_LOD(\d+)$')


def load_json(p):
    with open(p, encoding='utf-8-sig') as f:
        return json.load(f)


def guid_map(root):
    """Unity 폴더의 .meta → guid : 파일 경로"""
    out = {}
    for dp, _, files in os.walk(root):
        for f in files:
            if not f.endswith('.meta'):
                continue
            try:
                with open(os.path.join(dp, f), encoding='utf-8', errors='replace') as fh:
                    for line in fh:
                        if line.startswith('guid:'):
                            out[line.split(':', 1)[1].strip()] = os.path.join(dp, f[:-5])
                            break
            except OSError:
                pass
    return out


def parse_mat(path):
    """Unity .mat (YAML) 에서 텍스처 · 수 · 색 · 키워드 (단순 줄 읽기)"""
    tex, floats, colors, keywords = {}, {}, {}, set()
    section, cur = None, None
    with open(path, encoding='utf-8', errors='replace') as f:
        lines = f.read().splitlines()
    for line in lines:
        s = line.strip()
        if s.startswith('m_TexEnvs:'):
            section = 'tex'; continue
        if s.startswith('m_Floats:'):
            section = 'float'; continue
        if s.startswith('m_Colors:'):
            section = 'color'; continue
        if s.startswith('m_Ints:'):
            section = 'float'; continue
        if s.startswith('m_ShaderKeywords:'):
            keywords.update(s.split(':', 1)[1].split())
            section = None; continue
        if s.startswith('m_ValidKeywords:'):
            section = 'kw'; continue
        if s.startswith('m_InvalidKeywords:') or s.startswith('m_LightmapFlags') or s.startswith('m_BuildTextureStacks'):
            section = None; continue
        if section == 'kw':
            if s.startswith('- '):
                keywords.add(s[2:].strip())
            else:
                section = None
            continue
        if section == 'tex':
            m = re.match(r'- (\w+):\s*$', s)
            if m:
                cur = m.group(1); continue
            m = re.match(r'm_Texture: \{fileID: (-?\d+)(?:, guid: ([0-9a-f]+))?', s)
            if m and cur:
                if m.group(1) != '0' and m.group(2):
                    tex[cur] = m.group(2)
                continue
        elif section == 'float':
            m = re.match(r'- (\w+): (-?[\d.eE+-]+)', s)
            if m:
                floats[m.group(1)] = float(m.group(2)); continue
        elif section == 'color':
            m = re.match(r'- (\w+): \{r: ([^,]+), g: ([^,]+), b: ([^,]+), a: ([^}]+)\}', s)
            if m:
                colors[m.group(1)] = [float(m.group(i)) for i in range(2, 6)]; continue
    return tex, floats, colors, keywords


class Converter:
    def __init__(self, a):
        self.a = a
        self.project = os.path.abspath(a.project)
        self.dest = a.dest.replace('/', '\\').strip('\\')
        self.guids = guid_map(a.unity_root)
        # Unity 의 materialSearch (Everywhere): FBX 가 이름으로 찾은 재질 — 임시 프로젝트가 새로 만든 재질 (Model/Materials) 대신
        self.mat_by_name = {}
        for g, p in self.guids.items():
            if p.lower().endswith('.mat'):
                self.mat_by_name.setdefault(os.path.splitext(os.path.basename(p))[0], p)
        self.mat_cache = {}      # unity guid → nova .mat 상대 경로
        self.mat_names = {}      # nova 이름 → guid (겹침)
        self.copied = {}         # 원본 텍스처 → nova 상대 경로
        self.stats = {'materials': 0, 'textures': 0, 'textureMB': 0.0, 'missing': set()}
        # 스킨 메시 이름 → 서브셋마다 재질 칸 (Unity sharedMaterials[i] = 서브셋 i — 순서가 같다)
        self.subsets = {}
        self.bind_bounds = {}   # 메시 이름 → 후보마다 바인드 범위 (엔진이 계산 — SkinnedMesh::BindCandidateBounds)
        if a.model_info:
            info = load_json(a.model_info)
            for s in info.get('skinned', []):
                self.subsets.setdefault(s['name'], s.get('subsetMaterials', []))
                self.bind_bounds.setdefault(s['name'], s.get('bindBounds', []))
        self.stats['bindModes'] = {}
        self.stats['bindFar'] = []

    def bind_mode(self, part):
        """Unity 가 이 프리팹 자세에서 구운 범위와 가장 가까운 메시 바인드 후보 (엔진 bindMode)"""
        cands = self.bind_bounds.get(part['name'])
        if not cands or 'bakedMin' not in part:
            return None
        umin, umax = part['bakedMin'], part['bakedMax']
        uc = [(umin[i] + umax[i]) * 0.5 for i in range(3)]
        us = [umax[i] - umin[i] for i in range(3)]
        best, pick = None, None
        for k, b in enumerate(cands):
            c = [(b[i] + b[i + 3]) * 0.5 for i in range(3)]
            sz = [b[i + 3] - b[i] for i in range(3)]
            score = sum((c[i] - uc[i]) ** 2 for i in range(3)) ** 0.5 + sum(abs(sz[i] - us[i]) for i in range(3)) * 0.5
            if best is None or score < best:
                best, pick = score, k
        self.stats['bindModes'][pick] = self.stats['bindModes'].get(pick, 0) + 1
        if best > 0.25:
            self.stats['bindFar'].append(f"{part['name']} ({best:.2f} m)")
        return pick

    def rel(self, *parts):
        return '\\'.join([self.dest] + [p for p in parts if p])

    def abs(self, rel):
        return os.path.join(self.project, rel)

    def unity_to_source(self, unity_path):
        prefix = self.a.unity_prefix.rstrip('/') + '/'
        if unity_path.startswith(prefix):
            return os.path.join(self.a.unity_root, unity_path[len(prefix):].replace('/', os.sep))
        return None

    def copy_texture(self, guid):
        src = self.guids.get(guid)
        if not src or not os.path.isfile(src):
            return ''
        if src in self.copied:
            return self.copied[src]
        root = os.path.abspath(self.a.unity_root)
        sub = os.path.relpath(os.path.dirname(src), root)
        # 패키지 안 폴더 구조에서 "Textures" 아래만 (짧게)
        parts = sub.split(os.sep)
        if 'Textures' in parts:
            parts = parts[parts.index('Textures') + 1:]
        stem, ext = os.path.splitext(os.path.basename(src))
        limit = self.a.max_texture
        if limit > 0:
            # 군중용: 가장 긴 변을 limit 로 줄여 PNG (4K TGA 원본은 VRAM · 읽기 시간이 크다)
            rel = self.rel('Textures', *parts, stem + '.png')
            dst = self.abs(rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.exists(dst):
                from PIL import Image
                im = Image.open(src)
                im.load()
                if im.mode not in ('RGB', 'RGBA', 'L'):
                    im = im.convert('RGBA' if 'A' in im.getbands() else 'RGB')
                w, h = im.size
                if max(w, h) > limit:
                    sc = limit / float(max(w, h))
                    im = im.resize((max(1, int(w * sc)), max(1, int(h * sc))), Image.LANCZOS)
                im.save(dst, optimize=False)
        else:
            rel = self.rel('Textures', *parts, os.path.basename(src))
            dst = self.abs(rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src):
                shutil.copy2(src, dst)
        self.copied[src] = rel
        self.stats['textures'] += 1
        self.stats['textureMB'] += os.path.getsize(dst) / 1048576.0
        return rel

    def material(self, mat):
        guid = mat.get('guid', '')
        if not guid:
            return None
        if guid in self.mat_cache:
            return self.mat_cache[guid]
        src = self.guids.get(guid) or self.unity_to_source(mat.get('path', ''))
        if not src or not os.path.isfile(src):
            src = self.mat_by_name.get(mat.get('name', ''))
        if not src or not os.path.isfile(src):
            self.mat_cache[guid] = None
            self.stats['missing'].add(mat.get('name', guid))
            return None
        tex, fl, col, kw = parse_mat(src)
        def t(*names):
            for n in names:
                if n in tex:
                    p = self.copy_texture(tex[n])
                    if p:
                        return p
            return ''
        def f(default, *names):
            for n in names:
                if n in fl:
                    return fl[n]
            return default
        base = t('_BaseColorMap', '_BaseMap', '_MainTex')
        normal = t('_NormalMap', '_BumpMap')
        mask = t('_MaskMap')
        metallic_map = mask or t('_MetallicGlossMap')
        occlusion = mask or t('_OcclusionMap')
        color = col.get('_BaseColor') or col.get('_Color') or [1, 1, 1, 1]
        clip = f(0, '_AlphaCutoffEnable', '_AlphaClip') > 0.5 or '_ALPHATEST_ON' in kw
        # 매끄러움: HDRP 마스크는 A × (_SmoothnessRemapMax), 없으면 _Smoothness · _Glossiness
        smooth = f(0.5, '_SmoothnessRemapMax', '_Smoothness', '_Glossiness', '_GlossMapScale') if metallic_map else f(0.5, '_Smoothness', '_Glossiness')
        metal = f(1.0, '_MetallicRemapMax') if mask else f(0.0, '_Metallic')
        if metallic_map and not mask and '_Metallic' in fl:
            metal = 1.0   # Standard 의 Metallic Map 이 있으면 맵 값 그대로
        name = os.path.splitext(os.path.basename(src))[0]
        nova_name = name
        k = 2
        while nova_name in self.mat_names and self.mat_names[nova_name] != guid:
            nova_name = f'{name}_{k}'; k += 1
        self.mat_names[nova_name] = guid
        rel = self.rel('Materials', nova_name + '.mat')
        doc = {
            'Shader': 'Universal Render Pipeline/Lit',
            'ResourcePath': rel,
            'BaseMapPath': base,
            'NormalMapPath': normal,
            'MetallicMapPath': metallic_map,
            'OcclusionMapPath': occlusion,
            'EmissionMapPath': '',
            'BaseColor': [round(c, 6) for c in color],
            'Metallic': round(metal, 4),
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
        dst = self.abs(rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w', encoding='utf-8') as fh:
            json.dump(doc, fh, indent=4, ensure_ascii=False)
        self.mat_cache[guid] = rel
        self.stats['materials'] += 1
        return rel

    def keep(self, part):
        if not part['active']:
            return False
        m = LOD_RE.search(part['name'])
        return (not self.a.lod0_only) or m is None or m.group(1) == '0'

    def convert_prefab(self, export, template):
        root = json.loads(json.dumps(template['root']))
        children = root['children']
        by_name = {}
        for c in children:
            by_name.setdefault(c['name'], []).append(c)
        kept = []
        used = set()
        for part in export['skinned']:
            if not self.keep(part):
                continue
            cands = [c for c in by_name.get(part['name'], []) if id(c) not in used]
            if not cands:
                self.stats['missing'].add('part:' + part['name'])
                continue
            c = cands[0]
            used.add(id(c))
            c['active'] = True
            for comp in c['components']:
                if comp.get('type') == 'SkinnedMeshRenderer':
                    mode = self.bind_mode(part)
                    if mode is not None:
                        comp['bindMode'] = mode
                    paths = list(comp.get('m_MaterialPaths', []))
                    slots = self.subsets.get(part['name'])
                    for i, m in enumerate(part['materials']):
                        p = self.material(m)
                        if not p:
                            continue
                        slot = slots[i] if slots and i < len(slots) else i   # 정보가 없으면 서브메시 순서 그대로
                        while len(paths) <= slot:
                            paths.append('builtin:Default-Material')
                        paths[slot] = p
                    comp['m_MaterialPaths'] = paths
            kept.append(c)
        root['children'] = kept
        # Unity LOD 단계 부위를 뺐다 → LOD Group 도 뺀다 (먼 거리는 엔진 Auto LOD · 임포스터)
        if self.a.lod0_only:
            root['components'] = [c for c in root['components'] if c.get('type') != 'LODGroup']
        if self.a.controller:
            for comp in root['components']:
                if comp.get('type') == 'Animator':
                    comp['controller'] = self.a.controller
        name = export['name']
        if self.a.prefix and name.startswith(self.a.prefix):
            name = name[len(self.a.prefix):]
        name = name.strip()
        root['name'] = name
        out = dict(template)
        out['root'] = root
        rel = self.rel('Prefabs', name + '.prefab')
        dst = self.abs(rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w', encoding='utf-8') as fh:
            json.dump(out, fh, indent=2, ensure_ascii=False)
        return name, len(kept)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--export', required=True)
    ap.add_argument('--unity-root', required=True)
    ap.add_argument('--unity-prefix', default='Assets')
    ap.add_argument('--project', required=True)
    ap.add_argument('--dest', required=True)
    ap.add_argument('--template', required=True)
    ap.add_argument('--prefix', default='')
    ap.add_argument('--only', default='', help='이 이름으로 시작하는 Export JSON 만 (예: MedievalWomen_)')
    ap.add_argument('--skip', default='', help='쉼표 목록: 건너뛸 프리팹 이름')
    ap.add_argument('--lod0-only', action='store_true')
    ap.add_argument('--controller', default='')
    ap.add_argument('--model-info', default='', help='nova modelfile info --json 결과 (서브셋 재질 칸)')
    ap.add_argument('--max-texture', type=int, default=1024, help='텍스처 가장 긴 변 (0 = 원본 복사)')
    a = ap.parse_args()
    conv = Converter(a)
    template = load_json(os.path.join(conv.project, a.template))
    skip = set(s.strip() for s in a.skip.split(',') if s.strip())
    done = []
    for f in sorted(os.listdir(a.export)):
        if not f.endswith('.json') or f.startswith('_'):
            continue
        if a.only and not f.startswith(a.only):
            continue
        export = load_json(os.path.join(a.export, f))
        if export['name'] in skip or not export.get('skinned'):
            continue
        name, parts = conv.convert_prefab(export, template)
        done.append((name, parts))
    for n, p in done:
        print(f'  {n}: {p} parts')
    s = conv.stats
    print(f"prefabs {len(done)}, materials {s['materials']}, textures {s['textures']} ({s['textureMB']:.0f} MB)")
    if s['missing']:
        print('missing:', ', '.join(sorted(s['missing'])[:30]))
    if s['bindModes']:
        print('bind modes:', dict(sorted(s['bindModes'].items())))
    if s['bindFar']:
        print('bind far from Unity:', ', '.join(sorted(set(s['bindFar']))[:20]))


if __name__ == '__main__':
    main()
