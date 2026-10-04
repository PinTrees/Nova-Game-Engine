# 안드로이드 (MuMu · OpenGL ES) 도시 장면 성능: 오클루전 컬링 켬 · 끔 (docs/OCCLUSION_CULLING.md)
#   powershell Tools/tests/android_city_perf.ps1 [-Project 테스트프로젝트] [-Vm "NOVA Test"] [-KeepEmulator] [-Runs 3] [-Profile] [-SkipBuild]
#   -SkipBuild: 에디터의 장면 · 게임 데이터 단계를 건너뛴다 (지난번 assets 그대로 — 엔진 코드만 바꿔 다시 잴 때)
#   -Profile: 오클루전 켬으로 한 번 더 (-e profile on) — Profiler 구간 · GL 호출 수를 perf.json 의 profile 에 (그리기 CPU 진단)
#  1) 에디터: GLES 셰이더 + 도시 장면 (Tools/tests/city_scene.py — 렌더러 2222, 사람 16, 나무 36), Main Camera = 큰길 눈높이 → 게임 데이터 + DX11 기준 그림
#  2) APK → 설치
#  3) 기기의 scene 검사 (화면 없이 1280x720, 예열 60 + 잰 120 프레임) 를 켬 · 끔 번갈아 Runs 번 — 프레임 · CPU 시간 중앙값, 가려진 수, 같은 그림
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Vm = 'NOVA Test', [switch]$KeepEmulator, [int]$Runs = 3, [string]$Scene = 'Assets\Scenes\AndroidCity.scene', [switch]$Profile, [switch]$SkipBuild)
. (Join-Path $PSScriptRoot 'common.ps1')
$script:Project = $Project
$ErrorActionPreference = 'Continue'
$Out = Join-Path $Root ("TestResults\android-city-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $Out | Out-Null
$results = @()
function Check([string]$name, [bool]$ok, [string]$detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail }; Write-Host ("  [{0}] {1} — {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) }

$MuMu = 'D:\Program Files\Netease\MuMuPlayer\nx_main\MuMuManager.exe'
if (-not (Test-Path $MuMu)) { $MuMu = (Get-ChildItem 'C:\Program Files\Netease', 'D:\Program Files\Netease' -Recurse -Filter MuMuManager.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
$Sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$Adb = Join-Path $Sdk 'platform-tools\adb.exe'
function MuMu([string[]]$a) { (& $MuMu @a 2>&1 | Out-String) }

# ---- 에뮬레이터 (android.ps1 의 검사 전용 VM)
Write-Host '[android-city] emulator'
$all = MuMu @('info', '-v', 'all') | ConvertFrom-Json
$index = $null
foreach ($p in $all.PSObject.Properties) { if ($p.Value.name -eq $Vm) { $index = $p.Name } }
if (-not $index) { Check 'emulator' $false "no MuMu VM '$Vm' (run Tools/tests/android.ps1 once to create it)"; exit 1 }
$info = MuMu @('info', '-v', $index) | ConvertFrom-Json
if (-not $info.is_android_started) { MuMu @('control', '-v', $index, 'launch') | Out-Null }
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 180)
{
    MuMu @('control', '-v', $index, 'hide_window') | Out-Null
    $info = MuMu @('info', '-v', $index) | ConvertFrom-Json
    if ($info.player_state -eq 'start_finished' -and $info.adb_port) { break }
    Start-Sleep -Seconds 2
}
$serial = "127.0.0.1:$($info.adb_port)"
& $Adb connect $serial | Out-Null
# 막 켠 VM 은 adb 가 잠시 offline — ABI 를 읽을 때까지 기다린다 (비면 APK 빌드의 --abi 가 비어 실패했다)
$abi = ''
$sw2 = [Diagnostics.Stopwatch]::StartNew()
while (-not $abi -and $sw2.Elapsed.TotalSeconds -lt 60)
{
    & $Adb connect $serial 2>&1 | Out-Null
    $abi = (& $Adb -s $serial shell getprop ro.product.cpu.abi 2>$null | Out-String).Trim()
    if (-not $abi) { Start-Sleep -Seconds 1 }
}
Check 'emulator ready' ($info.player_state -eq 'start_finished' -and $abi) ("VM {0} '{1}', adb {2}, abi {3}" -f $index, $Vm, $serial, $abi)

# ---- 1) 에디터: 셰이더 · 도시 장면 · 게임 데이터 · DX11 기준
if (-not $SkipBuild)
{
Write-Host '[android-city] city scene'
$assets = Join-Path $Root 'Android\build\assets\Shaders'
$gameDir = Join-Path $Root 'Android\build\assets\game'
$gen = Join-Path $Out 'city'
& python (Join-Path $PSScriptRoot 'city_scene.py') $Project $gen | Out-Null
Backup-Layout
$editorSettings = Join-Path $Project 'Assets\EditorSettings.json'
$editorSettingsBefore = if (Test-Path $editorSettings) { [IO.File]::ReadAllBytes($editorSettings) } else { $null }
$ed = Start-TestEditor
try
{
    $s = Invoke-NovaJson "android shaders --out `"$assets`""
    Check 'GLES shaders exported' ($s -and $s.written -gt 0) $(if ($s) { "effects $($s.written)/$($s.effects)" } else { 'no result' })
    Invoke-Nova 'scene new --force' | Out-Null
    Invoke-Nova 'create plane --name Ground --position 0,-0.05,200 --scale 50,1,50' | Out-Null
    Invoke-Nova "exec --file `"$(Join-Path $gen 'city.cs')`"" | Out-Null
    Invoke-Nova "batch `"$(Join-Path $gen 'city_mats.txt')`" --keep-going" | Out-Null
    $k = 0
    foreach ($p in @('-5.5,0.2,8', '5.6,0.2,14', '-5.8,0.2,30', '5.5,0.2,52', '-5.6,0.2,75', '5.7,0.2,96', '-5.5,0.2,130', '5.6,0.2,160',
                     '-54,0.2,40', '54,0.2,70', '-108,0.2,120', '108,0.2,160', '-54,0.2,200', '54,0.2,240', '-162,0.2,300', '162,0.2,330'))
    {
        Invoke-Nova "create character --name P$k" | Out-Null
        Invoke-Nova "set P$k --position $p --rotation 0,$(($k * 47) % 360),0" | Out-Null
        $k++
    }
    $trees = @('-6.4,0,20', '6.4,0,46', '-6.4,0,74', '6.4,0,100', '-6.4,0,128', '6.4,0,154')
    foreach ($cx in -162, -108, -54, 54, 108, 162) { foreach ($cz in 30, 110, 190, 270, 350) { $trees += ('{0},0,{1}' -f ($cx + 6.4), $cz) } }
    $k = 0
    foreach ($p in $trees) { Invoke-Nova "create tree --name T$k --position $p --scale 0.35,0.35,0.35" | Out-Null; $k++ }
    Invoke-Nova 'set "Main Camera" --position 0,2.2,-14 --rotation -1.3,0,0' | Out-Null
    Invoke-Nova "scene save --as $($Scene -replace '\\', '/')" | Out-Null
    Invoke-Nova 'wait 30' | Out-Null
    Remove-Item -Recurse -Force $gameDir -ErrorAction SilentlyContinue
    $x = Invoke-NovaJson "android export --out `"$(Split-Path $assets)`" --scenes `"$Scene`""
    Check 'game data exported' ($x -and $x.files -gt 0) $(if ($x) { "$($x.files) files, $([math]::Round($x.bytes / 1MB, 1)) MB" } else { 'no result' })
    Invoke-NovaJson "android reference --out `"$(Join-Path $Out 'city_DirectX11.png')`" --width 1280 --height 720 --frames 30" | Out-Null
}
finally
{
    Write-Host "  $(Stop-TestEditor $ed)"; Restore-Layout
    if ($editorSettingsBefore) { [IO.File]::WriteAllBytes($editorSettings, $editorSettingsBefore) }
}

}

# ---- 2) APK
Write-Host '[android-city] build APK'
$py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
$py | Set-Content -Encoding utf8 (Join-Path $Out 'build_apk.txt')
$apk = Join-Path $Root 'Android\build\nova.apk'
Check 'APK build' ($py -match 'apk .*nova\.apk') (($py -split "`n" | Where-Object { $_ -match '^(built|apk|FAILED)' }) -join '; ')
$inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String).Trim()
Check 'install' ($inst -match 'Success') ($inst -split "`n" | Select-Object -Last 1)

# ---- 3) 기기: 켬 · 끔 번갈아
function SceneRun([string]$name, [string]$extra)
{
    & $Adb -s $serial logcat -c
    & $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
    & $Adb -s $serial shell "am start -W -n com.nova.engine/android.app.NativeActivity -e test scene -e size 1280x720 -e warmup 60 -e frames 120 $extra" | Out-Null
    $line = $null
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 180 -and -not $line)
    {
        Start-Sleep -Milliseconds 500
        $line = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_TEST \{.*"image":"([^"]*)"' | Select-Object -Last 1)
    }
    & $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out "logcat_$name.txt")
    if (-not $line) { return $null }
    # logcat 은 한 줄 1024 자에서 잘린다 → 앱이 쓴 결과 파일 (그림과 같은 폴더의 result_scene.json) 을 받는다
    $remote = ($line.Matches[0].Groups[1].Value -replace '/[^/]*$', '') + '/result_scene.json'
    $local = Join-Path $Out "result_$name.json"
    & $Adb -s $serial pull $remote $local 2>&1 | Out-Null
    if (-not (Test-Path $local)) { return $null }
    $j = Get-Content $local -Raw | ConvertFrom-Json
    if (-not $j.ok) { return $null }
    $bmp = Join-Path $Out "${name}_GLES.bmp"
    & $Adb -s $serial pull $j.image $bmp | Out-Null
    [pscustomobject]@{ Json = $j; Image = $bmp }
}
Write-Host '[android-city] device'
$on = @(); $off = @()
for ($r = 0; $r -lt $Runs; $r++)
{
    $a = SceneRun "on$r" ''
    $b = SceneRun "off$r" '-e occlusion off'
    if ($a) { $on += $a }
    if ($b) { $off += $b }
    Write-Host ("  run {0}: on {1} ms (cpu {2}), off {3} ms (cpu {4})" -f $r, $a.Json.drawMs, $a.Json.cpuMs, $b.Json.drawMs, $b.Json.cpuMs)
}
$prof = $null
if ($Profile)
{
    $p = SceneRun 'profile' '-e profile on'
    if ($p)
    {
        $prof = [pscustomobject]@{ cpuMs = $p.Json.cpuMs; drawMs = $p.Json.drawMs; gl = $p.Json.gl; scopes = $p.Json.scopes }
        Write-Host ("  profile: cpu {0} ms, gl {1}" -f $p.Json.cpuMs, ($p.Json.gl | ConvertTo-Json -Compress))
        foreach ($sc in $p.Json.scopes) { Write-Host ("    {0}{1} {2:N3} ms" -f ('  ' * $sc.d), $sc.n, $sc.ms) }
    }
}
function Med($list, [string]$f) { $v = @($list | ForEach-Object { [double]$_.Json.$f } | Sort-Object); if ($v.Count) { $v[[math]::Floor($v.Count / 2)] } else { -1 } }
Check 'city scene runs on the device (occlusion on · off)' ($on.Count -eq $Runs -and $off.Count -eq $Runs) ("{0}, {1}/{2} on, {3}/{2} off" -f $(if ($on.Count) { $on[0].Json.device } else { '?' }), $on.Count, $Runs, $off.Count)
if ($on.Count -and $off.Count)
{
    $g = $on[-1].Json.occlusion.game
    Check 'GLES: city renderers · people · trees culled' ($on[-1].Json.occlusion.supported -and $g.active -and $g.culled -ge 1500) ("culled {0}/{1}, people {2}/{3}, tree instances {4}/{5}, shadow casters {6}/{7}" -f $g.culled, $g.tested, $g.queriesHidden, $g.queries, $g.instancesCulled, $g.instancesTested, $g.shadowCulled, $g.shadowTested)
    $c = [NovaImageCompare]::Compare($off[-1].Image, $on[-1].Image, (Join-Path $Out 'on_off_diff.png'))
    Check 'GLES: same picture with occlusion on and off' ($c -and $c[1] -lt 0.5) $(if ($c) { 'max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'size differs' })
    $dOn = Med $on 'drawMs'; $dOff = Med $off 'drawMs'; $cOn = Med $on 'cpuMs'; $cOff = Med $off 'cpuMs'
    Check 'GLES: frame time (median of runs, 1280x720)' ($dOn -gt 0 -and $dOff -gt 0) ("off {0:N2} ms → on {1:N2} ms ({2:+0;-0}%), CPU off {3:N2} → on {4:N2} ms" -f $dOff, $dOn, (($dOn / $dOff - 1) * 100), $cOff, $cOn)
    [pscustomobject]@{ drawOn = $dOn; drawOff = $dOff; cpuOn = $cOn; cpuOff = $cOff; device = $on[0].Json.device; occlusion = $on[-1].Json.occlusion; gl = $on[-1].Json.gl; profile = $prof } | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $Out 'perf.json')
}
$crash = @(Get-ChildItem $Out -Filter 'logcat_*.txt' | Get-Content | Select-String 'FATAL|signal \d')
Check 'no crash' ($crash.Count -eq 0) $(if ($crash.Count) { $crash[0].Line } else { 'logcat clean' })

if (-not $KeepEmulator) { MuMu @('control', '-v', $index, 'shutdown') | Out-Null }
$results | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
$fail = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
Write-Host ("{0} passed, {1} failed  → {2}" -f ($results.Count - $fail), $fail, $Out)
exit $fail
