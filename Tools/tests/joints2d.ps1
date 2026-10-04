# Standalone Joint2D integration tests. Uses a fresh project and its own hidden editor PID.
# powershell -ExecutionPolicy Bypass -File Tools/tests/joints2d.ps1 -EngineRoot <private build>
[CmdletBinding()]
param([string]$EngineRoot = '', [string]$Out = '', [ValidateRange(10, 300)][int]$ReadyTimeoutSeconds = 120)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $EngineRoot) { $EngineRoot = $repoRoot }
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
if ($EngineRoot -eq $repoRoot -and (Get-Content (Join-Path $repoRoot 'docs/ai-status/CLAUDE.md') -Raw) -match '지금 사용 중:\s*Claude') { throw 'Shared build is in use. Supply an independent -EngineRoot.' }
if (-not $Out) { $Out = Join-Path $repoRoot ('TestResults/Joints2D-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$Out = [IO.Path]::GetFullPath($Out)
if (Test-Path -LiteralPath $Out) { throw 'Use a new output folder.' }
$sandbox = Join-Path $Out 'Engine'; $bin = Join-Path $sandbox 'Binaries'; $project = Join-Path $Out 'Project'
foreach ($dir in @($bin, (Join-Path $project 'Assets/Scenes'), (Join-Path $project 'Assets/Scripts'), (Join-Path $project 'ProjectSettings'), (Join-Path $project 'Packages'))) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
Get-ChildItem -LiteralPath (Join-Path $EngineRoot 'Binaries') -File | Where-Object Extension -in @('.exe', '.dll') | Copy-Item -Destination $bin
foreach ($dir in @('Shaders', 'Resources', 'ProjectSetting', 'Binaries/Scripting', 'Binaries/ShaderCache'))
{
    $src = Join-Path $EngineRoot $dir
    if (Test-Path -LiteralPath $src) { $dst = Join-Path $sandbox $dir; New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null; Copy-Item -LiteralPath $src -Destination $dst -Recurse }
}
$utf8 = New-Object Text.UTF8Encoding($false)
function Write-Json([string]$Path, $Value) { [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 100), $utf8) }
Write-Json (Join-Path $project 'Packages/manifest.json') @{ dependencies = @{} }
Write-Json (Join-Path $project 'ProjectSettings/ProjectSettings.json') @{ projectName = 'CodexJoints2DTests' }
Write-Json (Join-Path $project 'ProjectSettings/EditorBuildSettings.json') @{ scenes = @(@{ path = 'Assets/Scenes/Joints.scene'; enabled = $true }) }
Write-Json (Join-Path $project 'Assets/EditorSettings.json') @{ LastOpenedScenePath = '' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'joints2d_probe.cs') -Destination (Join-Path $project 'Assets/Scripts/Joint2DProbe.cs')
$nova = Join-Path $bin 'nova.exe'; $engineExe = Join-Path $bin 'NovaEngine.exe'
$editor = $null; $results = New-Object 'System.Collections.Generic.List[object]'
$playerProcess = $null;
$originalEncoding = [Console]::OutputEncoding; [Console]::OutputEncoding = $utf8
function Invoke-Editor([string[]]$Arguments, [int]$Timeout = 30)
{
    if (-not $editor -or $editor.HasExited) { throw 'Test editor is not running.' }
    $previous = $ErrorActionPreference
    try { $ErrorActionPreference = 'Continue'; $lines = @(& $nova @Arguments --pid $editor.Id --json --timeout $Timeout 2>&1); $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $previous }
    [pscustomobject]@{ ExitCode = $code; Text = (($lines | ForEach-Object { $_.ToString() }) -join "`n") }
}
function Command([string[]]$Arguments) { $r = Invoke-Editor $Arguments; if ($r.ExitCode -ne 0) { throw "nova $($Arguments -join ' '): $($r.Text)" }; ConvertFrom-Json $r.Text }
function Exec([string]$Code)
{
    $file = Join-Path $Out 'exec.cs'; [IO.File]::WriteAllText($file, $Code, $utf8)
    $r = Command @('exec', '--file', $file)
    if ($r.errors) { throw ($r | ConvertTo-Json -Depth 10) }
    $r.result
}
function Check([string]$Name, [bool]$Pass, [string]$Detail)
{
    $results.Add([pscustomobject]@{ Suite = 'joints2d'; Test = $Name; Result = $(if ($Pass) { 'PASS' } else { 'FAIL' }); Detail = $Detail })
    Write-Host "[$(if ($Pass) { 'PASS' } else { 'FAIL' })] $Name - $Detail"
}
function Start-Editor
{
    $script:editor = Start-Process -FilePath $engineExe -ArgumentList @('--project', ('"' + $project + '"'), '--no-activate', '-force-d3d11') -WorkingDirectory $bin -WindowStyle Hidden -PassThru
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $ReadyTimeoutSeconds)
    {
        $r = Invoke-Editor @('info') 1
        if ($r.ExitCode -eq 0) { $i = ConvertFrom-Json $r.Text; if ($i.project.TrimEnd('\', '/') -ne $project) { throw 'Unexpected project.' }; return }
        Start-Sleep -Milliseconds 250
    }
    throw 'Startup timed out.'
}
function Close-Editor
{
    if ($editor -and -not $editor.HasExited) { Invoke-Editor @('quit', '--force') | Out-Null; if (-not $editor.WaitForExit(10000)) { $editor.Kill(); $editor.WaitForExit() } }
    $script:editor = $null
}
function Wait-Seconds([double]$Seconds)
{
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $Seconds) { Command @('wait', '10') | Out-Null }
}
function Component([string]$Name, [string]$Type) { Command @('get', $Name, '--component', $Type) }
function Json-Argument($Value) { ($Value | ConvertTo-Json -Depth 10 -Compress).Replace('"', '\"') }
function Snapshot
{
    $data = @{}
    foreach ($pair in @(@('Hinge','HingeJoint2D'), @('Spring','SpringJoint2D'), @('Distance','DistanceJoint2D'), @('Wheel','WheelJoint2D'), @('FixedB','FixedJoint2D'), @('Slider','SliderJoint2D'), @('Limited','HingeJoint2D'), @('BreakForce','FixedJoint2D')))
    { $data[$pair[0]] = Component $pair[0] $pair[1] | ConvertTo-Json -Depth 20 -Compress }
    $data | ConvertTo-Json -Compress
}
try
{
    Start-Editor
    $sw = [Diagnostics.Stopwatch]::StartNew()
    do { Command @('wait', '20') | Out-Null; $info = Command @('info') } while ($info.compiling -and $sw.Elapsed.TotalSeconds -lt 90)
    if ($info.compiling) { throw 'Script compilation timed out.' }
    $setup = Exec 'return Joint2DProbe.Setup();'
    Command @('wait', '5') | Out-Null
    Check 'C# AddComponent requires an immediately accessible Rigidbody2D' ($setup -eq 'True,True') $setup
    Check 'Multiple joints retain separate C# identities and values' ($setup -eq 'True,True') $setup
    # exec creates objects outside the editor's Undo recording. Reload sets a valid baseline.
    Command @('scene', 'save', '--as', 'Assets/Scenes/Joints.scene') | Out-Null
    Command @('scene', 'open', 'Assets/Scenes/Joints.scene', '--force') | Out-Null
    $h = Component 'Hinge' 'HingeJoint2D'
    Check 'Infinity survives JSON serialization' ($h.breakForce -eq 'Infinity' -and $h.breakTorque -eq 'Infinity') ($h | ConvertTo-Json -Compress)
    foreach ($pair in @(@('Hinge','HingeJoint2D'), @('Spring','SpringJoint2D'), @('Distance','DistanceJoint2D'), @('Wheel','WheelJoint2D'), @('FixedB','FixedJoint2D'), @('Slider','SliderJoint2D')))
    {
        $go = Command @('get', $pair[0]); $types = @($go.components | ForEach-Object type)
        Check ($pair[1] + ' follows Rigidbody2D in the Inspector') ([array]::IndexOf($types, 'Rigidbody2D') -ge 0 -and [array]::IndexOf($types, 'Rigidbody2D') -lt [array]::IndexOf($types, $pair[1])) ($types -join ',')
    }
    $values = Json-Argument @{ motor = @{ motorSpeed = 67; maxMotorTorque = 100 } }
    Command @('set', 'Limited', '--component', 'HingeJoint2D', '--values', $values) | Out-Null
    Check 'Inspector values apply through the component JSON path' ((Component 'Limited' 'HingeJoint2D').motor.motorSpeed -eq 67) 'Motor=67'
    Command @('undo') | Out-Null
    Check 'Undo restores joint settings' ((Component 'Limited' 'HingeJoint2D').motor.motorSpeed -eq 90) 'Motor=90'
    Command @('redo') | Out-Null
    Check 'Redo restores joint settings' ((Component 'Limited' 'HingeJoint2D').motor.motorSpeed -eq 67) 'Motor=67'
    Command @('undo') | Out-Null
    Command @('create', 'empty', '--name', 'RequiredBody') | Out-Null
    Command @('add-component', 'RequiredBody', 'HingeJoint2D') | Out-Null
    $types = @((Command @('get', 'RequiredBody')).components | ForEach-Object type)
    Check 'CLI attachment also adds Rigidbody2D' ($types -contains 'Rigidbody2D' -and $types -contains 'HingeJoint2D') ($types -join ',')
    Command @('undo') | Out-Null
    $types = @((Command @('get', 'RequiredBody')).components | ForEach-Object type)
    Check 'Undo removes both required body and joint' ($types -notcontains 'Rigidbody2D' -and $types -notcontains 'HingeJoint2D') ($types -join ',')
    Command @('redo') | Out-Null
    Command @('delete', 'RequiredBody') | Out-Null
    Exec 'var g = NovaEngine.Object.Instantiate(GameObject.Find("JointGroup")); g.name = "JointGroupCopy"; return g.name;' | Out-Null
    Command @('wait', '5') | Out-Null
    $clonedA = Command @('get', 'JointGroupCopy/GroupA'); $clonedJ = Component 'JointGroupCopy/GroupB' 'FixedJoint2D'
    Check 'Clone remaps connectedBody within the copied hierarchy' ("#$($clonedJ.connectedBody)" -eq $clonedA.id) "body=#$($clonedJ.connectedBody) expected=$($clonedA.id)"
    Command @('scene', 'save', '--as', 'Assets/Scenes/Joints.scene') | Out-Null
    $before = Snapshot
    Command @('scene', 'open', 'Assets/Scenes/Joints.scene', '--force') | Out-Null
    Check 'All six joint settings survive scene save/reload' ((Snapshot) -eq $before) 'Compare component JSON'

    # Valid prefab asset + two distinct instance trees exercise the engine's actual merge path.
    $sceneFile = Join-Path $project 'Assets/Scenes/Joints.scene'
    $scene = Get-Content -LiteralPath $sceneFile -Raw | ConvertFrom-Json
    $group = @($scene.rootGameObjects | Where-Object name -eq 'JointGroup')[0]
    $prefabPath = 'Assets/JointGroup.prefab'
    Write-Json (Join-Path $project $prefabPath) @{ root = $group; layerFormat = $scene.layerFormat }
    $script:nextTestID = [uint64]7000000000000
    function Make-Instance($Node, [bool]$Root)
    {
        $copy = $Node | ConvertTo-Json -Depth 100 | ConvertFrom-Json
        $source = $copy.fileID; $script:nextTestID++; $copy.fileID = $script:nextTestID
        $copy | Add-Member -NotePropertyName prefab -NotePropertyValue @{ asset = $prefabPath; source = $source; root = $Root; overrides = @() } -Force
        $copy.children = @($Node.children | ForEach-Object { Make-Instance $_ $false })
        $copy
    }
    $one = Make-Instance $group $true; $one.name = 'PrefabOne'
    $two = Make-Instance $group $true; $two.name = 'PrefabTwo'
    $scene.rootGameObjects = @($scene.rootGameObjects) + @($one, $two)
    Write-Json $sceneFile $scene
    Command @('scene', 'open', 'Assets/Scenes/Joints.scene', '--force') | Out-Null
    foreach ($root in @('PrefabOne', 'PrefabTwo'))
    {
        $a = Command @('get', "$root/GroupA"); $j = Component "$root/GroupB" 'FixedJoint2D'
        Check ($root + ' joint connects to its own prefab instance') ("#$($j.connectedBody)" -eq $a.id) "body=#$($j.connectedBody) expected=$($a.id)"
    }
    Command @('scene', 'save') | Out-Null
    $savedScene = Get-Content -LiteralPath $sceneFile -Raw | ConvertFrom-Json
    $prefabOne = @($savedScene.rootGameObjects | Where-Object name -eq 'PrefabOne')[0]
    $prefabChild = @($prefabOne.children | Where-Object name -eq 'GroupB')[0]
    Check 'Default prefab connection is not reported as an override' (@($prefabChild.prefab.overrides | Where-Object { $_ -match '/connectedBody$' }).Count -eq 0) ($prefabChild.prefab.overrides -join ',')
    $before = Snapshot
    Command @('play') | Out-Null
    Wait-Seconds 3.4
    $report = Exec 'return Joint2DProbe.Report();' | ConvertFrom-Json
    Write-Json (Join-Path $Out 'simulation.json') $report
    Check 'World hinge swings without moving its pivot' ($report.hingePivotError -lt 0.035 -and $report.hingeTravel -gt 15) "pivot=$($report.hingePivotError) travel=$($report.hingeTravel)"
    # Angular constraints have solver slop; Box2D's stop settles about two degrees beyond the limit.
    Check 'Hinge motor obeys +/-30 degree limits' ($report.limitedAngle -le 33 -and $report.limitedAngle -gt 20) "max=$($report.limitedAngle), tolerance=3 deg"
    Check 'C# motor/limit updates apply during Play' ($report.changed -and $report.finalLimitedAngle -lt -12 -and $report.finalLimitedAngle -gt -18) "angle=$($report.finalLimitedAngle)"
    Check 'Joint recreates after Rigidbody2D body-type changes' ($report.recovered -and [math]::Abs($report.freeSpeed - 90) -lt 8) "speed=$($report.freeSpeed) deg/s"
    Check 'Spring with zero damping oscillates' ($report.springMin -lt 0.9 -and $report.springMax -gt 1.5) "min=$($report.springMin) max=$($report.springMax)"
    Check 'Critically damped spring settles without overshoot' ($report.dampedError -lt 0.06 -and $report.dampedMin -ge 0.98) "error=$($report.dampedError) min=$($report.dampedMin)"
    Check 'Distance joint preserves length between two falling bodies' ([math]::Abs($report.distance - 2) -lt 0.035 -and $report.distanceAnchorY -lt 4) "length=$($report.distance) anchorY=$($report.distanceAnchorY)"
    Check 'MaxDistanceOnly permits shortening and limits stretching' ($report.ropeMin -lt 1.2 -and $report.ropeMax -le 2.05) "min=$($report.ropeMin) max=$($report.ropeMax)"
    Check 'Fixed joint falls while preserving the relative pose' ($report.fixedError -lt 0.035 -and $report.fixedY -lt 2) "error=$($report.fixedError) y=$($report.fixedY)"
    Check 'Slider respects axis and translation limits' ($report.sliderError -lt 0.035 -and $report.sliderRange -le 1.035 -and $report.sliderRange -gt 0.8) "axis=$($report.sliderError) range=$($report.sliderRange)"
    Check 'Two wheel motors propel a suspended vehicle on the ground' ($report.wheelSpeed -lt -100 -and $report.rearWheelSpeed -lt -100 -and $report.wheelTravel -gt 0.2 -and $report.wheelAxisError -lt 0.08) "speeds=$($report.wheelSpeed),$($report.rearWheelSpeed) travel=$($report.wheelTravel) axis=$($report.wheelAxisError)"
    Check 'Force break callback runs once and removes the joint' ($report.forceBreaks -eq 1 -and $report.forceRemoved) "count=$($report.forceBreaks) removed=$($report.forceRemoved)"
    Check 'Torque break callback runs once and removes the joint' ($report.torqueBreaks -eq 1 -and $report.torqueRemoved) "count=$($report.torqueBreaks) removed=$($report.torqueRemoved)"
    Check 'Break callback receives the correct readable Joint2D instance' ($report.typed -and $report.readable) "typed=$($report.typed) readable=$($report.readable)"
    Check 'Automatic distance and slider angle are configured' ([math]::Abs($report.autoDistance - 3) -lt 0.01 -and [math]::Abs($report.autoAngle + 90) -lt 0.01) "distance=$($report.autoDistance) angle=$($report.autoAngle)"
    Exec 'var j = GameObject.Find("Multiple").GetComponents<HingeJoint2D>()[1]; NovaEngine.Object.Destroy(j); return true;' | Out-Null
    Command @('wait', '10') | Out-Null
    $remaining = Exec 'return GameObject.Find("Multiple").GetComponents<HingeJoint2D>().Length;'
    Check 'Destroy removes the requested joint among multiple components' ($remaining -eq '1') "remaining=$remaining"
    Exec 'NovaEngine.Object.Destroy(GameObject.Find("Motor").GetComponent<Rigidbody2D>()); return true;' | Out-Null
    Command @('wait', '15') | Out-Null
    Exec 'var rb = GameObject.Find("Motor").AddComponent<Rigidbody2D>(); rb.gravityScale = 0; rb.angularDamping = 0; return rb.mass;' | Out-Null
    Wait-Seconds 0.3
    $speed = Exec 'return GameObject.Find("Motor").GetComponent<HingeJoint2D>().jointSpeed.ToString(System.Globalization.CultureInfo.InvariantCulture);'
    Check 'Joint recreates after removing and adding Rigidbody2D' ([math]::Abs([double]$speed - 90) -lt 8) "speed=$speed"
    Command @('stop') | Out-Null
    Check 'Play/Stop restores settings and joints removed by breakage' ((Snapshot) -eq $before) 'Compare complete pre-Play settings'
    Command @('build', (Join-Path $Out 'Player')) | Out-Null
    $build = Command @('build-status', '--wait')
    Write-Json (Join-Path $Out 'build.json') $build
    Close-Editor
    $log = [IO.File]::ReadAllText((Join-Path $bin 'Logs/Editor.log'))
    Check 'Game build succeeds with Joint2D scenes and C# API' ($log -match '\[Build\] Build completed:' -and (Test-Path (Join-Path $Out 'Player/Project.exe')) -and (Test-Path (Join-Path $Out 'Player/NovaCore.dll'))) 'Player output and completed build log'
    Check 'Editor reports no native bridge mismatch or crash' ($log -notmatch 'size mismatch|\[CRASH\]|device removed|DEVICE_REMOVED') 'Editor.log'
    $playerDirectory = Join-Path $Out 'Player'
    $playerProcess = Start-Process -FilePath (Join-Path $playerDirectory 'Project.exe') -ArgumentList @('--no-activate', '-force-d3d11') -WorkingDirectory $playerDirectory -WindowStyle Hidden -PassThru
    if (-not $playerProcess.WaitForExit(30000)) { $playerProcess.Kill(); $playerProcess.WaitForExit(); Check 'Built game exits after its own probe completes' $false 'Timed out' }
    else { Check 'Built game exits after its own probe completes' ($playerProcess.ExitCode -eq 0) "exit=$($playerProcess.ExitCode)" }
    $playerLog = [IO.File]::ReadAllText((Join-Path $playerDirectory 'Project_Data/Binaries/Logs/Editor.log'))
    $matched = $playerLog -match 'JOINTS2D_REPORT (\{[^\r\n]+\})'
    $playerReportJson = if ($matched) { $Matches[1] } else { '' }
    Check 'Built game runs the Joint2D C# probe without a bridge error' ($matched -and $playerLog -notmatch 'size mismatch|\[CRASH\]|SEHException') 'Player Editor.log'
    if ($matched)
    {
        $playerReport = $playerReportJson | ConvertFrom-Json
        Write-Json (Join-Path $Out 'player-simulation.json') $playerReport
        Check 'Built game simulates joints and dispatches both break callbacks' ($playerReport.forceBreaks -eq 1 -and $playerReport.torqueBreaks -eq 1 -and $playerReport.typed -and $playerReport.readable -and $playerReport.hingePivotError -lt 0.035 -and $playerReport.wheelTravel -gt 0.2) 'Player physics and callback results'
    }
}
catch { Check 'Test harness completed' $false $_.Exception.Message }
finally
{
    try { Close-Editor } catch { Check 'Test editor shutdown' $false $_.Exception.Message }
    if ($playerProcess -and -not $playerProcess.HasExited) { $playerProcess.Kill(); $playerProcess.WaitForExit() }
    [Console]::OutputEncoding = $originalEncoding
    Write-Json (Join-Path $Out 'results.json') @($results.ToArray())
    Write-Json (Join-Path $Out 'engine.json') @{ root = $EngineRoot; project = $project; dllSha256 = (Get-FileHash -LiteralPath (Join-Path $bin 'NovaCore.dll')).Hash }
}
if (@($results | Where-Object Result -eq 'FAIL').Count) { exit 1 }
