# 안드로이드 (MuMu) 에서 내비게이션: 편집기의 Build And Run (nova android build --run) 으로 3D · 2D NavMesh 검사 장면을 기기에 → logcat 의 NavProbe 로그
#  - 구운 .navmesh 가 게임 데이터에 들어가는가, C# NovaEngine.AI (DllImport NovaNavigation → 엔진에 함께 넣은 패키지) 가 기기에서 도는가
#  - 3D: 가운데 벽을 돌아가는 길 · 에이전트 도착, 2D (XY): Box Collider 2D 벽을 돌아가는 길 · 도착 (z 그대로)
#  쓰기: powershell -File Tools/tests/android_nav.ps1   (먼저 python Android/build.py 로 오늘 엔진을 빌드해 둔다 — libnova.so 를 플레이어로 쓴다)
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Vm = 'NOVA Test', [switch]$KeepEmulator, [string]$PlayerLib = '')
. (Join-Path $PSScriptRoot 'common.ps1')
$script:Project = $Project
$ErrorActionPreference = 'Continue'
$Out = Join-Path $Root ("TestResults\android-nav-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $Out | Out-Null
$results = @()
function Check([string]$name, [bool]$ok, [string]$detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail }; Write-Host ("  [{0}] {1} — {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) }

$MuMu = 'D:\Program Files\Netease\MuMuPlayer\nx_main\MuMuManager.exe'
if (-not (Test-Path $MuMu)) { $MuMu = (Get-ChildItem 'C:\Program Files\Netease', 'D:\Program Files\Netease' -Recurse -Filter MuMuManager.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
$Sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$Adb = Join-Path $Sdk 'platform-tools\adb.exe'

Write-Host '[android-nav] emulator'
$all = (& $MuMu info -v all 2>&1 | Out-String) | ConvertFrom-Json
$index = $null
foreach ($p in $all.PSObject.Properties) { if ($p.Value.name -eq $Vm) { $index = $p.Name } }
if (-not $index) { Write-Host "no MuMu VM '$Vm' (run Tools/tests/android.ps1 once to create it)"; exit 1 }
$info = Start-MuMuHidden $MuMu $index   # 화면에 보이지 않게
$serial = "127.0.0.1:$($info.adb_port)"
& $Adb connect $serial | Out-Null
Check 'emulator ready' ($info.player_state -eq 'start_finished') "VM $index '$Vm', adb $serial"

# 오늘 엔진 (Android/build.py 의 결과) 을 Build And Run 의 플레이어로
# 가장 새로 빌드한 것 (기본 Android/build, --out 으로 다른 곳에 빌드했으면 그쪽) — -PlayerLib 로 고를 수도
$lib = if ($PlayerLib) { $PlayerLib } else {
    @((Join-Path $Root 'Android\build\cmake\x86_64-Release\libnova.so'), 'E:\NovaTest\AndroidCheck\cmake\x86_64-Release\libnova.so') |
        Where-Object { Test-Path $_ } | Sort-Object { (Get-Item $_).LastWriteTime } | Select-Object -Last 1 }
$playerDir = Join-Path $Root 'Android\Player\x86_64'
New-Item -ItemType Directory -Force $playerDir | Out-Null
if (Test-Path $lib) { Copy-Item $lib $playerDir -Force }
Check 'player library (Android/build.py)' (Test-Path (Join-Path $playerDir 'libnova.so')) ("{0:N1} MB, built {1}" -f ((Get-Item $lib -ErrorAction SilentlyContinue).Length / 1MB), (Get-Item $lib -ErrorAction SilentlyContinue).LastWriteTime)

$manifest = Join-Path $Project 'Packages\manifest.json'
$manifestBefore = if (Test-Path $manifest) { [IO.File]::ReadAllBytes($manifest) } else { $null }
$ebs = Join-Path $Project 'ProjectSettings\EditorBuildSettings.json'
$ebsBefore = if (Test-Path $ebs) { [IO.File]::ReadAllBytes($ebs) } else { $null }
$editorSettings = Join-Path $Project 'Assets\EditorSettings.json'
$settingsBefore = if (Test-Path $editorSettings) { [IO.File]::ReadAllBytes($editorSettings) } else { $null }
Backup-Layout
$ed = Start-TestEditor
$b = $null
try
{
    $scene = New-NavPlayerScene $Out
    Check 'test scene with baked NavMeshes (3D + 2D)' ([bool]$scene) $(if ($scene) { $scene } else { 'bake failed' })
    if ($scene)
    {
        Invoke-Nova "build-scenes set --scenes $scene" | Out-Null
        & $Adb -s $serial uninstall com.NOVATest.UIDemo 2>&1 | Out-Null   # 다른 키로 설치했던 것
        & $Adb -s $serial logcat -c
        $apk = Join-Path $Out 'NavTest.apk'
        $st = Invoke-NovaJson "android build --out `"$apk`" --run --device $serial"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Start-Sleep -Milliseconds 1000; $b = Invoke-NovaJson 'android build-status' } while ($sw.Elapsed.TotalSeconds -lt 600 -and $b -and $b.running)
        Check 'Build And Run (APK on the device)' ($st -and $b -and $b.success) $(if ($b) { if ($b.success) { "{0:N1} MB, {1:N1} s" -f ($b.bytes / 1MB), $b.seconds } else { $b.error } } else { 'no status' })
        $names = @()
        if (Test-Path $apk) { Add-Type -AssemblyName System.IO.Compression.FileSystem; $z = [IO.Compression.ZipFile]::OpenRead($apk); $names = @($z.Entries | ForEach-Object { $_.FullName }); $z.Dispose() }
        $navFiles = @($names | Where-Object { $_ -match '\.navmesh$' })
        Check 'baked .navmesh files are packed' ($navFiles.Count -eq 2) ($navFiles -join ', ')
    }
}
finally
{
    Write-Host "  $(Stop-TestEditor $ed)"; Restore-Layout
    Remove-NavPlayerScene
    if ($manifestBefore) { [IO.File]::WriteAllBytes($manifest, $manifestBefore) }
    if ($ebsBefore) { [IO.File]::WriteAllBytes($ebs, $ebsBefore) }
    if ($settingsBefore) { [IO.File]::WriteAllBytes($editorSettings, $settingsBefore) }
}

if ($b -and $b.success)
{
    Write-Host '[android-nav] on the device'
    $sw = [Diagnostics.Stopwatch]::StartNew(); $done = $null
    while ($sw.Elapsed.TotalSeconds -lt 60 -and -not $done)
    {
        Start-Sleep -Milliseconds 500
        $done = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NavProbe done' | Select-Object -Last 1)
    }
    $log = @(& $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F)
    $log | Set-Content -Encoding utf8 (Join-Path $Out 'logcat_nav.txt')
    $r = Test-NavPlayerLog ($log | ForEach-Object { "$_" }) 'Android'
    Check 'paths on the device (3D around the wall, 2D around the Box Collider 2D)' $r.PathOk $(if ($r.Start) { $r.Start } else { 'no "NavProbe start" (logcat_nav.txt)' })
    Check 'agents walk there (3D on the floor, 2D on X/Y with z kept)' $r.WalkOk $(if ($r.Done) { $r.Done } else { 'no "NavProbe done" in 60 s' })
    $crash = @($log | Select-String 'FATAL|signal \d')
    Check 'no crash' ($crash.Count -eq 0) $(if ($crash.Count) { $crash[0].Line } else { 'logcat clean' })
    & $Adb -s $serial shell am force-stop com.NOVATest.UIDemo | Out-Null
}

if (-not $KeepEmulator) { & $MuMu control -v $index shutdown 2>&1 | Out-Null }
$results | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
$fail = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
Write-Host ("{0} passed, {1} failed  → {2}" -f ($results.Count - $fail), $fail, $Out)
exit $fail
