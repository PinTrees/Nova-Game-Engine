# 웹 빌드의 셰이더 변환기 Tint (SPIR-V → WGSL, Chrome 의 WebGPU 가 쓰는 구글 변환기) 를 받아 빌드한다
#   powershell -File Tools/web/build_tint.ps1      →  %USERPROFILE%\.nova\dawn\out\tint\Release\tint.exe (ShaderCross::TintPath 가 찾는다)
#  - Dawn 저장소를 얕게 받고, Tint 가 쓰는 의존성 셋 (SPIRV-Headers · SPIRV-Tools · abseil) 만 DEPS 의 커밋으로 받는다 (~0.6 GB)
#  - Visual Studio 의 CMake · MSBuild 로 tint 실행 파일만 (Release, 20 ~ 30 분)
param([string]$Root = (Join-Path $env:USERPROFILE '.nova\dawn'))
$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $Root)) { git clone --depth 1 https://dawn.googlesource.com/dawn $Root }
$deps = Get-Content (Join-Path $Root 'DEPS') -Raw
foreach ($d in @('third_party/spirv-headers/src', 'third_party/spirv-tools/src', 'third_party/abseil-cpp'))
{
    $dir = Join-Path $Root $d
    if (Test-Path (Join-Path $dir '.git')) { continue }
    # 'third_party/x': { 'url': '{chromium_git}/...@<커밋>'
    $m = [regex]::Match($deps, "'" + [regex]::Escape($d) + "':\s*\{\s*'url':\s*'\{chromium_git\}([^@']+)@([0-9a-f]+)'")
    if (-not $m.Success) { throw "DEPS has no $d" }
    New-Item -ItemType Directory -Force $dir | Out-Null
    git -C $dir init -q
    git -C $dir remote add origin ('https://chromium.googlesource.com' + $m.Groups[1].Value)
    git -C $dir fetch -q --depth 1 origin $m.Groups[2].Value
    git -C $dir checkout -q FETCH_HEAD
}
$out = Join-Path $Root 'out\tint'
& $cmake -S $Root -B $out -A x64 -DDAWN_ENABLE_D3D11=OFF -DDAWN_ENABLE_D3D12=OFF -DDAWN_ENABLE_VULKAN=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF `
    -DDAWN_ENABLE_NULL=OFF -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_BUILD_PROTOBUF=OFF -DDAWN_USE_GLFW=OFF -DTINT_BUILD_SPV_READER=ON `
    -DTINT_BUILD_WGSL_WRITER=ON -DTINT_BUILD_WGSL_READER=ON -DTINT_BUILD_GLSL_WRITER=OFF -DTINT_BUILD_HLSL_WRITER=OFF -DTINT_BUILD_MSL_WRITER=OFF `
    -DTINT_BUILD_SPV_WRITER=OFF -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_GLSL_VALIDATOR=OFF -DTINT_BUILD_CMD_TOOLS=ON | Out-Null
& $msbuild (Join-Path $out 'src\tint\tint_cmd_tint_cmd.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
$exe = Join-Path $out 'Release\tint.exe'
if (-not (Test-Path $exe)) { throw 'tint.exe was not built' }
"tint: $exe"
