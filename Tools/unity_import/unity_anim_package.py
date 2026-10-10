"""Unity 애니메이션 폴더 → NOVA 애니메이션 에셋 패키지 (종류별로 정리).

Unity 프로젝트의 애니메이션 FBX 를 훑어 종류 (이동 · 전투 무기별 · 피격 · 죽음 · 회피 · 이동기 · 탈것 …) 로 나누고,
깔끔한 이름으로 복사한 패키지 폴더와 목록 (catalog.json) 을 만든다. 원본 Unity 프로젝트는 읽기만 한다.

  python unity_anim_package.py scan    --src <Unity Assets/01_Animation> [--report out.txt]
  python unity_anim_package.py build   --src <...> --dest <패키지 폴더> [--name 이름]
  python unity_anim_package.py install --package <패키지 폴더> --project <NOVA 프로젝트> --pick <목록.json 또는 범주,범주>

clip 정보 (이름 · 프레임 · 반복 · 루트 고정) 는 FBX 옆 .meta 의 clipAnimations 에서 읽는다 (Unity 가 저장한 값).
패키지는 저장소 밖에 둔다 (에셋 스토어 라이선스 — 다시 배포하지 않는다).
"""
import argparse
import json
import os
import re
import shutil
import sys
from collections import Counter, defaultdict

# 원본 팩 폴더 → (짧은 이름, 기본 무기 · 종류)
PACKS = {
    'BoStaff_AnimSet_forUnity': ('BoStaff', 'Staff'),
    'Dodge_and_Evade_Anims': ('DodgeEvade', None),
    'Full_Mount_Attacks': ('MountAttack', None),
    'GhostSamurai_Animset': ('GhostSamurai', 'Katana'),
    'Grruzam Powerful Sword Animation(Great Sword, Katana)': ('Grruzam', 'Greatsword'),
    'Hand_to_Hand_Set': ('HandToHand', 'Unarmed'),
    'Insane_Spear_Shield_Anim_Set': ('SpearShield', 'SpearShield'),
    'LongswordAnimsetPro': ('Longsword', 'Longsword'),
    'MoCapCentral': ('Social', None),
    'Movement_MocapAnimPack_2.0': ('Movement', None),
    'Small_Boat_Anim_Set': ('Boat', None),
    'SwordShield_MocapAnimPack': ('SwordShield', 'SwordShield'),
    'Universal_Traversal_Anims': ('Traversal', None),
    'YummyGames': ('Swim', None),
}
# 팩 전체가 한 종류 (규칙보다 먼저)
PACK_CATEGORY = {'YummyGames': ('Swim', None), 'Small_Boat_Anim_Set': ('Boat', None), 'Full_Mount_Attacks': ('Mounted', None)}
# 애니메이션이 아닌 FBX (데모 캐릭터 · 소품 · 기준 스켈레톤) — Reference 로 따로
REFERENCE = r'/models?/|/props?/|skeleton_reference|skeletalmesh|skelmesh|_mesh\.fbx$|/demo/character/'

# 종류 판정: (범주, 하위, 정규식) — 앞에서부터 처음 맞는 것. 경로 + 클립 이름 (소문자) 에 건다
RULES = [
    ('Mounted', None, r'mount|horse|ride|rider|saddle'),
    ('Boat', None, r'boat|row(ing)?_|paddle|canoe'),
    ('Death', None, r'death|dead|die(_|\b)|dying'),
    ('Reaction', 'Knockdown', r'knock ?down|knockback|knock_back|getup|get_up|get up|stand ?up|fall(en)?_?down'),
    ('Reaction', 'Hit', r'(^|[^a-z])hit(s|_|\b)|hurt|damage|stagger|stun|flinch|impact'),
    ('Dodge', None, r'dodge|evade|roll|sidestep|side_step|backstep|back_step|dash'),
    ('Traversal', 'Climb', r'climb|ladder|ledge|hang|shimmy|wall_?run'),
    ('Traversal', 'Vault', r'vault|mantle|hurdle|slide|parkour|traversal|step_?up|step_?down'),
    ('Locomotion', 'Jump', r'jump|fall|land(ing)?\b|airborne|in_?air'),
    ('Combat', 'Block', r'block|guard|parry|defen[cs]e|deflect|shield_?up'),
    ('Combat', 'Equip', r'equip|unequip|draw|sheath|holster|unsheath|withdraw|take_?(up|down)|hide_?(up|down)'),
    ('Combat', 'Attack', r'attack|atk|combo|slash|strike|swing|stab|thrust|punch|kick|cut|heavy|light_?att|skill|special|charge|spin|shoot|aim|sting'),
    ('Locomotion', 'Turn', r'turn'),
    ('Locomotion', 'Crouch', r'crouch|(^|[_/])cr_'),
    ('Locomotion', 'Sprint', r'sprint'),
    ('Locomotion', 'Run', r'run|jog'),
    ('Locomotion', 'Walk', r'walk|strafe|step'),
    ('Idle', None, r'idle|stand|breath|wait|pose'),
    ('Interaction', None, r'pick_?up|grenade|throw|open|door|cabinet|push|pull|trade|interact|window'),
    ('Social', None, r'greet|talk|listen|roshambo|wave|cheer|taunt|salute|emote|sit|lie|sleep|drink|eat|look|face'),
]
# 경로 · 이름에 무기가 있으면 팩 기본 무기 대신 (GhostSamurai 는 Bow · GreatSword · Katana … 를 함께 담았다)
WEAPONS = [('Bow', r'(^|[/_ ])bow([/_ ]|$)'), ('Greatsword', r'great_?sword|greatsword'), ('Katana', r'katana'), ('SpearShield', r'spear'),
           ('Staff', r'staff'), ('Longsword', r'longsword|long_sword'), ('DualBlades', r'dual'), ('Unarmed', r'unarmed|fist|hand_?to_?hand'),
           ('SwordShield', r'sword_?shield|sword_and_shield')]


def read_meta_clips(meta_path):
    """Unity .meta 의 clipAnimations: [{name, firstFrame, lastFrame, loopTime, keepOriginalPositionXZ …}]"""
    clips = []
    try:
        with open(meta_path, encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError:
        return clips
    m = re.search(r'\n    clipAnimations:\n(.*?)\n    \S', text, re.S)
    if not m:
        return clips
    block = m.group(1)
    for part in re.split(r'\n    - serializedVersion: \d+\n', '\n' + block):
        name = re.search(r'\n      name: (.*)', '\n' + part)
        if not name:
            continue
        def num(key, default=0.0):
            v = re.search(r'\n      ' + key + r': ([-\d.e]+)', '\n' + part)
            return float(v.group(1)) if v else default
        clips.append({
            'name': name.group(1).strip(),
            'firstFrame': num('firstFrame'),
            'lastFrame': num('lastFrame'),
            'loop': num('loopTime') != 0,
            'lockRootXZ': num('keepOriginalPositionXZ') == 0 and num('loopBlendPositionXZ') != 0,
            'lockRootY': num('loopBlendPositionY') != 0,
            'lockRootRotation': num('loopBlendOrientation') != 0,
        })
    # 메타의 애니메이션 종류 (Humanoid = 3, Generic = 2)
    t = re.search(r'\n    animationType: (\d+)', text)
    for c in clips:
        c['animationType'] = int(t.group(1)) if t else 0
    return clips


def clean(name):
    name = re.sub(r'[()\[\]{}]', '', name)
    name = re.sub(r'[^A-Za-z0-9]+', '_', name).strip('_')
    return re.sub(r'_+', '_', name)


def classify(pack_key, rel, clip_name):
    text = (rel + '/' + clip_name).lower().replace('\\', '/')
    if re.search(REFERENCE, '/' + rel.lower().replace('\\', '/')):
        return 'Reference', 'Models'
    if re.search(r't_?pose', text):
        return 'Reference', 'Poses'
    if pack_key in PACK_CATEGORY:
        return PACK_CATEGORY[pack_key]
    base = PACKS.get(pack_key, (clean(pack_key), None))
    # 파일 · 클립 이름으로 먼저, 안 맞으면 폴더 경로로 (폴더 "Stand_Style" 의 stand 가 Idle 로 잡히지 않게)
    name = (os.path.splitext(os.path.basename(rel))[0] + '/' + clip_name).lower()
    for probe in (name, text):
        for cat, sub, rx in RULES:
            if re.search(rx, probe):
                if cat == 'Combat':
                    weapon = base[1] or 'General'
                    for wname, wrx in WEAPONS:
                        if re.search(wrx, text):
                            weapon = wname
                            break
                    return cat, weapon + ('/' + sub if sub else '')
                return cat, sub
    if base[1]:
        return 'Combat', base[1] + '/Other'
    if pack_key == 'MoCapCentral':
        return 'Social', None
    return 'Misc', None


def scan(src):
    entries = []
    for pack in sorted(os.listdir(src)):
        root = os.path.join(src, pack)
        if not os.path.isdir(root):
            continue
        short = PACKS.get(pack, (clean(pack), None))[0]
        for dirpath, _, files in os.walk(root):
            for fn in files:
                if not fn.lower().endswith('.fbx'):
                    continue
                path = os.path.join(dirpath, fn)
                rel = os.path.relpath(path, root)
                clips = read_meta_clips(path + '.meta')
                stem = os.path.splitext(fn)[0]
                first = clips[0]['name'] if clips else stem
                cat, sub = classify(pack, rel, first)
                if len(clips) > 3 and cat != 'Reference':
                    cat, sub = 'Sets', PACKS.get(pack, (clean(pack), None))[0]   # 한 파일에 동작 여러 개 — 목록 (catalog) 에 클립마다
                lower = rel.lower()
                entries.append({
                    'pack': short,
                    'source': os.path.relpath(path, src).replace('\\', '/'),
                    'bytes': os.path.getsize(path),
                    'category': cat,
                    'sub': sub,
                    'inPlace': 'inplace' in lower.replace('_', '').replace(' ', '') or 'in_place' in lower,
                    'clips': clips,
                    'stem': stem,
                })
    return entries


def target_name(e):
    stem = clean(e['stem'])
    # 원본 이름이 이미 팩 이름으로 시작하면 한 번만 (GhostSamurai_GhostSamurai_… → GhostSamurai_…)
    if stem.lower().startswith(e['pack'].lower() + '_') or stem.lower() == e['pack'].lower():
        return stem
    return clean(e['pack'] + '_' + stem)


def target_dir(e):
    parts = [e['category']] + ([p for p in e['sub'].split('/')] if e['sub'] else [])
    return '/'.join(parts)


def cmd_scan(args):
    entries = scan(args.src)
    by = Counter((e['category'], e['sub']) for e in entries)
    lines = ['%d FBX, %.2f GB' % (len(entries), sum(e['bytes'] for e in entries) / 1e9)]
    for (cat, sub), n in sorted(by.items(), key=lambda kv: (kv[0][0], kv[0][1] or '')):
        lines.append('%6d  %s%s' % (n, cat, '/' + sub if sub else ''))
    misc = [e['source'] for e in entries if e['category'] == 'Misc']
    lines.append('-- Misc 예 (%d) --' % len(misc))
    lines += misc[:80]
    text = '\n'.join(lines)
    if args.report:
        with open(args.report, 'w', encoding='utf-8') as f:
            f.write(text + '\n')
            for e in entries:
                f.write('%s/%s\t<- %s\n' % (target_dir(e), target_name(e), e['source']))
    print(text)


def cmd_build(args):
    entries = scan(args.src)
    dest = args.dest
    os.makedirs(dest, exist_ok=True)
    used = defaultdict(int)
    catalog = []
    copied = skipped = 0
    for e in entries:
        d = target_dir(e)
        name = target_name(e)
        key = (d + '/' + name).lower()
        used[key] += 1
        if used[key] > 1:
            name += '_%d' % used[key]
        rel = d + '/' + name + '.fbx'
        out = os.path.join(dest, 'Animations', *rel.split('/'))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        srcp = os.path.join(args.src, *e['source'].split('/'))
        if os.path.exists(out) and os.path.getsize(out) == e['bytes']:
            skipped += 1
        else:
            shutil.copyfile(srcp, out)
            copied += 1
        catalog.append({
            'file': 'Animations/' + rel,
            'category': e['category'],
            'sub': e['sub'],
            'pack': e['pack'],
            'inPlace': e['inPlace'],
            'clips': e['clips'],
            'source': e['source'],
        })
    counts = Counter(c['category'] + ('/' + c['sub'] if c['sub'] else '') for c in catalog)
    pkg = {
        'name': args.name,
        'displayName': 'Animation Library',
        'version': '1.0.0',
        'type': 'assets',
        'description': 'Unity 애니메이션 에셋을 종류별로 정리한 NOVA 애니메이션 묶음 (원본: Unity 에셋 스토어 — 다시 배포하지 않음)',
        'clips': len(catalog),
        'categories': dict(sorted(counts.items())),
    }
    with open(os.path.join(dest, 'package.json'), 'w', encoding='utf-8') as f:
        json.dump(pkg, f, ensure_ascii=False, indent=2)
    with open(os.path.join(dest, 'catalog.json'), 'w', encoding='utf-8') as f:
        json.dump(catalog, f, ensure_ascii=False, indent=1)
    print('package %s: %d clips (copied %d, unchanged %d) -> %s' % (args.name, len(catalog), copied, skipped, dest))


def cmd_install(args):
    with open(os.path.join(args.package, 'catalog.json'), encoding='utf-8') as f:
        catalog = json.load(f)
    if os.path.isfile(args.pick):
        with open(args.pick, encoding='utf-8') as f:
            picks = json.load(f)   # [{"file": "Animations/...", "as": "Walk"}] 또는 ["Animations/..."]
        wanted = [(p, None) if isinstance(p, str) else (p['file'], p.get('as')) for p in picks]
    else:
        prefixes = [p.strip().strip('/') + '/' for p in args.pick.split(',') if p.strip()]
        wanted = [(c['file'], None) for c in catalog if any(('Animations/' + p).lower() in (c['file'].lower() + '/') or c['file'].lower().startswith('animations/' + p.lower()) for p in prefixes)]
    known = {c['file']: c for c in catalog}
    dest_root = os.path.join(args.project, 'Assets', 'Animations')
    n = 0
    for file, alias in wanted:
        if file not in known:
            print('없음:', file, file=sys.stderr)
            continue
        rel = file[len('Animations/'):]
        out = os.path.join(dest_root, *rel.split('/'))
        if alias:
            out = os.path.join(os.path.dirname(out), clean(alias) + '.fbx')
        os.makedirs(os.path.dirname(out), exist_ok=True)
        src = os.path.join(args.package, *file.split('/'))
        if not (os.path.exists(out) and os.path.getsize(out) == os.path.getsize(src)):
            shutil.copyfile(src, out)
        n += 1
    print('installed %d clips -> %s' % (n, dest_root))


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest='cmd', required=True)
    a = sp.add_parser('scan'); a.add_argument('--src', required=True); a.add_argument('--report')
    b = sp.add_parser('build'); b.add_argument('--src', required=True); b.add_argument('--dest', required=True); b.add_argument('--name', default='com.user.animation-library')
    c = sp.add_parser('install'); c.add_argument('--package', required=True); c.add_argument('--project', required=True); c.add_argument('--pick', required=True)
    args = ap.parse_args()
    {'scan': cmd_scan, 'build': cmd_build, 'install': cmd_install}[args.cmd](args)


if __name__ == '__main__':
    main()
