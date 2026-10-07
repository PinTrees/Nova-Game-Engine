# 모델 끌어 놓기 (Project → Hierarchy · Scene 뷰)

Unity 에서 FBX 를 씬에 끌어 놓은 것과 같은 결과를 만듭니다.

## 결과

- **정적 모델** (FBX · GLB · glTF): 파일 이름의 루트 GameObject + 모델의 **노드마다 GameObject** (노드의 로컬 위치 · 회전 · 배율), 메시가 있는 노드에 **Mesh Filter + Mesh Renderer**
  - 재질 칸 = 파일의 재질 (Unity 의 Extract Materials). FBX 는 재질마다 `<파일>_FBX.Materials/<재질 이름>.mat` (Diffuse 색 · 그림 · 발광 · Phong 광택 → Smoothness, 이미 있으면 그대로 — 고친 값을 지킨다).
    FBX 에 묻힌 그림 (Video 의 Content — `*0` 또는 원래 파일 이름으로 가리킴) 은 `<파일>_FBX.Textures/<원래 이름>.png` 로 꺼내 (Unity 의 Extract Textures — 압축 그림은 그대로, 풀린 화소는 TGA) 재질의 Base Map · Normal · Emission 에,
    GLB 는 묻힌 재질 · 그림을 꺼내 (`<파일>.Materials` · `<파일>.Textures`) 붙인다. 프로젝트 `Assets` 밖의 모델 (엔진 Resources) 은 Default-Material
  - 메시 노드와 본이 같은 이름이면 (Blender 의 메시 오브젝트 `Head` · 본 `Head`) 메시 노드를 `<이름>_Mesh` 로 읽는다 — 엔진은 이름으로 노드를 찾는다
  - 노드 하나 (자식 없음) 짜리 모델은 루트에 바로 Mesh Renderer (Unity 와 같음)
  - 파일 단위 (FBX cm → m, Unity 의 Convert Units) 는 최상위 자식의 위치 · 배율에 들어간다 — 루트는 원점 · 배율 1
- **LOD 노드**: 형제 노드 이름이 `이름_LOD0` · `이름_LOD1` … (둘 이상) 이면 그 부모에 **LOD Group** (LOD 0 = 60 %, 다음은 반씩, 마지막 LOD 1 %). 하나뿐인 `_LOD0` (예: 충돌용 `UCX_.._LOD0`) 은 그냥 메시
- **스킨 메시가 있는 모델** (캐릭터): 예전처럼 Skinned Mesh Renderer + Animator

## 놓는 곳

- **Hierarchy** 빈 곳 = 루트 (원점), 행 위 = 그 GameObject 의 자식 (로컬 원점)
- **Scene 뷰** = 마우스 아래 바닥 (y = 0) — 못 맞으면 카메라 앞 10 m
- 놓은 뒤 루트를 고르고, Undo 한 번에 되돌린다

## CLI

| 명령 | 뜻 |
|------|-----|
| `nova modelfile place Assets/x.fbx [--parent P] [--position x,y,z]` | 끌어 놓기와 같은 길 (결과: GameObject 수 · Mesh Renderer 수 · 월드 범위) |
| `nova modelfile info Assets/x.fbx` | 정적 · 스킨 메시, 노드 이름, 단위 배율 |

## 동작 (엔진 안)

- `Source/Editor/ModelPlacement.*`: `MeshFile` (`ResourceManager::LoadMeshFile`) 의 노드 트리 (`Avatas[0]` — 이름 · 부모 · 로컬 행렬 · UnitScale) 를 따라 GameObject 를 만들고, 정적 메시는 이름이 같은 노드에 (`MeshFilter::SetMesh(mesh, 모델 경로, 메시 번호)` — 씬 저장 · 다시 열기 그대로). 정점에는 Scale Factor 만 들어 있어 노드 위치에도 Scale Factor 를 곱하고 단위 (UnitScale / Scale Factor) 는 최상위에. 노드 0 (파일 루트) 의 변환도 최상위 자식에 접는다
- 씬에 넣은 뒤 LOD Group (렌더러를 fileID 로 가리키므로) — `RecalculateBounds`
- Hierarchy (`SceneHierachyEditorWindow::HandleFbxFileDrop`) · Scene 뷰 (`SceneEditorWindow` 의 `FBX_FILE` 끌어 놓기) · CLI 가 같은 함수

## 검사

`Tools/tests/run_tests.ps1 -Only modelplace` — 엔진 Resources 의 SmallBoat · Factory FBX 와 `Tools/tests/make_lod_gltf.py` 가 만든 LOD glTF:

1. 정적 FBX: 루트 + 노드 3 개 (Mesh Renderer 3), 크기 3.5 m (cm → m), 화면에 보임
2. `_LOD0 ~ _LOD3` FBX: LOD 4 개짜리 LOD Group 하나 (`UCX_.._LOD0` 은 그냥 메시)
3. glTF `Crate_LOD0 ~ 2`: 루트에 LOD Group, 가까이 LOD 0 · 멀리 LOD 2
4. Hierarchy 행에 놓기 = 그 아래 원점
5. 저장 → 다시 열기: 모델 · LOD Group 그대로
6. Undo 한 번에 사라짐

## 아직

- FBX 재질 · 그림 가져오기 (Unity 의 Materials 탭), OBJ (로더가 아직 FBX · GLB · glTF · VRM 만)
