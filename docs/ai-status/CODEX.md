# NOVA Codex 작업 상태

## 현재 작업 Hub 코드 서명 준비 (2026-10-04)

- 사용자 지시: SmartScreen 경고 개선을 위한 서명 작업 진행.
- 담당: `Tools/package_hub.ps1`, 새 `Tools/HubSigning.ps1`, 새 `Tools/tests/hub_signing.ps1`, `Tools/NovaHub/signing/artifact-signing.sample.json`, `docs/NOVA_HUB.md`, 이 상태 문서와 공동 명세의 Hub 인계 문단. 기존 Hub 디자인/엔진/Claude 렌더링과 공용 빌드 출력은 수정하지 않는다.
- 현재 공개 `NovaHubSetup.exe`는 NotSigned이고 사용자/컴퓨터 인증서 저장소에 코드 서명용 개인 키가 없다. 인증서 신원 확인 전 실제 공개 서명/교체는 불가능하다.
- 구현 완료: Windows 인증서 저장소의 지정 인증서 또는 Azure Artifact Signing으로 자체 실행 파일/DLL → 설치 exe 순서로 SHA256/RFC3161 서명. 실패/미서명/타임스탬프 누락/게시자·인증서 불일치는 배포 패키징 실패로 처리한다. 서명 설정 없이 실행하면 빌드/출력 전에 실패하고, 개발용 미서명은 명시 `-AllowUnsigned`와 `ReleaseReady=false`로만 허용한다. 최종 서명 뒤 해시를 만든다.
- 사용자 답변: 현재 개인, 곧 개인사업자 등록 완료 예정. 등록 완료 후 검증된 사업자 명의를 사용하고 이후 인증서/프로필/ExpectedPublisher 설정을 교체할 수 있다. 한국 개인은 Microsoft Public Trust 신청 대상이 아니고 한국 조직은 신청 가능하나 실제 신원 검증이 필요하다. 계정 생성/구매/신원 제출은 하지 않았다.
- 검사 **26/26** (`E:\NovaTest\CodexHubSigning-7b0394ab\results.json`): 실제 현재 설치 exe의 NotSigned 거절/해시 보존, 신원 없는 실제 패키징의 출력 생성 전 거절, 인증서와 공급자 경계 시뮬레이션. 실제 SDK SignTool로 기존 Microsoft .NET 서명 파일의 Authenticode/타임스탬프 검증도 통과했다 (`TestResults/CodexHub/signing-real-verification.json`).
- 개발용 전체 패키징/검사 설치 성공: `E:\NovaTest\CodexHubSigningPackage-ecf8e296`, payload **30/30** 일치, SHA256 sidecar **2/2**, ReleaseReady=false (`TestResults/CodexHub/signing-package-results.json`). 원래 공개 설치 파일의 SHA256을 보존했고 업로드/사이트 재배포하지 않았다.
- 다음 외부 조건: 사업자 등록 → 공인 코드 서명 인증서 또는 Microsoft Public Trust 신원 검증/프로필 승인 → 실제 서명/설치 검증 후 공개 자산 갱신. 공인 인증서가 없으므로 실제 NOVA 서명은 아직 미검증이며 경고 제거 완료로 표시하지 않는다. 소스 미커밋 상태.

## 이전 완료 작업 Nova Hub 배포 분리

- 사용자 정정(2026-10-04): **기존 `Source/Hub/HubApp.cpp` 디자인을 그대로 사용한다.** 별도로 만든 WinForms UI는 폐기한다. 기존 상단 바/사이드바/프로젝트 목록/새 프로젝트 창/설정/CLI 화면은 유지하고 설치 카드 안에 다운로드 기능만 연결한다.
- 담당: `Source/Hub/HubApp.*`, `Source/Hub/HubProject.*`, 새 `Source/Hub/HubEngineInstaller.*`. 착수 시 변경이 없었다. 다운로드 검증 코어는 화면 없는 설치 서비스로 재사용한다. 별도 Release 빌드에서 Hub만 반영하며 Claude의 렌더링/공용 출력은 보존했다.
- 최종 확인에서 기존 CLI 설치가 Hub 자체를 엔진 경로로 기록하는 문제를 발견했다. 변경이 없던 `Source/Hub/CliInstaller.cpp`도 담당하여 독립 Hub의 CLI가 실제 설치된 엔진의 CLI/실행 경로를 쓰도록 보완했다. `NOVA_CLI_INSTALL_DIR`/`NOVA_CLI_NO_PATH`로 사용자 PATH를 변경하지 않고 native 프로젝트 생성/설치한 CLI로 정확한 엔진과 한글 프로젝트 열기까지 통과했다.
- 2026년 10월 4일 사용자 지시: 웹사이트에서는 Hub exe를 내려받고 Hub 안에서 엔진을 설치하는 흐름을 구현한다.
- 담당: 새 `Tools/NovaHub/` (화면 없는 서비스/Core/검사), 새 `Tools/NovaHubSetup/` (설치 exe), 새 `Tools/package_hub.ps1`, 별도 Hub 검사와 `docs/NOVA_HUB.md`. 엔진 CMake/렌더링/공용 출력은 수정하지 않는다.
- 웹사이트 저장소 `E:\GitHub\Nova-Game-Engine-Site`: 다운로드 URL/CTA/시작 안내와 해당 검사를 맡는다. 사이트 작업 폴더는 착수 시 변경이 없었다.
- Claude는 TAA/SMAA/APV 렌더링 작업 중이다. Hub는 **기존 native Hub와 NovaCore.dll**을 사용한다. 설치 서비스는 별도로 빌드했고 기존 Joint 변경을 보존했다.
- **완료/배포**: 공식 사이트에서 NovaHubSetup.exe 직접 다운로드, 기존 디자인 Hub의 버전별 엔진 설치/취소/진행률/프로젝트 연결. v0.1.0에 Hub 설치 exe와 정상 Release 엔진 r1 ZIP을 추가했다. 원래 ZIP은 제거/교체하지 않았고 Hub는 r1을 선택한다.
- 검사: Core 최종 ZIP 포함 **30/30**, native 기존 화면 9개 함수 동일, 설치/업데이트 payload 전체 해시 일치, 공개 다운로드 SHA256 일치, 실제 설치 서비스의 공개 다운로드/취소/중복 보호/제거 시 native 프로젝트 보존, 실제 한글 경로 에디터 시작/CLI 응답. 웹 버튼 **2/2**, analyze/Web Release 빌드/live HTML·JS 검증 통과.
- 화면 캡처는 FrameArrived 시간 초과로 실패했다. 버튼 클릭/팝업 시각 검사는 미확인이다. 구현/제한/배포 규약은 `docs/NOVA_HUB.md`.
- 확정 기반 `83226d8`에 Hub 파일만 반영한 별도 Release: `E:\NovaTest\CodexHubEngine-41cf718a`. 최종 설치 파일 `E:\NovaTest\CodexNativeHubFinal-a91ecb43\NovaHubSetup.exe`. 검증 로그/공개 SHA256 `TestResults/CodexHub/final-public-installer.json`. Hub/웹사이트 소스는 아직 커밋/push하지 않았다.

아래는 앞선 Joint 2D 완료 기록이다.

- 갱신 시각: 2026년 10월 3일 04시 39분 KST
- 단계: **Joint 2D 6종 구현과 독립 검증 완료** — Joint 42/42, 기존 2D 물리 8/8, 씬 44/44. 빌드한 게임 실행도 통과.
- 현재 허용 범위: 사용자가 새 작업 지시 확인과 진행을 직접 지시함. 공동 명세의 Joint 2D 작업에 착수한다.
- 기준 커밋: `673c3b3` (Shader Graph 3단계 포함). 현재 Joint2D 변경은 커밋/push하지 않았다.
- 독립 엔진: `E:\NovaTest\CodexJoints2DEngine-eb17bb47`. Claude의 Decal 소스와 공용 출력은 포함하지 않는다.

## 현재 진행 결과

6종 컴포넌트, Box2D 제약, 즉시 접근 가능한 필수 Rigidbody2D, Inspector 입력/선택 앵커, C# 클래스와 구조체, 단일 끊어짐 콜백/제거, JSON/Undo/Play 복원과 내부 연결 복제를 구현했다. 프리팹의 인스턴스 연결 ID와 기본 오버라이드 판정도 보완했다. 씬 표기 차이로 현재 씬을 삭제 후 재사용하던 캐시 문제와 C#의 오래된 오브젝트 조회도 검사에서 발견하여 수정했다.

C# 네이티브 표의 맨 끝에 8개를 같은 순서로 추가했다: `J2_GetFloat`, `J2_SetFloat`, `J2_GetVec`, `J2_SetVec`, `J2_GetConnected`, `J2_SetConnected`, `J2_Find`, `J2_Remove`. 이번 작업이 끝나고 Decal C# 바인딩을 추가할 때는 이 8개 뒤에 추가하고 네이티브/관리 표와 ScriptCore.dll을 함께 빌드해야 한다.

## Joint 2D 최종 검증과 인계

- 독립 Debug 엔진과 C# SDK 빌드 성공. CMake는 새 cpp를 자동 수집하므로 CMakeLists.txt를 수정하지 않았다.
- `TestResults/CodexJoints2D/joints-results.json`: **42/42**. 여섯 조인트, 필수 Rigidbody2D 즉시 접근, 모터/제한 변경, 몸체 재생성, 힘·토크 콜백의 종류/반작용/1회 호출, 저장/Undo/Play 복원, 복제와 프리팹 인스턴스 연결을 확인했다. 바퀴 두 개의 차량이 8.494 이동하고, 떨어지는 두 몸체의 거리는 2를 유지했다. 감쇠비 1 스프링은 목표 길이를 넘지 않고 수렴했다.
- 빌드한 게임에서도 C# 조인트 물리와 두 끊어짐 콜백이 동작하며 검사 종료 후 종료 코드 0으로 끝났다 (`player-simulation.json`).
- `TestResults/CodexJoints2D/physics-regression2/results.json`: 기존 2D 물리 **8/8**. 첫 회귀 실행은 전역 설치 엔진을 선택했으므로 이번 검증에서 제외하고, 재실행은 `NOVA_ENGINE`으로 독립 실행 파일을 명시했다.
- `TestResults/CodexJoints2D/scene-results.json`: 기존 씬 동작 **44/44**. 불러오기 실패 보존, 외부/한글 경로, Play/Stop, 자동 복구와 지형 복원을 유지한다.
- Inspector 필드의 JSON 반영과 컴포넌트 순서는 검사했지만 UI 버튼 클릭과 앵커 표시의 시각 검사는 수행하지 않았다. Hinge ±30° 제한은 솔버 허용 오차를 고려해 3° 범위로 검사했다 (관측 최대 32.177°).
- Codex의 전용 검사 에디터와 게임 프로세스는 모두 종료했다. 공용 build/Binaries/Intermediate와 Claude의 Decal 소스는 사용·수정하지 않았다.
- 엔진/SDK 담당 소스 14개는 현재 작업 폴더와 검증한 독립 빌드의 SHA256이 모두 일치한다 (`TestResults/CodexJoints2D/verification.json`). Joint 메뉴 6항목과 Claude의 Decal 항목 보존도 확인했다.

상세 사용법과 현재 제한은 `docs/JOINTS_2D.md`. 이제 Codex의 네이티브 표 편집은 끝났으므로 Claude가 필요하면 위 8개 뒤에 Decal 바인딩을 추가할 수 있다. 다음 통합 빌드는 최신 작업 폴더를 새 복사본에 반영하고 **CMake 재구성 → 엔진/SDK 빌드 → Joint/Decal/기존 물리/씬 검사** 순서로 확인한다. 현재 독립 검사 결과는 Decal 통합 검증을 대신하지 않는다.

공용 `AddComponentMenu.cpp`에는 두 작성자의 변경이 섞여 있다. Codex의 Physics 2D 6줄과 Claude의 Decal 1줄을 모두 보존했으며, 커밋할 때 자기 줄만 stage한다. README/AGENT_HANDOFF/NOVA_CLI 및 공용 run_tests는 이번 Joint 작업에서 수정하지 않았다. 사용자에게 이번 작업 완료를 보고하며 커밋/push는 아직 하지 않았다.

아래는 이전 씬 작업의 이력과 이번 Joint 작업 착수 당시의 범위 기록이다. 현재 결과와 인계는 위 내용을 우선한다.

## 이전 완료 작업: 씬 불러오기 보호 (`6f2f642`)

## 이번 수정 파일과 담당 범위

1. `Source/Scene/Scene.cpp`: 파일 전체를 엄격히 파싱하고 JSON 구조 및 복원 오류를 처리한다. 성공 전 임시 씬이 생성된 오브젝트를 소유하며 실패 시 정리한다. 경로와 실패 원인을 Console과 Editor.log에 기록한다.
2. `Source/Scene/SceneManager.cpp`: 새 씬 복원 성공 뒤에 기존 씬을 교체한다. 일반 열기, 같은 씬 변경 버리기, 자동 복구의 실패에서는 현재 씬을 보존한다. 손상된 시작 씬에는 파일을 덮어쓰지 않고 기본 Untitled 씬을 제공한다. 정상 교체에서는 기존 동작처럼 미저장 지형 편집을 버리고 대상 씬의 지형을 파일에서 다시 읽는다.
3. `Source/Scene/GameObject.cpp`: JSON 오브젝트와 배열 형식을 확인하고 자식 복원 전에 소유 트리에 연결하여 부분 생성된 자손까지 씬이 정리할 수 있게 한다.
4. `Source/Editor/CliCommands.cpp`: `scene-open` 처리만 수정했다. 같은 파일을 실제로 다시 읽고 실패를 명령 실패로 반환한다. 성공 전에 선택을 지우지 않는다.
5. `Tools/tests/scene_lifecycle.ps1`: 기존 14개 조건에 손상 파일, 같은 파일 재열기, Play 중 실패, 자동 복구, 시작 씬, 지형 조건을 추가했다. 검사 대상은 모두 전용 복사본과 새 테스트 프로젝트다.
6. `docs/AI_COLLABORATION.md`, 이 상태 문서: 범위와 완료 결과를 공유한다.

GameObject와 scene-open 보완은 이번 작업 시작 시 변경이 없는 것을 확인하고 소스 수정 전에 이 상태 문서에 담당 범위를 기록했다. 두 파일은 Claude의 최신 3단계 목록에 없다. Shader Graph, 재질, 렌더링, C# 바인딩, 빌드 파이프라인, 공용 검사 파일, 공개 헤더와 클래스 배치는 수정하지 않았다.

## 검증 결과

- 독립 엔진 `E:\NovaTest\CodexSceneEngine-04b00c06`의 Debug 전체 빌드 성공. 확정 커밋 `49b2632`에 위 담당 소스 4개만 반영한 빌드다. 기존 라이브러리 PDB 누락 링크 경고가 있었고 빌드 오류는 없었다.
- 현재 작업 폴더의 최신 헤더로 담당 소스 4개를 별도 객체 파일에 컴파일하여 모두 성공했다. 공유 PCH 및 출력 폴더는 사용하지 않았다 (`TestResults/CodexSceneLoad/compile-current/results.json`).
- 전용 엔진/프로젝트 `E:\NovaTest\CodexSceneLoad-cbfc768e`에서 씬 회귀 검사 **44/44 통과** (`TestResults/CodexSceneLoad/results.json`). 테스트 에디터는 종료했으며 남아 있는 담당 테스트 프로세스는 0개다.
- 손상 파일 14종(잘린 JSON, 빈 파일, 뒤의 쓰레기 데이터, 잘못된 루트 형식, 필수 항목 누락, 컴포넌트 및 자식 복원 실패)을 거절하고 내용·경로·수정 상태·선택·Undo·살아 있는 오브젝트 수를 보존했다.
- 같은 씬 다시 열기 실패, Play 중 실패, 손상된 자동 복구 2종의 보존을 확인했다. 정상 재열기와 정상 자동 복구도 통과했다.
- 손상된 시작 씬은 Untitled로 시작하고 원본 파일을 보존했다. 미저장 지형 나무 3개는 실패 시 보존하며 정상 재열기 및 공유 지형을 쓰는 다른 씬으로 전환할 때 저장된 값으로 복원했다.
- 작업 폴더와 검사 빌드의 담당 소스 해시 4개가 일치한다 (`TestResults/CodexSceneLoad/verification.json`).

첫 실행은 로딩 보호 검사까지 통과한 뒤, 실행 중인 Editor.log의 파일 잠금 때문에 검사 스크립트가 중단됐다 (`E:\NovaTest\CodexSceneLoad-46bca82e`). 에디터 종료 뒤 로그를 읽도록 고쳤다. 수정 뒤 40/40 통과했고 지형 확인을 추가한 최종 실행은 44/44 통과했다. 초기 중단 결과를 엔진 실패나 최종 통과 결과로 표시하지 않는다.

## Claude에게 공유할 사항

공용 `build/`, `Binaries/`, `Intermediate/`, `E:\NovaTest\ScriptTest` 에디터는 Claude가 사용 중이다. Codex는 자신의 독립 빌드와 테스트 프로세스만 사용했고 Claude의 상태 파일과 소스는 수정하지 않았다.

다음 통합 빌드에는 담당 소스 4개를 함께 반영한다. 추가된 GameObject 변경은 JSON 복원과 자손 소유권 순서에 한정하고 CLI 변경은 `scene-open` 한 명령에 한정했다. 공개 API/헤더 변경은 없다.

회귀 검사: `powershell -ExecutionPolicy Bypass -File Tools/tests/scene_lifecycle.ps1 -EngineRoot <독립 엔진 루트> -Out <새 결과 폴더>`. 기본 공용 엔진을 사용할 때 Claude가 사용 중이라고 기록했으면 실행을 거절한다. 공용 출력 사용을 끝낸 후 최신 엔진에서 실행할 수 있다.

현재 헤더 컴파일 성공은 확인했지만 최신 Shader Graph 3단계와 합친 실행 검사는 아직 하지 않았다. 44/44 결과는 위 독립 엔진의 실행 결과이며 최신 통합 결과를 대신하지 않는다. 저장 확인 창 버튼을 직접 클릭하는 UI 검사는 자동화하지 않았다.

## 다음 작업

1. 씬 저장 실패 시 기존 파일 보존: 현재 저장은 기존 파일을 truncate한 뒤 쓴다. JSON 문자열을 먼저 만들고 대상과 같은 폴더의 임시 파일에 쓰기와 닫기를 완료한 뒤 원본을 교체한다. 파일 잠금/쓰기 실패를 검사하는 작업이 다음 후보다. 아직 착수하지 않았다.
2. Shader Graph 3단계 완료 후 통합 검증: Claude가 공용 출력을 반환한 다음 최신 엔진으로 씬 회귀 검사와 그래프 검사를 함께 확인한다.

저장 보호의 우선 범위는 `Scene.cpp`의 파일 쓰기, `CliCommands.cpp`의 `scene-save` 처리, 씬 전용 검사다. CLI의 다른 이름 저장은 현재 성공 여부를 알기 전에 씬 경로를 바꾸므로 저장 실패 시 원래 경로를 복원하는 조건도 포함한다. Claude의 그래프 범위와 공용 검사 파일은 계속 분리한다.

완료 조건은 기존 파일 내용과 해시 보존, 현재 씬의 원래 경로와 수정 상태 보존, 실패한 임시 파일 정리, 한글/공백/외부 경로의 정상 저장 유지다. 새 테스트 프로젝트에서 대상 파일 잠금과 쓸 수 없는 경로를 만들어 확인하며, 저장 실패 원인을 Console과 Editor.log에 알린다. 이번 커밋 요청에서는 이 후속 작업 구현을 시작하지 않았다.

## 이전 완료 기록

`7751c5e`는 저장하지 않은 씬의 Play/Stop에서 원래의 빈 경로를 보존하고, 한글·공백을 포함한 외부 절대 경로의 저장/불러오기 및 재시작 복원을 맞춘 커밋이다. 당시 독립 검사 14/14를 통과했고 Claude의 변경을 포함하지 않았다. 이번 44개 검사에 이전 조건도 포함했다.

## 새 작업: Joint 2D

사용자의 이번 진행 지시로 공동 명세의 새 분담을 확인했다. Hinge/Spring/Distance/Wheel/Fixed/Slider Joint 2D의 컴포넌트, Box2D 연결, C# API, 에디터 필드/앵커, 저장/Undo/Play 복원, 끊어짐 콜백, 전용 회귀 검사를 맡는다. 이전 씬 저장 보호 후보는 후순위이며 아직 구현하지 않았다.

담당: 새 `Source/Physics2D/Physics2DJoints.h/.cpp`, `Physics2DManager.cpp`, `ScriptCore/Engine/Physics2D.cs`, `Source/Scripting/ScriptBindings.cpp`, `ScriptCore/Interop/NativeApi.cs` (이번 회차 Codex 전용), `ScriptCore/Interop/Bridge.cs`, `Source/Scripting/CSharpScript.h/.cpp`, 새 `Tools/tests/joints2d.ps1`. Rigidbody2D 자동 부착을 모든 컴포넌트 부착 경로에서 보장하기 위해 `Source/Scene/GameObject.h/.cpp`도 필요한 줄만 보완한다. 공개 기반 컴포넌트의 메모리 배치는 바꾸지 않는다.

공용 파일은 `Source/Editor/AddComponentMenu.cpp`의 Physics 2D 목록만 수정한다. 빌드는 자동 소스 수집을 먼저 확인하여 CMake 수정 필요성을 판단한다. CLI/README/기존 테스트는 필요하면 담당 줄을 명시하고 직전에 다시 읽는다. 렌더링, Shader Graph, Decal, Claude 상태 문서는 수정하지 않는다.

독립 엔진/프로젝트에서 빌드 및 실행 검사를 진행한다. 기존 독립 엔진은 이전 씬 검증용으로 보존하고 최신 확정 커밋에서 새 복사본을 준비한다. Claude의 공용 빌드 및 에디터는 사용하지 않는다. 커밋/push는 이번 진행 요청에서 수행하지 않는다.

## 편집 중인 공용 파일

- 없음 (착수 직전에 해당 파일을 다시 읽고 범위를 갱신한다)
- 공용 파일 편집 종료: `Source/Editor/AddComponentMenu.cpp` Physics 2D 6줄 반영, Claude의 Decal 항목 유지.

추가 담당: `ScriptCore/Engine/Core.cs`의 Joint2D 조회·추가 경로만 (일반 컴포넌트 경로 유지).
추가 담당: `Source/Scene/PrefabUtility.cpp`의 Joint2D 내부 연결 ID 변환만. 기존 프리팹 배치는 source ID를 새 인스턴스 ID로 바꾸지 않으므로 생성·재조립·Apply·오버라이드 비교에 필요한 줄을 보완한다. `Tools/tests/joints2d_probe.cs`는 전용 검사 프로젝트에만 복사하는 검사 스크립트다.
검사에서 씬 재로드/Undo 뒤 C# Instantiate가 오래된 캐시 주소를 사용하여 SEHException을 재현했다. 담당 `ScriptBindings.cpp`의 조회 캐시에 씬 번호와 오브젝트 생존/ID 검사를 추가한다 (Joint2D의 저장/Undo/복제 API 검증에 필요).
추가 담당: `Source/Scene/SceneManager.cpp`의 LoadScene 캐시 동일 포인터 보호. 저장한 경로와 CLI 경로가 다른 표기이면 현재 씬을 캐시에서 꺼낸 뒤 그 씬 자체를 삭제하고 재사용하여 "string too long"와 접근 위반이 발생했다. 조인트 씬 재로드 검사에서 재현했으며, 같은 현재 씬의 다른 경로 표기로 재로드할 때는 먼저 새 씬을 읽도록 보완한다.
