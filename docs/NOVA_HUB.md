# NOVA Hub 배포와 엔진 설치

2026-10-04. 웹사이트 → Hub 설치 exe → Hub 안에서 엔진 설치 → 프로젝트 생성/열기 흐름이다.

## 기존 디자인 유지

사용자의 지시대로 `Source/Hub/HubApp.cpp`의 기존 네이티브 ImGui 화면을 사용한다. 로고, Pretendard 글꼴, 상단 바, 사이드바, 프로젝트 검색/목록/즐겨찾기, 새 프로젝트 템플릿 창, 설정과 CLI 화면은 유지한다. 별도로 만들었던 WinForms UI 소스는 제거했다.

기존 설치 탭의 동일한 카드/버튼 스타일에 버전 선택, 다운로드, 진행률, 취소, 설치 폴더 열기와 제거를 붙였다. 새 프로젝트 창에는 설치된 엔진 버전 선택만 추가했다. 프로젝트에 기록된 버전의 `Binaries/NovaEngine.exe`를 실행하며, 해당 버전이 없으면 설치 탭으로 안내한다. 프로젝트를 열 때 버전 기록을 현재 Hub 버전으로 덮어쓰지 않는다.

## 사용자 흐름

1. [공식 사이트](https://nova-game-engine.web.app)에서 **NOVA Hub 다운로드**를 누른다. GitHub 목록 페이지 대신 [NovaHubSetup.exe](https://github.com/PinTrees/Nova-Game-Engine/releases/latest/download/NovaHubSetup.exe)가 직접 내려온다.
2. 설치 exe가 `%LOCALAPPDATA%\NOVA\HubApp`에 기존 Hub 앱/폰트/로고/필요 DLL을 설치하고 바탕화면과 시작 메뉴 바로가기를 만든다. 관리자 권한은 요구하지 않는다.
3. Hub의 **설치** 탭에서 엔진 버전을 선택한다. `%LOCALAPPDATA%\NOVA\Editors\<버전>`에 설치된다.
4. 기존 **프로젝트** 탭에서 새 프로젝트를 만들거나 기존 프로젝트를 추가하여 연다. 목록/즐겨찾기/설정은 기존 `%LOCALAPPDATA%\NOVA\Hub` 형식을 사용한다.

Hub의 설치 서비스는 .NET 런타임을 포함한다. Hub 실행에 별도 .NET 설치는 필요 없다. 엔진에서 C# 스크립트를 개발하려면 .NET SDK 8 이상이 필요하다. Hub 설치 프로그램에는 Windows 앱 목록 등록/Hub 제거 프로그램과 자동 업데이트 기능을 아직 넣지 않았다. 엔진 제거는 Hub 안에서 가능하고 프로젝트 파일은 보존한다.

## Android 빌드 지원 모듈 (2026-10-04, Claude)

Unity 의 "Android Build Support" 모듈처럼, Hub 의 **설치** 탭에서 안드로이드 빌드 도구를 같이 설치한다.

- 엔진 카드의 **"Android 빌드 지원 함께 설치"** 를 체크하면 라이선스 동의 창이 뜨고, 동의하면 엔진 설치가 끝난 뒤 이어서 설치한다.
  엔진 목록 아래의 **Android 빌드 지원** 카드에서 따로 설치할 수도 있다
- 설치 위치: 엔진 폴더(`%LOCALAPPDATA%\NOVA\Editors`) 옆의 `%LOCALAPPDATA%\NOVA\AndroidTools\{jdk,sdk}`. 모든 엔진 버전이 함께 쓴다
- 구성 요소와 출처 (2026-10-04 기준 약 1034 MB):

| 구성 요소 | 출처 · 검증 |
|---|---|
| OpenJDK 17 (Eclipse Temurin) | Adoptium API → `github.com/adoptium/…` zip, 크기 + SHA-256 |
| Platform-Tools · Build-Tools 36.0.0 · Platform android-34 · NDK 28.2.13676358 · CMake 3.22.1 | Google `dl.google.com/android/repository/repository2-3.xml` 의 Windows 묶음, 크기 + SHA-1 |

- **라이선스**: Android SDK 라이선스 원문을 Google 의 공식 목록에서 받아 동의 창에 그대로 보여 준다. 체크 상자 + "동의하고 설치" 를
  사용자가 직접 눌러야 하며, 서비스는 `acceptLicense` 가 없으면 설치를 거절한다. 동의하면 `sdk/licenses/android-sdk-license` 에
  라이선스 원문의 SHA-1 을 쓴다 (sdkmanager 와 같은 형식)
- 설치 방식은 엔진 설치와 같다: 임시 폴더(`.install-<guid>`)에 받고 → 크기 · 해시 확인 → 안전한 압축 해제 (경로 탈출 거절, 맨 위 폴더 하나는 벗김)
  → 구성 요소마다 `.nova-package` 표시 → 최종 폴더로 이동. 이미 있는 구성 요소는 다시 받지 않는다. 실패 · 취소하면 그 구성 요소는 남기지 않는다
- 빌드 쪽: `Android/build.py` 가 `ANDROID_HOME` · `JAVA_HOME` → `NOVA_ANDROID_TOOLS` → 엔진 옆 `AndroidTools` → `%LOCALAPPDATA%\NOVA\AndroidTools`
  → Android Studio 기본 위치 순서로 찾는다
- 서비스 명령: `android-status` · `android-catalog` (라이선스 원문 · 구성 요소 목록) · `android-install` (`acceptLicense`, 선택 `androidRoot`).
  모든 결과의 `android` 항목에 `root · installed · missing · downloadBytes` (목록을 받은 요청이면 `licenseId · license · packages` 도)

| 파일 | 하는 일 |
|---|---|
| `Tools/NovaHub/Core/AndroidToolsInstaller.cs` | 목록 읽기 · 다운로드 · 검증 · 설치 · 상태 |
| `Tools/NovaHub/Service/Program.cs` | `android-*` 명령 |
| `Source/Hub/HubEngineInstaller.*` | 요청의 `acceptLicense`, 결과의 `android` 상태 |
| `Source/Hub/HubApp.*` | 모듈 카드 · "함께 설치" 체크 · 라이선스 동의 창 · 엔진 설치 뒤 이어서 설치 |
| `Tools/NovaHub/Tests/Program.cs` | Android 검사 7 개 (+ `--live` 에서 공개 목록 확인) |

## 파일 분담

- `Source/Hub/HubApp.*`: 기존 UI에 설치 상태 연결, 설치된 버전으로 프로젝트 열기.
- `Source/Hub/HubProject.*`: 선택한 버전 저장/유지, 지정한 엔진 실행, 설치된 독립 Hub로 돌아가기.
- `Source/Hub/CliInstaller.cpp`: 독립 Hub가 아니라 설치된 실제 엔진을 CLI의 실행 대상으로 기록한다. 엔진을 설치하기 전에는 CLI 설치를 비활성화한다.
- `Source/Hub/HubEngineInstaller.*`: 창을 멈추지 않는 설치 서비스 실행, JSON 진행 상태/결과 조회와 취소.
- `Tools/NovaHub/Core/`: 공개 버전 목록, 다운로드/검증/압축 해제/설치 등록과 제거. 프로젝트 데이터 형식도 검사한다.
- `Tools/NovaHub/Service/`: 화면이 없는 `NovaHubService.exe`. 공식 목록에서 버전을 다시 확인한 뒤 Core를 호출한다.
- `Tools/NovaHubSetup/`, `Tools/package_hub.ps1`: 기존 native Hub를 별도 설치 exe로 묶는다.
- 웹사이트 저장소: `E:\GitHub\Nova-Game-Engine-Site`. 다운로드 버튼/시작 안내/FAQ/문서/선택적 소스 빌드 요구사항을 수정했다.

현재 Hub 화면은 기존 `NovaCore.dll` 안에 있다. 설치 묶음에 이 DLL과 Hub 리소스를 포함하고 기존 Launcher를 `NovaHub.exe`로 사용한다. 엔진 실행 파일, 엔진 셰이더/에셋/패키지/스크립팅 SDK는 Hub 묶음에 포함하지 않는다. 설치 서비스가 내려받는 엔진 ZIP은 별도이다. native UI를 더 작은 라이브러리로 분리하는 최적화는 이후 작업이다.

## 배포 규약

- 저장소: `PinTrees/Nova-Game-Engine`. 공개 stable release만 사용한다.
- 태그 `v<x.y.z>`, 엔진 자산 `NOVA-Engine-<x.y.z>-win64.zip`. 수정된 배포는 `NOVA-Engine-<x.y.z>-win64-r1.zip`, `-r2.zip` 형식을 사용할 수 있다. 같은 버전에서는 가장 높은 revision을 선택한다.
- GitHub API의 SHA256 digest와 파일 크기가 있는 공식 HTTPS 자산만 목록에 표시한다. 설치 직전에 공개 목록을 다시 확인한다.
- 다음 최신 stable release에도 **NovaHubSetup.exe**를 반드시 올린다. 웹사이트의 `releases/latest/download` 링크를 유지하기 위한 조건이다. Hub 버전과 엔진 버전은 각각 빌드한다.
- 기존 공개 0.1.0 ZIP은 Debug 실행 파일과 Release Assimp가 섞여 있었다. 원래 자산은 삭제/교체하지 않고 정상 Release인 **NOVA-Engine-0.1.0-win64-r1.zip**을 추가했다. 기반은 확정 커밋 `83226d8`와 이번 Hub 파일이다. Claude의 진행 중 TAA/APV 및 Codex의 미커밋 Joint 변경은 넣지 않았다.

다운로드는 임시 폴더에서 크기/SHA256을 확인하고, 압축 경로 탈출/절대 경로/ADS/중복/링크/Windows 끝 공백 경로를 거절한다. 필수 실행 파일과 `NovaCore.dll`이 모두 있어야 최종 버전 폴더로 이동한다. 실패/취소는 기존 엔진을 덮어쓰지 않는다. 설치 기록 저장이 중단되어도 최종 폴더의 마커로 복구한다. 제거는 Hub 설치 루트와 마커/링크/실행 중 엔진 검사를 통과해야 한다.

## 빌드와 검증

공용 `build/Binaries`를 사용하지 않는다. 별도 Release 빌드에 담당 Hub 파일만 반영하고 CMake를 재구성한 뒤 `NovaEngine`을 빌드한다. 패키징 전 소스 해시와 DLL 빌드 시각을 확인한다.

```powershell
# EngineRoot는 담당 Hub 변경을 반영한 별도 Release 빌드다.
# 개발 검사 전용. 공개 배포는 아래 코드 서명 설정을 사용한다.
./Tools/package_hub.ps1 -EngineRoot E:/NovaTest/<별도엔진> -Out E:/NovaTest/<새Hub출력> -AllowUnsigned
dotnet run --project Tools/NovaHub/Tests -c Release -- E:/NovaTest/<새검사폴더>
# 실제 공개 엔진 다운로드까지 검사
dotnet run --project Tools/NovaHub/Tests -c Release -- E:/NovaTest/<새공개검사폴더> --live
```

`--test-install <새폴더>`는 설치 파일 검사 전용이다. 바로가기/사용자 기본 목록을 변경하지 않는다. native Hub 검사에는 `NOVA_HUB_STATE_DIR`와 `NOVA_HUB_ENGINE_ROOT`로 별도 목록/엔진 경로를 지정한다. 서비스 요청은 `--request <request.json>`이고 결과/진행률/취소 파일은 그 요청의 작업 폴더 안에 둔다.

검증 기록은 `TestResults/CodexHub`에 있다.

- Core **30/30**: 기존 29개 보호 조건 + 최종 실제 Release ZIP 설치.
- 기존 화면 9개 함수가 `83226d8`와 동일함을 비교했다 (`existing-design-results.json`). 새 프로젝트/설치 탭을 제외한 화면 구조와 스타일은 수정하지 않았다.
- native 설치/재설치의 모든 payload 파일 해시 일치, 다른 파일이 있는 폴더 보호.
- native Hub에서 CLI 설치 전 엔진 필요 조건과 실제 엔진 경로 기록을 검사했다. 별도 CLI로 한글 경로의 native Hub 프로젝트를 열고 정확한 엔진 프로세스/프로젝트를 확인했다. 사용자 PATH는 변경하지 않았다 (`native-cli-final-results.json`).
- 실제 공개 `NovaHubSetup.exe` 재다운로드 SHA256 일치.
- 실제 서비스의 공개 목록/수정 ZIP 선택/다운로드/설치, 중복 설치/취소 보호와 엔진 제거 시 native 프로젝트 파일 보존.
- 설치한 최종 Release 엔진이 한글 경로의 프로젝트를 DirectX 11 에디터로 열고 `nova info`에 정상 응답했다. 검사 에디터만 종료했다.
- Hub/엔진 DLL의 Debug CRT 의존성 없음. native Release 및 서비스 빌드 성공.
- 웹사이트 다운로드 버튼 **2/2**, `flutter analyze` 문제 없음, Release 웹 빌드 성공. Firebase preview를 live로 반영했고 공개 HTML/JS가 검사 빌드와 동일함을 확인했다.
- Windows 화면 캡처는 `FrameArrived timed out`으로 실패했다. 기존 native Hub 실행과 목록 조회는 확인했지만 설치 버튼 클릭/새 프로젝트 팝업의 시각 검사는 완료했다고 주장하지 않는다.

CLI 경로 보완 뒤 Hub 설치 exe도 다시 검증하고 이 작업에서 올린 설치 자산만 갱신했다. 기존 엔진 ZIP은 그대로 유지했다. 이번 소스 변경은 아직 커밋/push하지 않았다. 렌더링/Joint 변경과 섞지 않고 담당 Hub/웹사이트 파일만 별도로 커밋해야 한다.

## Windows 게시자 서명과 SmartScreen (2026-10-04)

현재 공개 `NovaHubSetup.exe`는 **NotSigned**다. 이번 서명 연결 준비로 공개 파일이 자동으로 서명되거나 SmartScreen 경고가 없어지는 것은 아니다. 이 PC의 CurrentUser/LocalMachine `My` 저장소에 코드 서명용 개인 키가 없으며, 서명 계정/신원 검증도 준비되지 않았다. 현재 공개 자산은 교체하지 않는다.

사용자는 현재 개인이며 개인사업자 등록을 진행 중이다. 등록 완료 후 검증된 사업자 명의로 서명하고, 이후 명의 변경은 빌드 설정으로 처리한다. Windows 게시자 이름은 인증서의 검증된 명의다. 단순한 브랜드 이름으로 임의 지정할 수 없다. 다른 법적 명의로 인증서를 바꾸면 기존 SmartScreen 게시자 평판이 계속 유지된다고 보장할 수 없다.

[Microsoft 지원 조건](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart)에 따르면 한국 조직은 Artifact Signing의 Public Trust 신원 검증을 신청할 수 있고, 개인은 미국/캐나다만 지원한다. 개인사업자도 실제 법적 사업자 정보와 제출 서류에 대한 검증이 필요하다. 한국 개인에게 지원되지 않는 Private Trust/Test 프로필은 일반 소비자 배포용 대안으로 사용하지 않는다. 계정 생성/결제/신원 제출은 사용자에게 남아 있다.

### 서명 방식 1: Azure Artifact Signing

사업자 등록이 끝난 뒤 유료 Azure 구독에서 Artifact Signing 계정 → 조직 신원 검증 → **Public Trust** 인증서 프로필을 준비한다. 서명하는 계정에는 Certificate Profile Signer 권한이 필요하다. 명의/주소/사업자 정보 및 소유 도메인 이메일 등은 Microsoft의 검증 요구사항에 따라 제출한다. 실제 프로필이 승인되기 전에는 서명할 수 없다.

[Microsoft 설치 안내](https://learn.microsoft.com/en-us/azure/artifact-signing/how-to-signing-integrations)에 따라 x64 SignTool/Artifact Signing Dlib, .NET 8 런타임과 필요한 VC 런타임을 준비한다. 이번 코드 수정은 SDK의 기존 SignTool만 사용했고, 계정이나 유료 리소스는 생성하지 않았다.

`Tools/NovaHub/signing/artifact-signing.sample.json`을 저장소 밖의 개인 설정 파일로 복사하고 실제 계정/프로필로 바꾼다. Endpoint는 계정 생성 지역과 일치해야 한다. 샘플은 Korea Central이다. Azure 로그인 또는 운영 환경의 관리/워크로드 인증을 사용하고, 비밀 키를 저장소에 넣지 않는다. 샘플은 자동 브라우저 로그인을 제외한다.

```powershell
./Tools/package_hub.ps1 -EngineRoot E:/NovaTest/<별도엔진> -Out E:/NovaTest/<새Hub출력> `
    -SigningDlib '<공식 클라이언트의 x64 Azure.CodeSigning.Dlib.dll>' `
    -SigningMetadata '<저장소 밖 metadata.json>' `
    -ExpectedPublisher '<승인된 인증서에 표시되는 게시자 이름>'
```

### 서명 방식 2: 인증서 저장소

공인 코드 서명 인증서와 연결된 개인 키/하드웨어 토큰을 Windows `My` 인증서 저장소에 준비한 경우 아래처럼 정확한 인증서 지문으로 선택한다. PFX 암호를 명령 인수에 넣거나 개인 키를 Git 저장소에 복사하는 흐름은 제공하지 않는다.

```powershell
./Tools/package_hub.ps1 -EngineRoot E:/NovaTest/<별도엔진> -Out E:/NovaTest/<새Hub출력> `
    -CertificateThumbprint '<인증서의 40자리 SHA1 지문>' `
    -CertificateStore CurrentUser -ExpectedPublisher '<검증된 게시자 이름>'
```

지문은 인증서 식별에만 사용하며 실제 파일 서명 해시는 **SHA256**이다. CurrentUser가 기본이고 LocalMachine은 `-CertificateStore LocalMachine`으로 지정한다. `-TimestampUrl`로 발급 기관의 RFC3161 서버를 지정할 수 있다. 저장소 방식 기본 타임스탬프는 DigiCert, Artifact Signing 방식은 Microsoft 공식 권장 서버다. 자체 서명/만료/코드 서명 용도 없음/개인 키 없음/다른 게시자 인증서는 거절한다.

### 빌드 단계와 배포 검사

- `Tools/HubSigning.ps1`에서 서명 방식/인증서/메타데이터를 먼저 확인한다. 인증서나 서명 설정이 없으면 출력 폴더 생성과 빌드 전에 중단한다.
- 원본 엔진 빌드는 변경하지 않고 패키징용 복사본의 `NovaHub.exe`, `NovaHubService.exe`, `NovaCore.dll`, `nova.exe`를 SHA256과 RFC3161 타임스탬프로 서명한다. 제삼자 DLL의 서명은 그대로 둔다.
- 서명된 파일을 설치 프로그램에 포함한 뒤 `NovaHubSetup.exe` 자체를 서명한다. 각 파일의 Authenticode `Valid`, 타임스탬프, 게시자 이름, 저장소 방식 인증서 지문, SignTool `/pa /all /tw` 검증을 모두 요구한다. SignTool 경고도 실패로 취급한다.
- 모두 성공해야 `hub-signing.json`, portable ZIP, 최종 SHA256 sidecar를 만든다. `hub-signing.json`의 `ReleaseReady`는 서명 검사를 통과했다는 뜻이며 SmartScreen 평판이나 경고 제거를 보증하지 않는다. 서명 후 파일을 수정하면 다시 패키징/서명/해시 생성해야 한다.
- 설정은 인수 또는 `NOVA_SIGNING_CERT_SHA1`, `NOVA_SIGNING_DLIB`, `NOVA_SIGNING_METADATA`, `NOVA_SIGNING_PUBLISHER` 환경 변수로 교체한다. 인증서 저장소 방식과 클라우드 방식을 혼용하지 않는다. 사업자 명의 변경 시 ExpectedPublisher와 인증서/프로필 설정을 함께 갱신한다.
- 미서명은 명시적인 `-AllowUnsigned` **개발 검사 전용**이다. 이 모드는 경고를 출력하고 `ReleaseReady=false`를 기록하며 다른 서명 옵션과 혼용할 수 없다. 공개 업로드 도구는 이 스크립트에 포함되어 있지 않다.

독립 검사: `powershell -NoProfile -ExecutionPolicy Bypass -File Tools/tests/hub_signing.ps1 -Out <새검사폴더> -UnsignedFile <현재미서명설치exe>`. 신원 없음/설정 충돌/잘못된 클라우드 주소/만료·자체 서명 인증서/개인 키 없음/게시자·인증서 불일치/타임스탬프 누락/서명 변조/서명 공급자 실패의 차단을 확인한다. 인증서 경계는 시뮬레이션하며 머신의 인증서·신뢰 저장소를 변경하지 않는다. 실제 공인 인증서 발급 후 클라우드 인증, 실제 서명/타임스탬프, 서명된 설치 프로그램 설치를 추가로 검증해야 한다.

이번 준비의 검사 결과는 **26/26**이다. 실제 SDK SignTool로 기존 Microsoft .NET 실행 파일의 서명/타임스탬프 검증도 통과했다. 명시 개발용 모드의 전체 패키징과 검사 전용 설치에서 payload **30/30**, SHA256 sidecar **2/2**, `ReleaseReady=false`, 원래 공개 설치 파일 해시 보존을 확인했다 (`TestResults/CodexHub/signing-package-results.json`, `signing-real-verification.json`). 공인 NOVA 인증서로 서명에 성공했다는 뜻은 아니다.

[Microsoft SmartScreen 안내](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)에 따르면 유효한 OV/EV 서명도 초기 다운로드 경고를 즉시 없애지 못한다. 같은 검증된 게시자 명의로 꾸준히 서명해야 평판을 쌓을 수 있다. EV 구입만으로 경고를 없애거나 자체 서명으로 공인 게시자를 만드는 방법은 사용하지 않는다.
