# Unity 에셋 가져오기 (캐릭터 · 애니메이션)

Unity 프로젝트의 캐릭터 패키지 (모듈형 프리팹 · HDRP / URP / Standard 재질) 와 애니메이션 FBX 를 NOVA 로 옮기는 도구입니다. 원본 Unity 프로젝트는 읽기만 합니다 (복사해서 씁니다).

도구: `Tools/unity_import/`

| 파일 | 하는 일 |
|---|---|
| `NovaExport.cs` | Unity 편집기 스크립트 (batchmode) — 프리팹을 풀어 부위 · 켜짐 · 메시 · 재질 · Unity 가 구운 바인드 자세 상자를 JSON 으로 |
| `unity_character_import.py` | 그 JSON → NOVA 재질 (.mat) · 변형 프리팹 |
| `unity_anim_package.py` | 애니메이션 FBX 를 종류별로 정리한 **애니메이션 라이브러리 패키지** (저장소 밖) · 프로젝트에 골라 넣기 |

## 캐릭터

예: TheTalesFactory Medieval Women (부위 120 여 개를 켜고 끈 변형 프리팹 27 개, HDRP 재질).

1. **Unity 가 프리팹을 푼다** — 임시 Unity 프로젝트에 FBX · 프리팹 · .mat 와 `NovaExport.cs` (Assets/Editor) 를 넣고
   ```
   Unity.exe -batchmode -nographics -projectPath <임시> -executeMethod NovaExport.Run -quit
   ```
   → `<임시>/Export/<프리팹>.json` (부위 경로 · 켜짐 — 루트를 뺀 activeSelf 사슬 · 메시 · 재질 · `bakedMin/Max` = SkinnedMeshRenderer.BakeMesh 의 상자), `_materials.json`
2. **NOVA 에서 FBX 를 한 번 놓아 모든 부위 프리팹을 저장** — `nova modelfile place <fbx>` → `nova prefab save` (템플릿). 모델 정보 `nova modelfile info <fbx> --json > info.json` (스킨 메시마다 서브셋의 파일 재질 번호 · 메시 바인드 후보별 상자)
3. **변환**
   ```
   python -I Tools/unity_import/unity_character_import.py --export <임시>/Export --unity-root <Unity 패키지 폴더> --unity-prefix Assets/<패키지>
       --project <NOVA 프로젝트> --dest Assets/Characters/<이름> --template Assets/Characters/<이름>/_AllParts.prefab
       --model-info info.json [--prefix <이름>_] [--lod0-only] [--controller <.controller>] [--max-texture 1024]
   ```

재질 대응 (NOVA = URP Lit):

| Unity | NOVA |
|---|---|
| `_BaseColorMap` (HDRP) · `_BaseMap` (URP) · `_MainTex` | Base Map |
| `_BaseColor` · `_Color` | Base Color |
| `_NormalMap` · `_BumpMap` (`_NormalScale` · `_BumpScale`) | Normal Map |
| HDRP Mask Map (R 금속성 · G AO · A 매끄러움) | Metallic Map (R · A) + Occlusion Map (G) |
| `_AlphaCutoffEnable` · `_AlphaClip` · `_ALPHATEST_ON` | Alpha Clipping (Cutoff = `_AlphaCutoff` · `_Cutoff`) |

- 재질 칸 = FBX 의 파일 재질 번호 (서브셋의 `MaterialIndex`) — Unity 의 재질 슬롯 순서와 다를 수 있어 모델 정보로 맞춘다
- 텍스처는 쓰는 것만, 가장 긴 변 `--max-texture` 로 줄여 PNG 로
- **메시 바인드**: 부위마다 FBX 의 메시 노드 변환이 다르게 들어 있다 (Unreal · Unity 내보내기). NOVA 는 후보 20 개 (기본 5 × 뒤 회전 4) 중 Unity 가 구운 상자 (`bakedMin/Max`) 와 가장 가까운 것을 골라 렌더러에 `bindMode` 로 고정한다. 0.25 m 넘게 다르면 알린다

## 애니메이션 라이브러리 (종류별 정리 · 별도 패키지)

Unity 의 애니메이션 폴더 (예: 팩 14 개, FBX 4760 개, 7.8 GB) 를 **종류별로** 나누고 이름을 정리해 저장소 밖의 패키지 폴더로 만듭니다. 에셋 스토어 라이선스라 저장소 · 엔진 Resources 에는 넣지 않습니다. 쓸 클립만 프로젝트에 골라 넣습니다.

```
python -I Tools/unity_import/unity_anim_package.py scan    --src <Unity Assets/01_Animation> [--report out.txt]
python -I Tools/unity_import/unity_anim_package.py build   --src <...> --dest E:/NovaAssets/AnimationLibrary
python -I Tools/unity_import/unity_anim_package.py install --package E:/NovaAssets/AnimationLibrary --project <NOVA 프로젝트> --pick <범주,범주 | 고른 목록.json>
```

패키지 구조:

```
AnimationLibrary/
  package.json        이름 · 버전 · 범주별 클립 수
  catalog.json        클립마다: 파일 · 범주 · 팩 · 제자리 (inPlace) · Unity 클립 정보 (이름 · 프레임 · 반복 · 루트 고정) · 원본 경로
  Animations/
    Locomotion/Walk · Run · Sprint · Turn · Crouch · Jump
    Idle/  Dodge/  Death/  Reaction/Hit · Knockdown
    Combat/<무기>/Attack · Block · Equip · Other     (SwordShield · Longsword · Greatsword · Katana · Bow · SpearShield · Staff · Unarmed)
    Traversal/Climb · Vault   Mounted/   Boat/   Swim/   Social/   Interaction/
    Reference/Models · Poses   (애니메이션이 아닌 데모 캐릭터 · 소품 · T 포즈)
```

- 이름: `<팩>_<원래 이름>` (원래 이름이 이미 팩 이름으로 시작하면 한 번만). 예 `Locomotion/Walk/Movement_St_Walk_F_IPC.fbx`
- 범주: 파일 · 클립 이름의 낱말로 먼저 (예 attack · block · hit · death · walk), 안 맞으면 폴더 경로로. 무기는 경로 · 이름의 무기 낱말, 없으면 팩의 기본 무기
- 클립 정보는 FBX 옆 Unity `.meta` 의 `clipAnimations` (Unity 에서 설정한 이름 · 구간 · Loop Time · 루트 고정)
- `install --pick` 은 범주 (예 `Idle,Locomotion/Walk,Reaction/Hit`) 또는 `[{"file": "Animations/...", "as": "Walk"}]` 목록 — 프로젝트의 `Assets/Animations/<범주>/` 로 복사
- 다른 스켈레톤의 클립은 Animator (Avatar = Auto) 가 Humanoid 리타깃으로 옮긴다: 본 이름이 반도 안 맞을 때, 그리고 **이름은 맞아도 바인드 자세가 다를 때** (사람 본의 바인드 로컬 회전 · Hips 전역 회전이 15 도 넘게 — 예: Unreal 마네킹 클립 → TheTalesFactory 캐릭터, 둘 다 pelvis · spine_01 … 이름). 예전에는 이름만 맞으면 로컬 회전을 그대로 옮겨 캐릭터가 누웠다. 같은 리그 (바인드가 같다) 는 이름 그대로 (Generic)
