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
Write-JsonFile (Join-Path $project 'ProjectSettings/EditorBuildSettings.json') @{ scenes = @(@{ path = 'Assets/Scenes/Destination.scene'; enabled = $true }, @{ path = 'Assets/Scenes/BrokenPlay.scene'; enabled = $true }) }
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
function Capture-GuardState
{
    [pscustomobject]@{
        Info = (Command @('info'))
        Object = ((Command @('get', 'CodexLoadGuard')) | ConvertTo-Json -Depth 20 -Compress)
    }
}
function Guard-Unchanged($Before, $After)
{
    $a = $Before.Info; $b = $After.Info
    (Path-Equals $a.scene $b.scene) -and $a.dirty -eq $b.dirty -and $a.playing -eq $b.playing -and
        $a.objects -eq $b.objects -and $a.liveObjects -eq $b.liveObjects -and
        $a.canUndo -eq $b.canUndo -and $a.undo -eq $b.undo -and
        ($a.selection | ConvertTo-Json -Compress) -eq ($b.selection | ConvertTo-Json -Compress) -and
        $Before.Object -eq $After.Object
}
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

    $goodExternal = [IO.File]::ReadAllText($externalFile)
    Command @('create', 'empty', '--name', 'CodexLoadGuard') | Out-Null
    Command @('set', 'CodexLoadGuard', '--position', '2,3,4') | Out-Null
    Command @('select', 'CodexLoadGuard') | Out-Null
    $before = Capture-GuardState
    Check 'Load protection starts with unsaved work and selection' ($before.Info.dirty -and $before.Info.selection) 'Saved scene with an unsaved object'

    # 성공적으로 복원된 루트/컴포넌트/자식 뒤에 오류를 넣어 부분 복원의 정리도 확인한다.
    $valid = @{ name = 'TemporaryRoot'; components = @(@{ type = 'Transform' }); children = @() }
    $fixtures = @(
        @{ Name = 'truncated JSON'; Text = '{"rootGameObjects":[' },
        @{ Name = 'empty file'; Text = '' },
        @{ Name = 'trailing garbage'; Text = ($goodExternal + ' broken') },
        @{ Name = 'non-object scene'; Text = '[]' },
        @{ Name = 'missing roots'; Text = '{}' },
        @{ Name = 'null roots'; Text = '{"rootGameObjects":null}' },
        @{ Name = 'object instead of roots array'; Text = '{"rootGameObjects":{}}' },
        @{ Name = 'invalid root after a valid root'; Data = @{ rootGameObjects = @($valid, @{ components = @() }) } },
        @{ Name = 'missing components'; Data = @{ rootGameObjects = @(@{ name = 'Broken' }) } },
        @{ Name = 'invalid components collection'; Data = @{ rootGameObjects = @(@{ name = 'Broken'; components = @{} }) } },
        @{ Name = 'invalid component type after a light'; Data = @{ rootGameObjects = @(@{ name = 'Broken'; components = @(@{ type = 'Light' }, @{ type = 1 }) }) } },
        @{ Name = 'invalid component field'; Data = @{ rootGameObjects = @(@{ name = 'Broken'; components = @(@{ type = 'Light'; intensity = 'bad' }) }) } },
        @{ Name = 'invalid children collection'; Data = @{ rootGameObjects = @(@{ name = 'Broken'; components = @(); children = @{} }) } },
        @{ Name = 'nested child failure'; Data = @{ rootGameObjects = @(@{ name = 'Parent'; components = @(); children = @(@{ name = 'Child'; components = @(@{ type = 'Light' }); children = @($valid, @{ components = @() }) }) }) } }
    )
    $invalidDir = Join-Path $scenes 'Invalid'
    New-Item -ItemType Directory -Path $invalidDir | Out-Null
    $index = 0
    foreach ($fixture in $fixtures)
    {
        $badFile = Join-Path $invalidDir ("invalid-$index.scene")
        $text = if ($fixture.ContainsKey('Text')) { $fixture.Text } else { $fixture.Data | ConvertTo-Json -Depth 20 }
        [IO.File]::WriteAllText($badFile, $text, $utf8)
        $reply = Invoke-Editor @('scene', 'open', $badFile, '--force')
        $after = Capture-GuardState
        Check ("Reject $($fixture.Name) and preserve work") ($reply.ExitCode -ne 0 -and (Guard-Unchanged $before $after)) $reply.Text
        $index++
    }
    $missing = Invoke-Editor @('scene', 'open', (Join-Path $invalidDir 'missing.scene'), '--force')
    Check 'Missing file preserves dirty content selection and Undo' ($missing.ExitCode -ne 0 -and (Guard-Unchanged $before (Capture-GuardState))) $missing.Text

    # 같은 파일 다시 열기(Don't Save)는 DiscardChanges 경로를 사용한다.
    try
    {
        [IO.File]::WriteAllText($externalFile, ($fixtures[-1].Data | ConvertTo-Json -Depth 20), $utf8)
        $reload = Invoke-Editor @('scene', 'open', $externalFile, '--force')
        Check 'Failed reload preserves current work' ($reload.ExitCode -ne 0 -and (Guard-Unchanged $before (Capture-GuardState))) $reload.Text
    }
    finally { [IO.File]::WriteAllText($externalFile, $goodExternal, $utf8) }
    Command @('scene', 'open', $externalFile, '--force') | Out-Null
    $info = Command @('info')
    Check 'Valid reload discards edits and restores the saved scene' (-not $info.dirty -and @((Command @('find', 'CodexLoadGuard'))).Count -eq 0 -and (Has-Origin)) $info.scene

    Command @('create', 'empty', '--name', 'CodexLoadGuard') | Out-Null
    Command @('scene', 'save') | Out-Null
    Command @('play') | Out-Null
    $playBefore = Capture-GuardState
    $badPlayFile = Join-Path $scenes 'BrokenPlay.scene'
    [IO.File]::WriteAllText($badPlayFile, '{', $utf8)
    $switchScript = Join-Path $Out 'switch_broken_scene.cs'
    [IO.File]::WriteAllText($switchScript, 'NovaEngine.SceneManagement.SceneManager.LoadScene("Assets/Scenes/BrokenPlay.scene"); return true;', $utf8)
    Command @('exec', '--file', $switchScript) | Out-Null
    Command @('wait', '10') | Out-Null
    Check 'Play rejects a broken scene and keeps the running scene' (Guard-Unchanged $playBefore (Capture-GuardState)) 'LoadSceneDuringPlay'
    Command @('stop') | Out-Null
    Close-Editor
    $logFiles = @(Get-ChildItem -LiteralPath $sandboxEngine -Filter 'Editor.log' -File -Recurse)
    $logs = ($logFiles | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
    Check 'Load failures report the path and reason in the editor log' ($logs -match 'Could not open scene.*invalid-0.scene' -and $logs -match 'rootGameObjects must be an array') 'Console errors are also written to Editor.log'
    Check 'Play failure reaches the scene loader' ($logs -match 'Could not open scene.*BrokenPlay.scene') 'Broken scene is in Build Settings'

    Start-Editor
    $info = Command @('info')
    Check 'Startup restores the last external absolute scene' ((Path-Equals $info.scene $externalFile) -and (Has-Origin)) $info.scene

    # 충돌을 만들지 않고, 종료된 세션의 자동 복구 파일만 테스트 프로젝트에 준비한다.
    $recoveryDir = Join-Path $project 'Library/AutoSave'
    $recoveryScene = Join-Path $recoveryDir 'autosave_999999999.scene'
    foreach ($recoveryFixture in @($fixtures[4], $fixtures[-1]))
    {
        Close-Editor
        New-Item -ItemType Directory -Force -Path $recoveryDir | Out-Null
        Write-JsonFile (Join-Path $recoveryDir 'session_999999999.json') @{}
        Write-JsonFile (Join-Path $recoveryDir 'autosave_999999999.json') @{ scenePath = $externalFile; sceneName = 'CodexRecovery'; time = ([DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 60); objects = 1 }
        $text = if ($recoveryFixture.ContainsKey('Text')) { $recoveryFixture.Text } else { $recoveryFixture.Data | ConvertTo-Json -Depth 20 }
        [IO.File]::WriteAllText($recoveryScene, $text, $utf8)
        Start-Editor
        Command @('set', 'CodexLoadGuard', '--position', '8,9,10') | Out-Null
        Command @('select', 'CodexLoadGuard') | Out-Null
        $recoveryBefore = Capture-GuardState
        $reply = Invoke-Editor @('autosave', 'recover')
        Check ("Recovery rejects $($recoveryFixture.Name) and preserves work") ($reply.ExitCode -ne 0 -and (Guard-Unchanged $recoveryBefore (Capture-GuardState))) $reply.Text
    }

    Close-Editor
    Write-JsonFile (Join-Path $recoveryDir 'session_999999999.json') @{}
    Write-JsonFile (Join-Path $recoveryDir 'autosave_999999999.json') @{ scenePath = $externalFile; sceneName = 'CodexRecovery'; time = ([DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 60); objects = 1 }
    Write-JsonFile $recoveryScene @{ rootGameObjects = @(@{ name = 'CodexRecovered'; components = @(); children = @($valid) }) }
    Start-Editor
    Command @('autosave', 'recover') | Out-Null
    $info = Command @('info')
    Check 'Valid recovery replaces the scene and keeps unsaved status' ((Path-Equals $info.scene $externalFile) -and $info.dirty -and $info.objects -eq 2 -and @((Command @('find', 'CodexRecovered'))).Count -eq 1) $info.scene

    Close-Editor
    try
    {
        [IO.File]::WriteAllText($externalFile, '{', $utf8)
        Start-Editor
        $info = Command @('info')
        Check 'Broken startup scene opens a valid Untitled scene' ($info.scene -eq '' -and $info.objects -ge 2 -and -not $info.dirty) 'Editor remains available without overwriting the broken file'
        Check 'Startup does not overwrite the broken scene file' ([IO.File]::ReadAllText($externalFile) -eq '{') $externalFile
    }
    finally { [IO.File]::WriteAllText($externalFile, $goodExternal, $utf8) }

    # 새 씬을 먼저 읽어도, 성공한 교체에서 이전 씬의 미저장 지형이 따라오면 안 된다.
    Command @('create', 'terrain', '--name', 'CodexTerrain') | Out-Null
    Command @('terrain-trees', 'CodexTerrain', '--count', '0', '--clear') | Out-Null
    $terrainScene = 'Assets/Scenes/TerrainOrigin.scene'
    Command @('scene', 'save', '--as', $terrainScene) | Out-Null
    $terrainJson = [IO.File]::ReadAllText((Join-Path $project $terrainScene))
    $terrainData = Command @('get', 'CodexTerrain', '--component', 'Terrain')
    $terrainFile = Join-Path $project $terrainData.terrainData
    $terrainHash = (Get-FileHash -LiteralPath $terrainFile).Hash
    $trees = Command @('terrain-trees', 'CodexTerrain', '--count', '3')
    Check 'Terrain test starts with unsaved trees' ($trees.trees -eq 3 -and (Command @('info')).dirty -and (Get-FileHash -LiteralPath $terrainFile).Hash -eq $terrainHash) 'Three trees in memory; terrain file remains unchanged'

    $badTerrain = ConvertFrom-Json -InputObject $terrainJson
    $badTerrain.rootGameObjects = @($badTerrain.rootGameObjects) + @(@{ components = @() })
    $badTerrainFile = Join-Path $scenes 'InvalidTerrain.scene'
    Write-JsonFile $badTerrainFile $badTerrain
    $reply = Invoke-Editor @('scene', 'open', $badTerrainFile, '--force')
    $trees = Command @('terrain-trees', 'CodexTerrain', '--count', '0')
    Check 'Failed scene load preserves unsaved terrain data' ($reply.ExitCode -ne 0 -and $trees.trees -eq 3 -and (Command @('info')).dirty -and (Get-FileHash -LiteralPath $terrainFile).Hash -eq $terrainHash) $reply.Text

    Command @('scene', 'open', $terrainScene, '--force') | Out-Null
    $trees = Command @('terrain-trees', 'CodexTerrain', '--count', '0')
    Check 'Successful reload drops unsaved terrain edits' ($trees.trees -eq 0 -and -not (Command @('info')).dirty) 'Saved terrain has zero trees'

    $terrainDestination = 'Assets/Scenes/TerrainDestination.scene'
    [IO.File]::WriteAllText((Join-Path $project $terrainDestination), $terrainJson, $utf8)
    Command @('terrain-trees', 'CodexTerrain', '--count', '3') | Out-Null
    Command @('scene', 'open', $terrainDestination, '--force') | Out-Null
    $trees = Command @('terrain-trees', 'CodexTerrain', '--count', '0')
    Check 'Scene switch reloads shared terrain from the saved file' ($trees.trees -eq 0 -and -not (Command @('info')).dirty) 'Destination shares the same terrain asset'
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
