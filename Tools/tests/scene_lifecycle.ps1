# 씬 복원 · 경로 회귀 검사. 공용 run_tests.ps1/common.ps1 을 수정하거나 불러오지 않는다.
#   powershell -ExecutionPolicy Bypass -File Tools/tests/scene_lifecycle.ps1
#   ... -EngineRoot <독립 빌드한 엔진 루트> -Out <새 결과 폴더>
# 엔진과 리소스를 결과 폴더에 복사하고, 새 프로젝트와 전용 에디터 PID만 사용한다.
# 공용 엔진을 복사할 때 Claude가 사용 중이라고 기록했으면 실행을 거절한다.
[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$Out = '',
    [ValidateRange(10, 300)][int]$ReadyTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $EngineRoot) { $EngineRoot = $repoRoot }
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$claudeStatus = Join-Path $repoRoot 'docs/ai-status/CLAUDE.md'
if ($EngineRoot -eq $repoRoot -and (Test-Path -LiteralPath $claudeStatus))
{
    $status = Get-Content -LiteralPath $claudeStatus -Raw
    if ($status -match '지금 사용 중:\s*Claude')
    {
        throw 'Claude is using the shared build. Wait for its release or use -EngineRoot with an independent build.'
    }
}

$sourceBin = Join-Path $EngineRoot 'Binaries'
foreach ($file in @('NovaEngine.exe', 'NovaCore.dll', 'nova.exe'))
{
    if (-not (Test-Path -LiteralPath (Join-Path $sourceBin $file))) { throw "Build first: missing $file" }
}
if (-not $Out) { $Out = Join-Path $repoRoot ('TestResults/CodexScene/runtime-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)) }
$Out = [IO.Path]::GetFullPath($Out)
if (Test-Path -LiteralPath $Out) { throw 'Use a new output folder; an existing project will never be reused.' }
New-Item -ItemType Directory -Path $Out | Out-Null
$sandboxEngine = Join-Path $Out 'Engine'
$sandboxBin = Join-Path $sandboxEngine 'Binaries'
$project = Join-Path $Out 'Project'
$scenes = Join-Path $project 'Assets/Scenes'
$externalDir = Join-Path $Out '외부 씬'
foreach ($dir in @($sandboxBin, $scenes, (Join-Path $project 'ProjectSettings'), (Join-Path $project 'Packages'), $externalDir))
{
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
# .ini/로그/텍스처 캐시는 복사하지 않는다. 테스트의 레이아웃과 로그는 전용 엔진 폴더에 생긴다.
Get-ChildItem -LiteralPath $sourceBin -File | Where-Object Extension -in @('.exe', '.dll') | Copy-Item -Destination $sandboxBin
foreach ($dir in @('Shaders', 'Resources', 'ProjectSetting', 'Binaries/Scripting', 'Binaries/ShaderCache'))
{
    $src = Join-Path $EngineRoot $dir
    if (Test-Path -LiteralPath $src)
    {
        $dst = Join-Path $sandboxEngine $dir
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item -LiteralPath $src -Destination $dst -Recurse
    }
}
$utf8 = New-Object Text.UTF8Encoding($false)
function Write-JsonFile([string]$Path, $Value)
{
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 20), $utf8)
}
Write-JsonFile (Join-Path $project 'Packages/manifest.json') @{ dependencies = @{} }
Write-JsonFile (Join-Path $project 'ProjectSettings/ProjectSettings.json') @{ projectName = 'CodexSceneTests' }
Write-JsonFile (Join-Path $project 'ProjectSettings/EditorBuildSettings.json') @{ scenes = @(@{ path = 'Assets/Scenes/Destination.scene'; enabled = $true }) }
Write-JsonFile (Join-Path $project 'Assets/EditorSettings.json') @{ LastOpenedScenePath = '' }

$nova = Join-Path $sandboxBin 'nova.exe'
$engineExe = Join-Path $sandboxBin 'NovaEngine.exe'
$editor = $null
$results = New-Object 'System.Collections.Generic.List[object]'
$consoleEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = $utf8

function Invoke-Editor([string[]]$Arguments, [int]$Timeout = 30)
{
    if (-not $editor -or $editor.HasExited) { throw 'The test editor is not running.' }
    # 예상한 실패도 stderr 로 오므로 종료 코드와 JSON 을 직접 판단한다.
    $oldPreference = $ErrorActionPreference
    try
    {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $nova @Arguments --pid $editor.Id --json --timeout $Timeout 2>&1)
        $code = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $oldPreference }
    $text = ($lines | ForEach-Object { $_.ToString() }) -join "`n"
    [pscustomobject]@{ ExitCode = $code; Text = $text }
}
function Command([string[]]$Arguments)
{
    $reply = Invoke-Editor $Arguments
    if ($reply.ExitCode -ne 0) { throw "nova $($Arguments -join ' '): $($reply.Text)" }
    ConvertFrom-Json -InputObject $reply.Text
}
function Check([string]$Name, [bool]$Pass, [string]$Detail)
{
    $results.Add([pscustomobject]@{ Suite = 'scene_lifecycle'; Test = $Name; Result = $(if ($Pass) { 'PASS' } else { 'FAIL' }); Detail = $Detail })
    Write-Host "[$(if ($Pass) { 'PASS' } else { 'FAIL' })] $Name — $Detail"
}
function Start-Editor
{
    $script:editor = Start-Process -FilePath $engineExe -ArgumentList @('--project', ('"' + $project + '"'), '--no-activate', '-force-d3d11') -WorkingDirectory $sandboxBin -WindowStyle Hidden -PassThru
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $ReadyTimeoutSeconds)
    {
        $reply = Invoke-Editor @('info') 1
        if ($reply.ExitCode -eq 0)
        {
            $info = ConvertFrom-Json -InputObject $reply.Text
            if ($info.project.TrimEnd('\', '/') -ne $project) { throw 'Unexpected project on the test editor PID.' }
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw 'Test editor startup timed out.'
}
function Close-Editor
{
    if ($editor -and -not $editor.HasExited)
    {
        Invoke-Editor @('quit', '--force') | Out-Null
        if (-not $editor.WaitForExit(10000)) { $editor.Kill(); $editor.WaitForExit() }
    }
    $script:editor = $null
}
function Path-Equals([string]$A, [string]$B) { $A.Replace('/', '\') -eq $B.Replace('/', '\') }
function Has-Origin { @((Command @('find', 'CodexOrigin'))).Count -eq 1 }
function Switch-DuringPlay
{
    $switchScript = Join-Path $Out 'switch_scene.cs'
    [IO.File]::WriteAllText($switchScript, 'NovaEngine.SceneManagement.SceneManager.LoadScene("Destination"); return true;', $utf8)
    Command @('exec', '--file', $switchScript) | Out-Null
    Command @('wait', '10') | Out-Null
    $info = Command @('info')
    Check 'Play loads the destination scene' (Path-Equals $info.scene 'Assets/Scenes/Destination.scene') $info.scene
}

try
{
    Start-Editor
    Command @('scene', 'new', '--force') | Out-Null
    Command @('create', 'empty', '--name', 'CodexDestination') | Out-Null
    Command @('scene', 'save', '--as', 'Assets/Scenes/Destination.scene') | Out-Null
    $destinationFile = Join-Path $scenes 'Destination.scene'
    $destinationHash = (Get-FileHash -LiteralPath $destinationFile).Hash

    Command @('scene', 'new', '--force') | Out-Null
    Command @('create', 'empty', '--name', 'CodexOrigin') | Out-Null
    Command @('play') | Out-Null
    Switch-DuringPlay
    Command @('stop') | Out-Null
    $info = Command @('info')
    Check 'Untitled keeps an empty path after Play scene switch' ($info.scene -eq '' -and (Has-Origin)) "path=$($info.scene)"
    $save = Invoke-Editor @('scene', 'save')
    Check 'Untitled save requires a new path' ($save.ExitCode -ne 0 -and $save.Text -match 'never been saved') $save.Text
    Check 'Destination file is unchanged after Stop and save attempt' ((Get-FileHash -LiteralPath $destinationFile).Hash -eq $destinationHash) $destinationFile

    Command @('play') | Out-Null
    Command @('wait', '10') | Out-Null
    Command @('stop') | Out-Null
    $info = Command @('info')
    Check 'Untitled Play Stop without scene switch preserves content' ($info.scene -eq '' -and (Has-Origin)) "path=$($info.scene)"

    Command @('scene', 'save', '--as', 'Assets/Scenes/Origin.scene') | Out-Null
    $originFile = Join-Path $scenes 'Origin.scene'
    $originHash = (Get-FileHash -LiteralPath $originFile).Hash
    Command @('play') | Out-Null
    Switch-DuringPlay
    Command @('stop') | Out-Null
    $info = Command @('info')
    Check 'Saved scene restores its path and content after switching' ((Path-Equals $info.scene 'Assets/Scenes/Origin.scene') -and (Has-Origin)) $info.scene
    Check 'Play Stop does not change either saved file' (((Get-FileHash -LiteralPath $originFile).Hash -eq $originHash) -and ((Get-FileHash -LiteralPath $destinationFile).Hash -eq $destinationHash)) 'Origin and Destination hashes'

    Command @('play') | Out-Null
    Command @('wait', '10') | Out-Null
    Command @('stop') | Out-Null
    $info = Command @('info')
    Check 'Saved Play Stop without scene switch preserves content' ((Path-Equals $info.scene 'Assets/Scenes/Origin.scene') -and (Has-Origin)) $info.scene

    $unicodeRelative = 'Assets/Scenes/한글 씬.scene'
    Command @('scene', 'save', '--as', $unicodeRelative) | Out-Null
    Command @('scene', 'new', '--force') | Out-Null
    Command @('scene', 'open', $unicodeRelative, '--force') | Out-Null
    $info = Command @('info')
    Check 'Relative scene with Korean and spaces reopens' ((Path-Equals $info.scene $unicodeRelative) -and (Has-Origin)) $info.scene

    $externalFile = Join-Path $externalDir '레벨 테스트.scene'
    Copy-Item -LiteralPath (Join-Path $project $unicodeRelative) -Destination $externalFile
    Command @('scene', 'open', $externalFile, '--force') | Out-Null
    $info = Command @('info')
    Check 'External absolute scene with Korean and spaces opens' ((Path-Equals $info.scene $externalFile) -and (Has-Origin)) $info.scene
    Command @('create', 'empty', '--name', 'CodexExternalSaved') | Out-Null
    Command @('scene', 'save') | Out-Null
    Command @('scene', 'new', '--force') | Out-Null
    Command @('scene', 'open', $externalFile, '--force') | Out-Null
    Check 'External scene edits save and reopen' (@((Command @('find', 'CodexExternalSaved'))).Count -eq 1) $externalFile

    $missing = Invoke-Editor @('scene', 'open', (Join-Path $externalDir 'missing.scene'), '--force')
    $info = Command @('info')
    Check 'Missing file is rejected without changing the current scene' ($missing.ExitCode -ne 0 -and (Path-Equals $info.scene $externalFile) -and (Has-Origin)) $missing.Text

    Close-Editor
    Start-Editor
    $info = Command @('info')
    Check 'Startup restores the last external absolute scene' ((Path-Equals $info.scene $externalFile) -and (Has-Origin)) $info.scene
}
catch { Check 'Test harness completed' $false $_.Exception.Message }
finally
{
    try { Close-Editor } catch { Check 'Test editor shutdown' $false $_.Exception.Message }
    [Console]::OutputEncoding = $consoleEncoding
    Write-JsonFile (Join-Path $Out 'results.json') @($results.ToArray())
    Write-JsonFile (Join-Path $Out 'engine.json') @{ root = $EngineRoot; dllSha256 = (Get-FileHash -LiteralPath (Join-Path $sandboxBin 'NovaCore.dll')).Hash; project = $project }
}
if (@($results | Where-Object Result -eq 'FAIL').Count) { exit 1 }
