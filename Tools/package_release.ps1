# NOVA 엔진 배포 묶음 만들기: Release 빌드 → 실행에 필요한 파일만 모아 zip (dist/NOVA-Engine-<버전>-win64.zip)
#
#   powershell -ExecutionPolicy Bypass -File Tools\package_release.ps1            # Release 빌드 + 기본 묶음
#   ... -Full               Resources 전체 (옛 예제 텍스처 포함, 약 2 GB)
#   ... -NoBuild            지금 Binaries 그대로 (Release 로 빌드되어 있어야 함)
#   ... -AllowDebugAssimp   assimp 릴리스 DLL 이 없어도 진행 (시험용 — Visual Studio 가 없는 PC 에서는 실행되지 않는다)
#
# 들어가는 것: Binaries(NovaEngine.exe + NovaCore.dll, nova.exe, DLL, Scripting), Shaders, ProjectSetting(글꼴·아이콘·로고),
#             Resources(Packages, Textures/Skybox, Models), Assets(엔진 샘플 설정), VC++ 런타임(앱 로컬 배포), 읽어 보기.
# 빠지는 것: pdb, 로그, 셰이더·텍스처 캐시(첫 실행에 다시 만든다), 레이아웃 ini, 개발용 파일.
param([string]$Version = '', [switch]$Full, [switch]$NoBuild, [switch]$AllowDebugAssimp, [string]$Out = 'dist')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

if (-not $Version)
{
    $m = Select-String -Path 'Source\Core\EngineInfo.h' -Pattern 'ENGINE_VERSION_A "([^"]+)"' | Select-Object -First 1
    $Version = if ($m) { $m.Matches[0].Groups[1].Value } else { '0.0.0' }
}
$name = "NOVA-Engine-$Version-win64"
$stage = Join-Path $root "$Out\$name"
$zip = Join-Path $root "$Out\$name.zip"

# ---- 1. Release 빌드
if (-not $NoBuild)
{
    Write-Host '[1/5] Release 빌드...'
    & (Join-Path $root 'build.bat') release | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Release 빌드 실패 (build.bat release 를 직접 실행해 오류를 보세요)' }
}

# ---- 2. assimp: Release 엔진도 지금은 디버그 CRT 의 assimp-vc143-mtd.dll 을 쓴다 (저장소에 릴리스 DLL 이 없음).
#         디버그 CRT(ucrtbased.dll 등)는 재배포할 수 없어 Visual Studio 가 없는 PC 에서는 실행되지 않는다.
$assimpRelease = @('assimp-vc143-mt.dll', 'Binaries\assimp-vc143-mt.dll') | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $assimpRelease -and -not $AllowDebugAssimp)
{
    throw ("assimp 릴리스 DLL(assimp-vc143-mt.dll)이 없습니다. assimp 를 Release(/MD)로 빌드해 저장소 루트에 두고 CMake 에서 " +
        "NOVA_ASSIMP_RELEASE_DLL 을 켜야 Visual Studio 없는 PC 에서도 실행됩니다. 시험용으로는 -AllowDebugAssimp.")
}

# ---- 3. 파일 모으기
Write-Host "[2/5] 파일 모으기 → $stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
function CopyDir([string]$from, [string]$to, [string[]]$exclude = @())
{
    if (-not (Test-Path $from)) { return }
    New-Item -ItemType Directory -Force (Join-Path $stage $to) | Out-Null
    robocopy $from (Join-Path $stage $to) /E /NFL /NDL /NJH /NJS /NP /XF *.pdb *.ilk *.log /XD $exclude | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "복사 실패: $from" }
}
$bin = Join-Path $stage 'Binaries'
New-Item -ItemType Directory -Force $bin | Out-Null
foreach ($f in @('NovaEngine.exe', 'NovaCore.dll', 'nova.exe', 'dxcompiler.dll', 'dxil.dll'))
{
    if (-not (Test-Path "Binaries\$f")) { throw "Binaries\$f 가 없습니다 (빌드했나요?)" }
    Copy-Item "Binaries\$f" $bin
}
if ($assimpRelease) { Copy-Item $assimpRelease $bin } else { Copy-Item 'Binaries\assimp-vc143-mtd.dll' $bin }
CopyDir 'Binaries\Scripting' 'Binaries\Scripting'
CopyDir 'Packages' 'Packages' @('Source')   # NOVA 레지스트리 패키지 (package.json, Runtime C#, Plugins DLL·abi — 소스 제외)
CopyDir 'Shaders' 'Shaders'
CopyDir 'ProjectSetting' 'ProjectSetting'
CopyDir 'Assets' 'Assets'
if ($Full)
{
    CopyDir 'Resources' 'Resources'
}
else
{
    CopyDir 'Resources\Packages' 'Resources\Packages'
    CopyDir 'Resources\Textures\Skybox' 'Resources\Textures\Skybox'
    CopyDir 'Resources\Models' 'Resources\Models'
}
# 안드로이드 플레이어 라이브러리 (Build Settings → Android 가 APK 에 넣는다 — python Android/build.py 로 만든 것)
foreach ($abi in 'x86_64', 'arm64-v8a')
{
    $so = "Android\build\cmake\$abi-Release\libnova.so"
    if (Test-Path $so) { New-Item -ItemType Directory -Force (Join-Path $stage "Android\Player\$abi") | Out-Null; Copy-Item $so (Join-Path $stage "Android\Player\$abi") }
    # C# 런타임 (Mono — Tools/fetch_android_mono.ps1): 관리 어셈블리 (BCL) + 네이티브 (.so · System.Private.CoreLib.dll, 정적 .a 는 빼고)
    $rid = if ($abi -eq 'arm64-v8a') { 'android-arm64' } else { 'android-x64' }
    $mono = "ThirdParty\MonoAndroid\$abiuntimes\$rid"
    if (Test-Path "$mono
ative\libmonosgen-2.0.so")
    {
        $dst = Join-Path $stage "Android\Player\$abi\mono"
        New-Item -ItemType Directory -Force "$dst\lib", "$dst
ative" | Out-Null
        Copy-Item "$mono\lib
et8.0\*.dll" "$dst\lib"
        Get-ChildItem "$mono
ative" -File | Where-Object { $_.Extension -in '.so', '.dll' } | Copy-Item -Destination "$dst
ative"
        Copy-Item "ThirdParty\MonoAndroid\$abi\LICENSE.TXT", "ThirdParty\MonoAndroid\$abi\THIRD-PARTY-NOTICES.TXT" "$dst" -ErrorAction SilentlyContinue
    }
}

# ---- 4. VC++ 런타임 (앱 로컬 배포 — Microsoft 가 재배포를 허락한 Redist 폴더의 DLL)
Write-Host '[3/5] VC++ 런타임'
$vs = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Directory -ErrorAction SilentlyContinue | ForEach-Object { Get-ChildItem $_.FullName -Directory } |
    ForEach-Object { Join-Path $_.FullName 'VC\Redist\MSVC' } | Where-Object { Test-Path $_ } | Select-Object -First 1
$redist = if ($vs) { Get-ChildItem $vs -Directory | Where-Object { $_.Name -match '^\d' } | Sort-Object Name -Descending | Select-Object -First 1 } else { $null }
if ($redist)
{
    foreach ($sub in 'Microsoft.VC14*.CRT', 'Microsoft.VC14*.OpenMP')
    {
        Get-ChildItem (Join-Path $redist.FullName 'x64') -Directory -Filter $sub -ErrorAction SilentlyContinue | Select-Object -First 1 |
            ForEach-Object { Copy-Item (Join-Path $_.FullName '*.dll') $bin }
    }
}
else { Write-Warning 'VC++ Redist 폴더를 찾지 못했습니다 — 받는 PC 에 Visual C++ 재배포 패키지가 있어야 합니다' }

# ---- 5. 읽어 보기
$debugNote = if ($assimpRelease) { '' } else { "`r`n※ 이 묶음은 시험용입니다: assimp 디버그 DLL 을 써서 Visual Studio(C++) 가 설치된 PC 에서만 실행됩니다.`r`n" }
@"
NOVA Game Engine $Version (Windows x64)
공식 사이트: https://nova-game-engine.web.app
소스: https://github.com/PinTrees/Nova-Game-Engine
$debugNote
실행
  Binaries\NovaEngine.exe                    NOVA Hub (프로젝트 만들기 / 열기, NOVA CLI 설치)
  Binaries\NovaEngine.exe --project <폴더>   그 프로젝트를 에디터로
  -force-opengl / -force-d3d11                이번만 그래픽 API 바꾸기

필요한 것
  - Windows 10 / 11 (x64), DirectX 11 또는 OpenGL 4.5 GPU
  - C# 스크립트를 쓰려면 .NET SDK 8 이상 (https://dotnet.microsoft.com/download)

처음 실행은 셰이더를 컴파일하느라 몇 초 더 걸립니다 (Binaries\ShaderCache 에 저장).
"@ | Set-Content -Encoding utf8 (Join-Path $stage 'README.txt')

# ---- zip
Write-Host '[4/5] zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
$size = (Get-Item $zip).Length / 1MB
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
Write-Host ('[5/5] {0}  ({1:N1} MB)  SHA256 {2}' -f $zip, $size, $hash)
