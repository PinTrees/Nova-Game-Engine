# 안드로이드 (OpenGL ES 3.2) 재질 테셀레이션 검사 (MuMu 플레이어 · 창 없이): docs/TESSELLATION.md
#   powershell Tools/tests/android_tessellation.ps1 [-Project 테스트프로젝트] [-Vm "NOVA Test"] [-KeepEmulator] [-SkipBuild]
#   -SkipBuild: 1) · 2) 를 건너뛰고 지난 APK · DX11 기준 그림으로 기기만 (에뮬레이터가 끊겨 다시 돌릴 때)
#  1) 에디터: GLES 셰이더 (32 · 28 · 26 의 테셀레이션 기법) + 검사 씬 (돌 벽 · 자갈 바닥, Displacement Mode = Tessellation) → 게임 데이터 + DX11 기준 그림
#  2) APK (Android/build.py) → 설치
#  3) 기기의 scene 검사 (화면 없이) → 그림이 DX11 기준과 같은가 (깊이 프리패스와 본 패스가 어긋나면 검은 얼룩), 테셀레이션 pass 오류 · 충돌이 없는가
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Vm = 'NOVA Test', [switch]$KeepEmulator, [string]$Scene = 'Assets\Scenes\AndroidTessellation.scene', [switch]$SkipBuild)
. (Join-Path $PSScriptRoot 'common.ps1')
$script:Project = $Project
$ErrorActionPreference = 'Continue'
$Out = Join-Path $Root ("TestResults\android-tessellation-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $Out | Out-Null
$results = @()
function Check([string]$name, [bool]$ok, [string]$detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail }; Write-Host ("  [{0}] {1} — {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) }

$MuMu = 'D:\Program Files\Netease\MuMuPlayer\nx_main\MuMuManager.exe'
if (-not (Test-Path $MuMu)) { $MuMu = (Get-ChildItem 'C:\Program Files\Netease', 'D:\Program Files\Netease' -Recurse -Filter MuMuManager.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
$Sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$Adb = Join-Path $Sdk 'platform-tools\adb.exe'
function MuMu([string[]]$a) { (& $MuMu @a 2>&1 | Out-String) }

# ---- 검사 재질 (프로젝트 Assets/TessTest): 벽돌 · 자갈 높이 맵 (Tools/tests/make_tess_textures.py)
$tessDir = Join-Path $Project 'Assets\TessTest'
& python (Join-Path $PSScriptRoot 'make_tess_textures.py') $tessDir | Out-Null
Check 'test materials (height maps + Tessellation Displacement)' ((Test-Path (Join-Path $tessDir 'StoneWall.mat')) -and (Test-Path (Join-Path $tessDir 'Cobble.mat'))) $tessDir

# ---- 에뮬레이터 (android.ps1 과 같은 검사 전용 VM)
Write-Host '[android-tessellation] emulator'
$all = MuMu @('info', '-v', 'all') | ConvertFrom-Json
$index = $null
foreach ($p in $all.PSObject.Properties) { if ($p.Value.name -eq $Vm) { $index = $p.Name } }
if (-not $index) { Check 'emulator' $false "no MuMu VM '$Vm' (run Tools/tests/android.ps1 once to create it)"; exit 1 }
$info = Start-MuMuHidden $MuMu $index   # 화면에 보이지 않게 (common.ps1)
$serial = "127.0.0.1:$($info.adb_port)"
& $Adb connect $serial | Out-Null
$abi = ''
for ($i = 0; $i -lt 30 -and -not $abi; $i++) { $abi = (& $Adb -s $serial shell getprop ro.product.cpu.abi 2>$null | Out-String).Trim(); if (-not $abi) { Start-Sleep -Seconds 2; & $Adb connect $serial | Out-Null } }
Check 'emulator ready' ($info.player_state -eq 'start_finished' -and $abi) ("VM {0} '{1}', adb {2}, abi {3}" -f $index, $Vm, $serial, $abi)
if (-not $abi) { exit 1 }
# 부팅이 다 끝난 뒤 설치 (막 켠 VM 에 바로 설치하면 adb 가 끊겼다)
for ($i = 0; $i -lt 60; $i++) { if ((& $Adb -s $serial shell getprop sys.boot_completed 2>$null | Out-String).Trim() -eq '1') { break }; Start-Sleep -Seconds 2 }
Start-Sleep -Seconds 5

# ---- 1) 에디터: 셰이더 · 검사 씬 · 게임 데이터 · DX11 기준
$assets = Join-Path $Root 'Android\build\assets\Shaders'
$gameDir = Join-Path $Root 'Android\build\assets\game'
$apk = Join-Path $Root 'Android\build\nova.apk'
if ($SkipBuild)
{
    $prev = Get-ChildItem (Join-Path $Root 'TestResults') -Directory -Filter 'android-tessellation-*' | Sort-Object Name -Descending |
        Where-Object { Test-Path (Join-Path $_.FullName 'tess_DirectX11.png') } | Select-Object -First 1
    if ($prev) { Copy-Item (Join-Path $prev.FullName 'tess_DirectX11.png') $Out }
    Check 'previous APK + DX11 reference (-SkipBuild)' ((Test-Path $apk) -and $prev) $(if ($prev) { $prev.Name } else { 'no previous run' })
}
else
{
Write-Host '[android-tessellation] shaders + scene + DX11 reference'
Backup-Layout
$editorSettings = Join-Path $Project 'Assets\EditorSettings.json'
$editorSettingsBefore = if (Test-Path $editorSettings) { [IO.File]::ReadAllBytes($editorSettings) } else { $null }
$ed = Start-TestEditor
try
{
    Invoke-Nova 'autosave discard' | Out-Null
    $s = Invoke-NovaJson "android shaders --out `"$assets`""
    # GLES 로 바꾼 32 · 28 · 26 에 테셀레이션 기법의 Hull · Domain 이 들어 있는가
    $tessOk = $true
    foreach ($f in '32. InstancedBasic.json', '28. SsaoNormalDepth.json', '26. BuildShadowMap.json')
    {
        $txt = Get-Content -Raw (Join-Path $assets $f) -ErrorAction SilentlyContinue
        if (-not $txt -or $txt -notmatch 'gl_TessCoord' -or $txt -notmatch 'invariant gl_Position') { $tessOk = $false }
    }
    Check 'GLES shaders exported (tessellation stages, invariant position)' ($s -and $s.written -gt 0 -and $tessOk) $(if ($s) { "effects $($s.written)/$($s.effects), passes failed $($s.passesFailed)/$($s.passes)" } else { 'no result' })
    Invoke-Nova 'scene new --force' | Out-Null
    Invoke-Nova 'create plane --name Floor --position 0,0,0 --scale 0.6,1,0.6' | Out-Null
    Invoke-Nova 'create plane --name Wall --position 0,1.5,2 --rotation -90,0,0 --scale 0.4,1,0.3' | Out-Null
    Invoke-Nova 'set Floor --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/TessTest/Cobble.mat\"]}"' | Out-Null
    Invoke-Nova 'set Wall --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/TessTest/StoneWall.mat\"]}"' | Out-Null
    Invoke-Nova 'set "Main Camera" --position -1.6,1.4,-1.2 --rotation 13,32,0' | Out-Null
    Invoke-Nova "scene save --as $($Scene -replace '\\', '/')" | Out-Null
    Remove-Item -Recurse -Force $gameDir -ErrorAction SilentlyContinue
    $x = Invoke-NovaJson "android export --out `"$(Split-Path $assets)`" --scenes `"$Scene`""
    $h = @(Get-ChildItem (Join-Path $gameDir 'Assets\TessTest') -Filter *_Height.png* -ErrorAction SilentlyContinue)
    Check 'game data exported (height maps follow the materials)' ($x -and $x.files -gt 0 -and $h.Count -eq 2) $(if ($x) { "$($x.files) files, height maps $($h.Count)" } else { 'no result' })
    Invoke-Nova 'window scene' | Out-Null
    Invoke-NovaJson "android reference --out `"$(Join-Path $Out 'tess_DirectX11.png')`" --width 960 --height 540 --frames 30" | Out-Null
}
finally
{
    Write-Host "  $(Stop-TestEditor $ed)"; Restore-Layout
    if ($editorSettingsBefore) { [IO.File]::WriteAllBytes($editorSettings, $editorSettingsBefore) }
}

# ---- 2) APK
Write-Host '[android-tessellation] build APK'
$py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
$py | Set-Content -Encoding utf8 (Join-Path $Out 'build_apk.txt')
Check 'APK build' ($py -match 'apk .*nova\.apk') (($py -split "`n" | Where-Object { $_ -match '^(built|apk|FAILED)' }) -join '; ')
}
$inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String).Trim()
Check 'install' ($inst -match 'Success') ($inst -split "`n" | Select-Object -Last 1)

# ---- 3) 기기
Write-Host '[android-tessellation] device'
& $Adb -s $serial logcat -c
& $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
& $Adb -s $serial shell "am start -W -n com.nova.engine/android.app.NativeActivity -e test scene -e size 960x540 -e warmup 30 -e frames 60" | Out-Null
$line = $null
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 180 -and -not $line)
{
    Start-Sleep -Milliseconds 500
    $line = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_TEST \{.*"image":"([^"]*)"' | Select-Object -Last 1)
}
& $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out 'logcat_tessellation.txt')
$j = $null
if ($line)
{
    $remote = ($line.Matches[0].Groups[1].Value -replace '/[^/]*$', '') + '/result_scene.json'
    $local = Join-Path $Out 'result_scene.json'
    & $Adb -s $serial pull $remote $local 2>&1 | Out-Null
    if (Test-Path $local) { $j = Get-Content -Raw $local | ConvertFrom-Json }
}
Check 'scene on device' ($j -and [bool]$j.ok) $(if ($j) { "{0}, load {1} ms, draw {2} ms {3}" -f $j.device, $j.loadMs, $j.drawMs, $j.error } else { 'no NOVA_TEST line / result file (logcat_tessellation.txt)' })
$log = Get-Content (Join-Path $Out 'logcat_tessellation.txt') -ErrorAction SilentlyContinue
$terr = @($log | Select-String 'Tess\w*Tech.*(not available|GLSL|link)')
Check 'tessellation passes built on the device (GLES 3.2)' ($terr.Count -eq 0) $(if ($terr.Count) { $terr[0].Line } else { 'no tessellation pass errors in logcat' })
if ($j -and $j.ok)
{
    $bmp = Join-Path $Out 'tess_GLES.bmp'
    & $Adb -s $serial pull $j.image $bmp | Out-Null
    $ref = Join-Path $Out 'tess_DirectX11.png'
    if ((Test-Path $ref) -and (Test-Path $bmp))
    {
        # 깊이 프리패스와 본 패스의 자리가 어긋나면 그 자리는 하늘도 재질도 아닌 검정 → 아주 어두운 점의 비율로
        $c = [NovaImageCompare]::Compare($ref, $bmp, (Join-Path $Out 'gles_dx_diff.png'))
        Check 'GLES picture matches DX11 (no depth mismatch speckles)' ($c[2] -lt 6.0) ("diff mean {0:N2}, >8: {1:N2}%" -f $c[1], $c[2])
    }
    else { Check 'GLES picture matches DX11' $false 'reference or capture missing' }
    Check 'GLES: draw time (report)' $true ("{0} ms per frame (960x540)" -f $j.drawMs)
}
$crash = @($log | Select-String 'FATAL|signal \d')
Check 'no crash' ($crash.Count -eq 0) $(if ($crash.Count) { $crash[0].Line } else { 'logcat clean' })

if (-not $KeepEmulator) { MuMu @('control', '-v', $index, 'shutdown') | Out-Null }

$results | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
$fail = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
Write-Host ("{0} passed, {1} failed  → {2}" -f ($results.Count - $fail), $fail, $Out)
exit $fail
