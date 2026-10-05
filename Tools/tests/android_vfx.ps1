# 안드로이드 (OpenGL ES 3.2 compute) Visual Effect Graph 검사 (MuMu 플레이어 · 창 없이): docs/VFX_GRAPH.md
#   powershell Tools/tests/android_vfx.ps1 [-Project 테스트프로젝트] [-Vm "NOVA Test"] [-KeepEmulator]
#  1) 에디터: GLES 셰이더 (58. VFX.fx 포함) + 검사 씬 (마법진 · 불꽃놀이 견본) → 게임 데이터 (.vfx 에셋 포함) + DX11 기준 그림
#  2) APK (Android/build.py) → 설치
#  3) 기기의 scene 검사 (화면 없이) → NOVA_TEST 줄의 vfx 통계 (시스템마다 살아 있는 수 — GPU Event 사슬 포함) + 그림
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Vm = 'NOVA Test', [switch]$KeepEmulator, [string]$Scene = 'Assets\Scenes\AndroidVfx.scene')
. (Join-Path $PSScriptRoot 'common.ps1')
$script:Project = $Project
$ErrorActionPreference = 'Continue'
$Out = Join-Path $Root ("TestResults\android-vfx-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $Out | Out-Null
$results = @()
function Check([string]$name, [bool]$ok, [string]$detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail }; Write-Host ("  [{0}] {1} — {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) }

$MuMu = 'D:\Program Files\Netease\MuMuPlayer\nx_main\MuMuManager.exe'
if (-not (Test-Path $MuMu)) { $MuMu = (Get-ChildItem 'C:\Program Files\Netease', 'D:\Program Files\Netease' -Recurse -Filter MuMuManager.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
$Sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$Adb = Join-Path $Sdk 'platform-tools\adb.exe'
function MuMu([string[]]$a) { (& $MuMu @a 2>&1 | Out-String) }

# ---- 에뮬레이터 (android.ps1 과 같은 검사 전용 VM)
Write-Host '[android-vfx] emulator'
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
$abi = ''
for ($i = 0; $i -lt 30 -and -not $abi; $i++) { $abi = (& $Adb -s $serial shell getprop ro.product.cpu.abi 2>$null | Out-String).Trim(); if (-not $abi) { Start-Sleep -Seconds 2; & $Adb connect $serial | Out-Null } }
Check 'emulator ready' ($info.player_state -eq 'start_finished' -and $abi) ("VM {0} '{1}', adb {2}, abi {3}" -f $index, $Vm, $serial, $abi)
if (-not $abi) { exit 1 }

# ---- 1) 에디터: 셰이더 · 검사 씬 · 게임 데이터 · DX11 기준
Write-Host '[android-vfx] shaders + scene + DX11 reference'
$assets = Join-Path $Root 'Android\build\assets\Shaders'
$gameDir = Join-Path $Root 'Android\build\assets\game'
Backup-Layout
$editorSettings = Join-Path $Project 'Assets\EditorSettings.json'
$editorSettingsBefore = if (Test-Path $editorSettings) { [IO.File]::ReadAllBytes($editorSettings) } else { $null }
$ed = Start-TestEditor
try
{
    Invoke-Nova 'autosave discard' | Out-Null
    $s = Invoke-NovaJson "android shaders --out `"$assets`""
    $vfxJson = Join-Path $assets '58. VFX.json'
    Check 'GLES shaders exported (VFX kernels included)' ($s -and $s.written -gt 0 -and (Test-Path $vfxJson)) $(if ($s) { "effects $($s.written)/$($s.effects), passes failed $($s.passesFailed)/$($s.passes)" } else { 'no result' })
    Invoke-Nova 'scene new --force' | Out-Null
    Invoke-Nova 'set "Directional Light" Light.intensity=0.05' | Out-Null
    Invoke-Nova 'set "Main Camera" --position 0,5,-12 --rotation 18,0,0 Camera.backgroundType=1 Camera.backgroundColor=[0.004,0.006,0.016,1]' | Out-Null
    Invoke-Nova 'vfx new Assets/VFX/Test/AndroidCircle.vfx --template "Magic Circle" --overwrite' | Out-Null
    Invoke-Nova 'vfx new Assets/VFX/Test/AndroidFireworks.vfx --template Fireworks --overwrite' | Out-Null
    Invoke-Nova 'create visual-effect --asset Assets/VFX/Test/AndroidCircle.vfx --name Circle --position 0,0,0' | Out-Null
    Invoke-Nova 'create visual-effect --asset Assets/VFX/Test/AndroidFireworks.vfx --name Fireworks --position 0,0,10' | Out-Null
    Invoke-Nova "scene save --as $($Scene -replace '\\', '/')" | Out-Null
    # 화면 밖 이펙트는 시뮬레이션을 쉰다 (Culling = Simulate When Visible) → 기준 그림 전에 Scene 뷰 카메라로 비춰 돌린다
    Invoke-Nova 'camera --position 0,5,-12 --target 0,1.2,4' | Out-Null
    Invoke-Nova 'wait 30' | Out-Null
    Remove-Item -Recurse -Force $gameDir -ErrorAction SilentlyContinue
    $x = Invoke-NovaJson "android export --out `"$(Split-Path $assets)`" --scenes `"$Scene`""
    $vfxFiles = @(Get-ChildItem $gameDir -Recurse -Filter *.vfx -ErrorAction SilentlyContinue)
    Check 'game data exported (.vfx assets follow the scene)' ($x -and $x.files -gt 0 -and $vfxFiles.Count -ge 2) $(if ($x) { "$($x.files) files, .vfx $($vfxFiles.Count)" } else { 'no result' })
    Invoke-NovaJson "android reference --out `"$(Join-Path $Out 'vfx_DirectX11.png')`" --width 960 --height 540 --frames 120" | Out-Null
}
finally
{
    Write-Host "  $(Stop-TestEditor $ed)"; Restore-Layout
    if ($editorSettingsBefore) { [IO.File]::WriteAllBytes($editorSettings, $editorSettingsBefore) }
}

# ---- 2) APK
Write-Host '[android-vfx] build APK'
$py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
$py | Set-Content -Encoding utf8 (Join-Path $Out 'build_apk.txt')
$apk = Join-Path $Root 'Android\build\nova.apk'
Check 'APK build' ($py -match 'apk .*nova\.apk') (($py -split "`n" | Where-Object { $_ -match '^(built|apk|FAILED)' }) -join '; ')
$inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String).Trim()
Check 'install' ($inst -match 'Success') ($inst -split "`n" | Select-Object -Last 1)

# ---- 3) 기기: 예열 2000 프레임 (화면 없는 검사는 프레임이 짧다 — 몇 초 동안 로켓이 올라 터져 GPU Event 가 돈다) + 60 프레임
Write-Host '[android-vfx] device'
& $Adb -s $serial logcat -c
& $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
& $Adb -s $serial shell "am start -W -n com.nova.engine/android.app.NativeActivity -e test scene -e size 960x540 -e warmup 2000 -e frames 60" | Out-Null
$line = $null
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 180 -and -not $line)
{
    Start-Sleep -Milliseconds 500
    $line = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_TEST \{.*"image":"([^"]*)"' | Select-Object -Last 1)
}
& $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out 'logcat_vfx.txt')
# logcat 은 한 줄 1024 자에서 잘린다 → 앱이 쓴 결과 파일 (그림과 같은 폴더의 result_scene.json)
$j = $null
if ($line)
{
    $remote = ($line.Matches[0].Groups[1].Value -replace '/[^/]*$', '') + '/result_scene.json'
    $local = Join-Path $Out 'result_scene.json'
    & $Adb -s $serial pull $remote $local 2>&1 | Out-Null
    if (Test-Path $local) { $j = Get-Content -Raw $local | ConvertFrom-Json }
}
Check 'scene on device' ($j -and [bool]$j.ok) $(if ($j) { "{0}, load {1} ms, draw {2} ms {3}" -f $j.device, $j.loadMs, $j.drawMs, $j.error } else { 'no NOVA_TEST line / result file (logcat_vfx.txt)' })
if ($j -and $j.ok)
{
    $v = $j.vfx
    function SysAlive([string]$obj, [string]$name) { (($v.effects | Where-Object { $_.object -eq $obj }).systems | Where-Object { $_.name -eq $name }).alive }
    Check 'GLES: VFX compute (Magic Circle systems alive)' ($v.gpu -and (SysAlive 'Circle' 'Outer Ring') -gt 1000 -and (SysAlive 'Circle' 'Pillar') -gt 100) ("gpu {0}, Outer Ring {1}, Pillar {2}, error '{3}'" -f $v.gpu, (SysAlive 'Circle' 'Outer Ring'), (SysAlive 'Circle' 'Pillar'), $v.error)
    Check 'GLES: GPU events chain (Explosion, Crackle)' ((SysAlive 'Fireworks' 'Explosion') -gt 100) ("Rocket {0}, Explosion {1}, Crackle {2}" -f (SysAlive 'Fireworks' 'Rocket'), (SysAlive 'Fireworks' 'Explosion'), (SysAlive 'Fireworks' 'Crackle'))
    Check 'GLES: draw time (report)' $true ("{0} ms per frame (960x540), VFX draw calls {1}" -f $j.drawMs, $v.drawCalls)
    $bmp = Join-Path $Out 'vfx_GLES.bmp'
    & $Adb -s $serial pull $j.image $bmp | Out-Null
    $ref = Join-Path $Out 'vfx_DirectX11.png'
    $c = if ((Test-Path $ref) -and (Test-Path $bmp)) { [NovaImageCompare]::Compare($ref, $bmp, (Join-Path $Out 'gles_dx_diff.png')) } else { $null }
    # 파티클은 무작위 · 시간이 달라 같은 그림은 아니다 → 크게 어긋나지 않는지만 (보고)
    Check 'GLES vs DX11 picture (report)' ($null -ne $c) $(if ($c) { 'mean {0:N2}, >8: {1:N2}%' -f $c[1], $c[2] } else { 'reference or capture missing' })
}
$crash = @(Get-Content (Join-Path $Out 'logcat_vfx.txt') -ErrorAction SilentlyContinue | Select-String 'FATAL|signal \d')
Check 'no crash' ($crash.Count -eq 0) $(if ($crash.Count) { $crash[0].Line } else { 'logcat clean' })

if (-not $KeepEmulator) { MuMu @('control', '-v', $index, 'shutdown') | Out-Null }

$results | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
$fail = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
Write-Host ("{0} passed, {1} failed  → {2}" -f ($results.Count - $fail), $fail, $Out)
exit $fail
