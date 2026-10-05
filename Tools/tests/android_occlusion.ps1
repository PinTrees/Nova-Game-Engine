# 안드로이드 (OpenGL ES) 오클루전 컬링 검사 (MuMu 플레이어 · 창 없이): docs/OCCLUSION_CULLING.md
#   powershell Tools/tests/android_occlusion.ps1 [-Project 테스트프로젝트] [-Vm "NOVA Test"] [-KeepEmulator]
#  1) 에디터: GLES 셰이더 + 검사 씬 (큰 벽 뒤 상자 100 · 앞 상자 3 · 캐릭터 2 · 나무) → 게임 데이터 + DX11 기준 그림
#  2) APK (Android/build.py) → 설치
#  3) 기기의 scene 검사 (화면 없이 30 프레임) 를 오클루전 켬 · 끔으로 → NOVA_TEST 줄의 occlusion 통계 + 두 그림이 같은지 · DX11 과 같은지
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Vm = 'NOVA Test', [switch]$KeepEmulator, [string]$Scene = 'Assets\Scenes\AndroidOcclusion.scene')
. (Join-Path $PSScriptRoot 'common.ps1')
$script:Project = $Project
$ErrorActionPreference = 'Continue'
$Out = Join-Path $Root ("TestResults\android-occlusion-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $Out | Out-Null
$results = @()
function Check([string]$name, [bool]$ok, [string]$detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail }; Write-Host ("  [{0}] {1} — {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) }

$MuMu = 'D:\Program Files\Netease\MuMuPlayer\nx_main\MuMuManager.exe'
if (-not (Test-Path $MuMu)) { $MuMu = (Get-ChildItem 'C:\Program Files\Netease', 'D:\Program Files\Netease' -Recurse -Filter MuMuManager.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
$Sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$Adb = Join-Path $Sdk 'platform-tools\adb.exe'
function MuMu([string[]]$a) { (& $MuMu @a 2>&1 | Out-String) }

# ---- 에뮬레이터 (android.ps1 과 같은 검사 전용 VM)
Write-Host '[android-occlusion] emulator'
$all = MuMu @('info', '-v', 'all') | ConvertFrom-Json
$index = $null
foreach ($p in $all.PSObject.Properties) { if ($p.Value.name -eq $Vm) { $index = $p.Name } }
if (-not $index) { Check 'emulator' $false "no MuMu VM '$Vm' (run Tools/tests/android.ps1 once to create it)"; exit 1 }
$info = Start-MuMuHidden $MuMu $index   # 화면에 보이지 않게 (common.ps1)
$serial = "127.0.0.1:$($info.adb_port)"
& $Adb connect $serial | Out-Null
$abi = (& $Adb -s $serial shell getprop ro.product.cpu.abi | Out-String).Trim()
Check 'emulator ready' ($info.player_state -eq 'start_finished') ("VM {0} '{1}', adb {2}, abi {3}" -f $index, $Vm, $serial, $abi)

# ---- 1) 에디터: 셰이더 · 검사 씬 · 게임 데이터 · DX11 기준
Write-Host '[android-occlusion] shaders + scene + DX11 reference'
$assets = Join-Path $Root 'Android\build\assets\Shaders'
$gameDir = Join-Path $Root 'Android\build\assets\game'
Backup-Layout
$editorSettings = Join-Path $Project 'Assets\EditorSettings.json'
$editorSettingsBefore = if (Test-Path $editorSettings) { [IO.File]::ReadAllBytes($editorSettings) } else { $null }
$ed = Start-TestEditor
try
{
    $s = Invoke-NovaJson "android shaders --out `"$assets`""
    $occJson = Join-Path $assets '57. OcclusionCulling.json'
    Check 'GLES shaders exported (occlusion kernels included)' ($s -and $s.written -gt 0 -and (Test-Path $occJson)) $(if ($s) { "effects $($s.written)/$($s.effects), passes failed $($s.passesFailed)/$($s.passes)" } else { 'no result' })
    # occlusion · occlusiongl · occlusionvk 와 같은 씬: 큰 벽 (24 x 5 m) 뒤 상자 100, 앞 상자 3, 벽 뒤 캐릭터 · 앞 캐릭터, 벽 뒤 나무
    Invoke-Nova 'scene new --force' | Out-Null
    Invoke-Nova 'create plane --name Floor --position 0,0,0 --scale 6,1,6' | Out-Null
    Invoke-Nova 'create cube --name Wall --position 0,2.5,0 --scale 24,5,0.5' | Out-Null
    $cs = Join-Path $Out 'cubes.cs'
    'for (int i = 0; i < 100; i++) { var g = GameObject.CreatePrimitive(PrimitiveType.Cube); g.name = "H" + i; g.transform.position = new Vector3((i % 10) * 2 - 9, 0.5f, 2 + (i / 10) * 1.5f); } return 100;' | Set-Content -Encoding utf8 $cs
    Invoke-Nova "exec --file $cs" | Out-Null
    foreach ($f in @(@('F1', '-3,0.5,-4'), @('F2', '0,0.5,-5'), @('F3', '3,0.5,-4'))) { Invoke-Nova ("create cube --name {0} --position {1}" -f $f[0], $f[1]) | Out-Null }
    Invoke-Nova 'create character --name HidA' | Out-Null; Invoke-Nova 'set HidA --position 3,0,6' | Out-Null
    Invoke-Nova 'create character --name SeenC' | Out-Null; Invoke-Nova 'set SeenC --position -6,0,-4' | Out-Null
    Invoke-Nova 'create tree --name TreeA --position -3,0,24 --scale 0.25,0.25,0.25' | Out-Null
    Invoke-Nova 'set "Main Camera" --position 0,1.5,-14 --rotation 0,0,0' | Out-Null
    Invoke-Nova "scene save --as $($Scene -replace '\\', '/')" | Out-Null
    Invoke-Nova 'wait 30' | Out-Null
    Remove-Item -Recurse -Force $gameDir -ErrorAction SilentlyContinue
    $x = Invoke-NovaJson "android export --out `"$(Split-Path $assets)`" --scenes `"$Scene`""
    Check 'game data exported' ($x -and $x.files -gt 0) $(if ($x) { "$($x.files) files, $([math]::Round($x.bytes / 1MB, 1)) MB" } else { 'no result' })
    Invoke-NovaJson "android reference --out `"$(Join-Path $Out 'occ_DirectX11.png')`" --width 960 --height 540 --frames 30" | Out-Null
    Check 'DX11 reference' (Test-Path (Join-Path $Out 'occ_DirectX11.png')) 'occ_DirectX11.png (960x540, player order)'
}
finally
{
    Write-Host "  $(Stop-TestEditor $ed)"; Restore-Layout
    if ($editorSettingsBefore) { [IO.File]::WriteAllBytes($editorSettings, $editorSettingsBefore) }
}

# ---- 2) APK
Write-Host '[android-occlusion] build APK'
$py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
$py | Set-Content -Encoding utf8 (Join-Path $Out 'build_apk.txt')
$apk = Join-Path $Root 'Android\build\nova.apk'
Check 'APK build' ($py -match 'apk .*nova\.apk') (($py -split "`n" | Where-Object { $_ -match '^(built|apk|FAILED)' }) -join '; ')
$inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String).Trim()
Check 'install' ($inst -match 'Success') ($inst -split "`n" | Select-Object -Last 1)

# ---- 3) 기기: 오클루전 켬 · 끔
function SceneRun([string]$name, [string]$extra)
{
    & $Adb -s $serial logcat -c
    & $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
    & $Adb -s $serial shell "am start -W -n com.nova.engine/android.app.NativeActivity -e test scene -e size 960x540 -e frames 30 $extra" | Out-Null
    $line = $null
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 120 -and -not $line)
    {
        Start-Sleep -Milliseconds 500
        $line = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_TEST (\{.*\})' | Select-Object -Last 1)
    }
    & $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out "logcat_$name.txt")
    if (-not $line) { Check "$name scene on device" $false "no NOVA_TEST line in 120 s (logcat_$name.txt)"; return $null }
    $j = $line.Matches[0].Groups[1].Value | ConvertFrom-Json
    Check "$name scene on device" ([bool]$j.ok) ("{0}, load {1} ms, draw {2} ms {3}" -f $j.device, $j.loadMs, $j.drawMs, $j.error)
    if (-not $j.ok) { return $null }
    $bmp = Join-Path $Out "${name}_GLES.bmp"
    & $Adb -s $serial pull $j.image $bmp | Out-Null
    [pscustomobject]@{ Json = $j; Image = $bmp }
}

Write-Host '[android-occlusion] device'
$on = SceneRun 'occ_on' ''
$off = SceneRun 'occ_off' '-e occlusion off'
if ($on)
{
    $o = $on.Json.occlusion
    $g = $o.game
    Check 'GLES: supported, boxes · character · tree · shadow casters behind the wall culled' ($o.supported -and $g.active -and $g.culled -ge 98 -and $g.queries -ge 2 -and $g.queriesHidden -ge 1 -and $g.instancesCulled -ge 1 -and $g.shadowCulled -gt 0) ("supported {0}, culled {1}/{2}, boxes {3} (hidden {4}), tree instances culled {5}, shadow casters culled {6}/{7}" -f $o.supported, $g.culled, $g.tested, $g.queries, $g.queriesHidden, $g.instancesCulled, $g.shadowCulled, $g.shadowTested)
    $gpu = @(Get-Content (Join-Path $Out 'logcat_occ_on.txt') -ErrorAction SilentlyContinue | Select-String 'GPU-driven drawing|Occlusion\]|GLES.*(OcclusionCulling|fail)')
    $gpu | ForEach-Object { $_.Line } | Set-Content -Encoding utf8 (Join-Path $Out 'occlusion_log.txt')
}
if ($on -and $off)
{
    $c = [NovaImageCompare]::Compare($off.Image, $on.Image, (Join-Path $Out 'on_off_diff.png'))
    Check 'GLES: same picture with occlusion on and off' ($c -and $c[1] -lt 0.5 -and $c[2] -lt 0.1) $(if ($c) { 'max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'size differs' })
    Check 'GLES: draw time on / off (report)' $true ("on {0} ms, off {1} ms per frame (960x540, 30 frames)" -f $on.Json.drawMs, $off.Json.drawMs)
}
if ($on)
{
    $ref = Join-Path $Out 'occ_DirectX11.png'
    $c = if (Test-Path $ref) { [NovaImageCompare]::Compare($ref, $on.Image, (Join-Path $Out 'on_dx_diff.png')) } else { $null }
    # 기기는 플레이어라 30 프레임 동안 캐릭터 Idle 이 조금 움직인다 → 캐릭터 둘레만 다를 수 있다 (android.ps1 의 models 와 같은 기준)
    Check 'GLES = DX11 (occlusion on)' ($c -and $c[1] -lt 3.0 -and $c[2] -lt 6.0) $(if ($c) { 'max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'reference or size missing' })
}
$crash = @(Get-Content (Join-Path $Out 'logcat_occ_on.txt'), (Join-Path $Out 'logcat_occ_off.txt') -ErrorAction SilentlyContinue | Select-String 'FATAL|signal \d')
Check 'no crash' ($crash.Count -eq 0) $(if ($crash.Count) { $crash[0].Line } else { 'logcat clean' })

if (-not $KeepEmulator) { MuMu @('control', '-v', $index, 'shutdown') | Out-Null }

$results | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
$fail = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
Write-Host ("{0} passed, {1} failed  → {2}" -f ($results.Count - $fail), $fail, $Out)
exit $fail
