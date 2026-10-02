# NOVA Codex 작업 상태

- 갱신 시각: 2026년 10월 3일 03시 32분 KST
- 단계: 씬 수정 완료, 독립 엔진 빌드와 씬 회귀 검사 14개 모두 통과
- 기준 커밋: `49b2632`과 현재 작업 폴더의 변경
- 현재 허용 범위: 겹치지 않는 개발 작업 완료 후 사용자가 커밋과 다음 작업 안내를 지시함

## 이번에 변경한 파일

- `AGENTS.md`
- `CLAUDE.md`
- `docs/AI_COLLABORATION.md`
- `docs/ai-status/CODEX.md`

앞선 단계에서는 위 협업 문서만 작성했다. 이번 단계에서는 아래 담당 파일만 수정한다. Claude의 소스와 공용 검사 파일은 수정하지 않는다.

## 현재 담당 파일

1. `Source/Scene/SceneManager.cpp`에서 저장하지 않은 씬의 Play/Stop 경로 복원을 수정한다.
2. `Source/Scene/Scene.cpp`에서 절대 경로 씬을 읽고 쓰는 처리를 일치시킨다.
3. 별도 `Tools/tests/scene_lifecycle.ps1`로 씬 회귀 검사를 작성한다. 공용 검사 파일은 수정하지 않는다.

Claude가 자신의 상태 문서에서 위 파일의 분담에 동의했고 해당 파일을 수정하지 않겠다고 확인했다. Shader Graph, 렌더링, 재질, CLI, 빌드 파이프라인은 Codex 담당 범위에서 제외한다. 공개 헤더와 클래스 배치는 바꾸지 않는다.

## 검증과 다음 작업

`Scene.cpp`와 `SceneManager.cpp`를 공용 PCH와 출력 폴더를 쓰지 않고 각각 Debug 옵션으로 컴파일했다. 두 파일 모두 성공했다 (`TestResults/CodexScene/compile/results.json`). 공유 헤더의 기존 문자 인코딩 경고는 있었고 엔진 소스의 컴파일 오류는 없었다. 검사 스크립트의 PowerShell 구문과 경로 정규화 함수도 확인했다.

첫 검사에는 공용 실행 파일의 복사본을 썼고, 두 결함을 실제로 재현했다 (`E:\NovaTest\CodexSceneRuntime-b2850f5f/results.json`). 실제 `Binaries/NovaCore.dll`과 두 씬 객체 파일의 수정 시각은 Codex 소스 수정 전이라 이 실행 파일에는 수정이 반영되지 않았다. Claude 상태의 포함 기록과는 별도로 실제 결과를 기준으로 판단한다.

확정 커밋 `49b2632`에 두 씬 파일만 반영한 독립 엔진 `E:\NovaTest\CodexSceneEngine-04b00c06`의 Debug 전체 빌드가 성공했다 (`TestResults/CodexScene/build-result.json`). 이 빌드를 복사한 전용 엔진과 프로젝트에서 씬 회귀 조건 **14/14 통과** (`TestResults/CodexScene/results.json`). 현재 작업 폴더의 두 소스 해시가 검증 빌드의 소스 해시와 일치한다 (`TestResults/CodexScene/verification.json`).

검사 내용은 Untitled와 저장된 씬의 Play/Stop 복원, 실행 중 다른 씬으로 전환, 새 저장 경로 요구, 대상 씬의 파일 해시 보존, 한글과 공백이 있는 상대 경로, 외부 절대 경로의 읽기와 쓰기, 없는 파일 거절, 재시작 후 외부 씬 복원이다. 저장 대화상자의 취소 클릭은 자동화하지 않았다.

테스트 프로젝트는 `E:\NovaTest\CodexSceneFixed-c9daa610/Project`이며 테스트 에디터는 정상 종료했다. 원본 `build/`, `Binaries/`, 레이아웃, 사용자 씬은 변경하지 않았다. 임시 소스 아카이브는 추출 후 삭제했다. 독립 빌드와 결과는 후속 검증을 위해 보존한다.

Claude가 3단계 작업 중이므로 최신 Shader Graph 3단계와의 합친 빌드 및 실행은 아직 검사하지 않았다. 이 결과는 `49b2632`에 씬 수정만 반영한 엔진의 검증이다. 사용자의 이번 요청에 따라 씬 소스 두 개, 새 검사 스크립트와 Codex가 작성한 협업 문서만 로컬 커밋으로 기록한다. Claude의 소스 및 `docs/ai-status/CLAUDE.md`는 포함하지 않고 push도 하지 않는다.

## Claude와 공유할 사항

씬 소스 수정과 검증 완료. `SceneManager.cpp`는 빈 원래 경로를 보존하고 시작 시 절대 경로를 확인한다. `Scene.cpp`는 읽기와 쓰기의 경로 해석을 통일하고 UTF-16 파일 경로로 읽는다. 공개 API나 헤더는 바꾸지 않았다.

Claude가 3단계의 다음 통합 빌드를 할 때 두 씬 파일을 함께 컴파일해도 된다. 씬 회귀 검사는 `powershell -ExecutionPolicy Bypass -File Tools/tests/scene_lifecycle.ps1`로 실행할 수 있다. 다만 공용 출력을 Claude가 사용 중이라고 기록한 동안에는 검사 스크립트가 공용 엔진 복사를 거절하므로 먼저 사용 종료를 기록하거나 독립 빌드를 `-EngineRoot`로 지정한다. Codex는 Claude의 상태 문서나 공용 검사 파일을 수정하지 않았다.

## 다음 작업 제안

1. **씬 불러오기 실패 시 현재 작업 보존**: 현재 `SceneManager::LoadScene`은 기존 씬을 먼저 삭제하고 `Scene::Load`를 호출하며 JSON 파싱 예외를 처리하지 않는다. 잘린 파일, 잘못된 JSON, 필수 항목 누락을 검사하여 실패 시 현재 씬과 수정 상태를 유지하고 오류를 알리는 처리를 다음 Codex 작업으로 추천한다.
2. **씬 저장 실패 시 기존 파일 보존**: 현재 씬 저장은 기존 파일을 truncate한 뒤 쓴다. 임시 파일에 쓰고 성공한 뒤 교체하도록 바꿔 쓰기 실패 시 원본을 남기는 방안을 제안한다. 파일 잠금과 쓰기 실패를 전용 테스트 프로젝트에서 검사한다.
3. **Shader Graph 3단계 완료 후 통합 검증**: Claude가 공용 출력을 반환한 다음 최신 엔진으로 씬 회귀 검사와 관련 그래프 검사를 함께 확인한다. 위 독립 엔진의 통과 결과를 통합 결과로 대신 표시하지 않는다.

위 항목은 다음 작업 안내이며 이번 커밋 요청에서 구현을 시작하지 않았다. 씬 안정성 항목은 기존 Codex 파일 범위 안에서 진행할 수 있다.
