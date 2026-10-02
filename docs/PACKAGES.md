# NOVA 패키지 만들기

NOVA 의 패키지는 Unity 패키지와 같은 생각입니다. 모든 게임이 쓰지는 않는 기능을 엔진 본체 밖에 두고, **프로젝트에 넣은 패키지만** 불러오고 게임 빌드에 넣습니다. 프로젝트에는 `Packages/manifest.json` 에 이름·버전만 남습니다.

## 어디에 두나

| 위치 | 뜻 | manifest |
|---|---|---|
| 엔진 `Packages/<이름>/` | **NOVA 레지스트리** — 엔진과 같이 빌드·배포되는 공식 패키지 | `"com.nova.cameras": "1.0.0"` |
| 프로젝트 `Packages/<이름>/` | **embedded** — 그 프로젝트에만 있는 패키지 (항상 포함) | 적지 않음 |
| 아무 폴더 | **local** — Package Manager 의 *Add from disk...* / `nova package add <폴더>` | `"com.x": "file:../../경로"` (프로젝트 Packages 기준 상대 경로) |

## 폴더 구조

```
com.company.feature/
  package.json            이름·버전·설명·의존성·네이티브 DLL·컴포넌트
  Runtime/*.cs            C# API (프로젝트 스크립트와 같은 Assembly-CSharp 로 컴파일 — C# 만 있는 패키지도 됨)
  Source/*.cpp, *.h       C++ 컴포넌트 (있으면) → Plugins/<DLL>.dll
  ThirdParty/<라이브러리>/  (선택) 패키지만 쓰는 외부 라이브러리 — Source/*.cpp, *.c + Include/ 를 PCH 없이 같이 컴파일 (예: AI Navigation 의 Recast · Detour · DetourTileCache · FastLZ)
  Plugins/                빌드 결과 (<DLL>.dll + <DLL>.dll.abi) — 저장소에 넣지 않음
  Resources/              (선택) 게임 빌드에 같이 들어가는 파일
```

## package.json

```json
{
  "name": "com.nova.cameras",
  "displayName": "Cameras",
  "version": "1.0.0",
  "description": "…",
  "category": "Camera",
  "author": { "name": "NOVA" },
  "keywords": [ "camera", "follow" ],
  "dependencies": { "com.nova.other": "1.0.0" },
  "native": [ "NovaCameras.dll" ],
  "components": [
    { "type": "FollowCamera", "display": "Follow Camera", "category": "Rendering", "icon": "camera", "single": true }
  ]
}
```

- `dependencies`: 이 패키지를 넣을 때 먼저 넣는 다른 패키지.
- `native`: `Plugins/` 의 DLL. 엔진 저장소의 `Packages/` 에 `Source/` 가 있으면 CMake 가 자동으로 빌드한다 (DLL 이름 = 첫 항목).
- `components`: Add Component 메뉴에 보일 C++ 컴포넌트 (카테고리·아이콘). 씬에 저장된 타입 이름이 `type`.
- 선택: `date`(출시 날짜 `yyyy-mm-dd`, 없으면 package.json 수정 날짜), `nova`(필요한 엔진 버전 — Package Manager 의 Minimum Editor Version), `documentationUrl`. 폴더에 `CHANGELOG.md` · `LICENSE.md` 가 있으면 Package Manager 의 Changelog · Licenses 링크와 Version History 탭에 나온다.

### Feature (패키지 묶음)

Unity 6 의 Features 처럼 여러 패키지를 한 번에 넣는 묶음. 코드 없이 `"type": "feature"` + `dependencies` 만 둔다. Install = 묶인 패키지를 다 넣음, 묶인 패키지가 모두 프로젝트에 있으면 "설치됨".

```json
{
  "name": "com.nova.feature.3d-characters",
  "displayName": "3D Characters and Animation",
  "version": "1.0.0",
  "type": "feature",
  "dependencies": { "com.nova.animation": "1.0.0", "com.nova.cameras": "1.0.0", "com.nova.starter-assets": "1.0.0" }
}
```

## C++ 컴포넌트

엔진 본체는 `NovaCore.dll` 이고, 패키지 DLL 은 그것을 링크합니다. 엔진 헤더(`pch.h`)를 그대로 쓰고, 엔진이 `NOVA_API` 로 내보낸 클래스(Component, GameObject, Transform, Scene, PhysicsManager, UnityGUI, ImGui …)를 부릅니다.

```cpp
// Source/Spinner.h
#pragma once
#include "Component.h"

class Spinner : public Component
{
public:
	float Speed = 90.0f;   // 도/초
	void Update() override;
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	GENERATE_COMPONENT_BODY(Spinner)
};
REGISTER_PACKAGE_COMPONENT(Spinner)   // 엔진 컴포넌트의 REGISTER_COMPONENT 대신
```

```cpp
// Source/Package.cpp — 꼭 있어야 하는 진입점 (엔진과 같은 버전·구성으로 빌드했는지 확인)
#include "pch.h"
NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }
// 선택: NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad() { … }   NovaPackage_OnUnload()
```

- 저장: `GENERATE_COMPONENT_FUNC_TOJSON / FROMJSON` 으로 `j["type"] = "Spinner"` 와 값들.
- 다른 GameObject 참조는 fileID(`uint64`)로 들고 `UnityGUI::GameObjectField` 로 고르게 하고, `RemapFileIDs` 를 구현한다 (복제·프리팹).
- 패키지를 빼면 씬의 그 컴포넌트는 `MissingComponent` 로 데이터를 지키다가 다시 넣으면 살아난다.
- 엔진의 다른 클래스가 더 필요하면 엔진 쪽 선언에 `NOVA_API` 를 붙인다.

## C# API

```csharp
using System.Runtime.InteropServices;
namespace NovaEngine
{
    [NativeComponent("Spinner")]                 // GetComponent / AddComponent 가 이 네이티브 타입을 찾는다
    public sealed class Spinner : Component
    {
        [DllImport("NovaSpinner")] static extern float Spinner_GetSpeed(ulong go);
        [DllImport("NovaSpinner")] static extern void Spinner_SetSpeed(ulong go, float v);
        internal Spinner() { }
        public float speed { get => Spinner_GetSpeed(nativeId); set => Spinner_SetSpeed(nativeId, value); }
    }
}
```

C++ 쪽은 `NOVA_PACKAGE_EXPORT float Spinner_GetSpeed(uint64 go)` 처럼 내보내고, `ScriptBindings::FindObject(go)` + `GetComponentIncludingPending<Spinner>()` 로 찾는다 (같은 프레임에 AddComponent 한 것까지).

C# 만 있는 패키지는 `Runtime/` 에 MonoBehaviour 를 두면 된다 (예: `com.nova.starter-assets`).

## 엔진 확장 지점 (편집기 · 에셋 · 포즈)

패키지가 엔진 코드를 고치지 않고 편집기에 끼어드는 곳. 예: `com.nova.animation` (Animator 전체가 이 패키지).

| 무엇 | 어떻게 | 예 |
|---|---|---|
| 창 | `NovaPackage_OnLoad` 에서 `EditorGUIManager::GetI()->RegisterWindow(new MyWindow)`, `OnUnload` 에서 `UnregisterWindow` + `delete` (게임 빌드 `Application::IsPlayer()` 면 건너뜀) | Window > Animator |
| 에셋 형식 | `EditorExtensions::RegisterAssetType({Owner, Extension, Icon, CreateMenu, DefaultName, DragPayload, Create, Open, Inspector})` — Project 창 Create 메뉴 · 더블클릭 · Inspector · 끌기. `OnUnload` 에서 `UnregisterOwner(패키지 이름)` | `.controller` |
| Inspector 선택 | `SelectionManager::SetCustomSelection(owner, data, drawFn)` (`SelectionType::CUSTOM`) — 상태·전이처럼 GameObject 가 아닌 것. 빼기 전에 내 owner 의 선택을 지운다 (함수가 DLL 안에 있다) | Animator 상태 · 전이 |
| 포즈 후처리 | 컴포넌트가 `IAnimatorPoseModifier`(`Packages/com.nova.animation/Source/AnimatorIK.h`) 를 상속 → Animator 가 Play 중 같은 GameObject 의 것을 `PoseOrder()` 순으로 부른다. `AnimatorPose` = 스켈레톤 · Humanoid 아바타 · 로컬/모델 공간 행렬 · 모델↔월드 · dt. 도우미 `AnimatorIK::RotateBone / TranslateBone / SolveTwoBone / FromTo / Damp` | Legs Animator(0) → Look Animator(10) |
| 자동으로 넣기 | 씬을 읽다가 모르는 컴포넌트 타입이면 `PackageManager::AddForComponent(type)` 가 레지스트리 `components` 에서 찾아 넣는다 (그 뒤 C# 다시 컴파일) | Animator 가 있는 옛 씬 |

패키지가 쓰는 엔진 함수·클래스는 `NOVA_API` 로 내보내져 있어야 링크된다 (없으면 엔진 쪽에 붙인다).

## ABI (꼭 맞아야 하는 것)

패키지 DLL 은 **같은 엔진 버전 · 같은 구성(Debug/Release) · 같은 컴파일러(MSVC v143, /MD)** 로 빌드해야 합니다. 빌드가 만든 `<DLL>.dll.abi`(예: `nova-0.1-msvc143-release`)를 엔진이 불러오기 전에 비교하고, 다르면 불러오지 않고 Package Manager 에 이유를 보여 줍니다.

## 명령

```
nova package list
nova package add com.nova.cameras
nova package add D:\MyPackages\com.company.feature      (from disk)
nova package remove com.nova.cameras
nova window package-manager
```
