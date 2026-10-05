# 안드로이드 (OpenGL ES 3.2) 날씨 (com.nova.weather) 검사 (MuMu 플레이어 · 창 없이): docs/WEATHER.md
#   powershell Tools/tests/android_weather.ps1 [-Project 테스트프로젝트] [-Vm "NOVA Test"] [-KeepEmulator]
#  1) 에디터: GLES 셰이더 (32 · 58 · 59 포함) + 검사 씬 (폭풍 · 지붕 · 돌 바닥) → 게임 데이터 (날씨 소리 포함) + DX11 기준 그림
#  2) APK (Android/build.py) → 설치
#  3) 기기의 scene 검사 (화면 없이) → 그림이 DX11 기준과 비슷한가 (먹구름 · 젖은 바닥 · 비), 날씨 오류 · 충돌이 없는가
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Vm = 'NOVA Test', [switch]$KeepEmulator, [string]$Scene = 'Assets\Scenes\AndroidWeather.scene', [string]$Profile = 'Storm')
. (Join-Path $PSScriptRoot 'common.ps1')
$script:Project = $Project
$ErrorActionPreference = 'Continue'
$Out = Join-Path $Root ("TestResults\android-weather-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $Out | Out-Null
$results = @()
function Check([string]$name, [bool]$ok, [string]$detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail }; Write-Host ("  [{0}] {1} — {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) }

$MuMu = 'D:\Program Files\Netease\MuMuPlayer\nx_main\MuMuManager.exe'
if (-not (Test-Path $MuMu)) { $MuMu = (Get-ChildItem 'C:\Program Files\Netease', 'D:\Program Files\Netease' -Recurse -Filter MuMuManager.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
$Sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$Adb = Join-Path $Sdk 'platform-tools\adb.exe'
function MuMu([string[]]$a) { (& $MuMu @a 2>&1 | Out-String) }
Add-Type -AssemblyName System.Drawing
function Mean([string]$path)
{
    $bm = [System.Drawing.Bitmap]::FromFile($path); $sum = 0.0; $n = 0
    for ($y = 0; $y -lt $bm.Height; $y += 3) { for ($x = 0; $x -lt $bm.Width; $x += 3) { $c = $bm.GetPixel($x, $y); $sum += 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B; $n++ } }
    $bm.Dispose(); $sum / [math]::Max(1, $n)
}

# ---- 에뮬레이터 (android.ps1 과 같은 검사 전용 VM)
Write-Host '[android-weather] emulator'
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

# ---- 1) 에디터: 셰이더 · 검사 씬 · 게임 데이터 · DX11 기준
Write-Host '[android-weather] shaders + scene + DX11 reference'
$assets = Join-Path $Root 'Android\build\assets\Shaders'
$gameDir = Join-Path $Root 'Android\build\assets\game'
Backup-Layout
$editorSettings = Join-Path $Project 'Assets\EditorSettings.json'
$editorSettingsBefore = if (Test-Path $editorSettings) { [IO.File]::ReadAllBytes($editorSettings) } else { $null }
$ed = Start-TestEditor
try
{
    Invoke-Nova 'autosave discard' | Out-Null
    Invoke-NovaJson 'package add com.nova.weather' | Out-Null
    $s = Invoke-NovaJson "android shaders --out `"$assets`""
    Check 'GLES shaders exported (weather snow kernels included)' ($s -and $s.written -gt 0 -and (Test-Path (Join-Path $assets '59. WeatherSnow.json'))) $(if ($s) { "effects $($s.written)/$($s.effects), passes failed $($s.passesFailed)/$($s.passes)" } else { 'no result' })
    Invoke-Nova 'scene new --force' | Out-Null
    Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 200,1,200' | Out-Null
    Invoke-Nova 'create cube --name Roof --position 0,3,6 --scale 6,0.3,6' | Out-Null
    Invoke-Nova 'create sphere --name Ball --position -3,1,4 --scale 2,2,2' | Out-Null
    Invoke-Nova 'set "Main Camera" --position 0,2.2,-4 --rotation 12,0,0' | Out-Null
    Invoke-Nova 'create empty --name Weather' | Out-Null
    Invoke-Nova 'add-component Weather WeatherController' | Out-Null
    Invoke-Nova ('set Weather --component WeatherController --values "{\"profile\":\"' + $Profile + '\",\"transitionTime\":0,\"lightning\":false,\"sound\":true}"')| Out-Null
    Invoke-Nova "weather set --profile $Profile --seconds 0" | Out-Null   # 편집기도 바로 (기기는 장면을 읽을 때 바로)
    Invoke-Nova "scene save --as $($Scene -replace '\\', '/')" | Out-Null
    Remove-Item -Recurse -Force $gameDir -ErrorAction SilentlyContinue
    $x = Invoke-NovaJson "android export --out `"$(Split-Path $assets)`" --scenes `"$Scene`""
    $wav = @(Get-ChildItem (Join-Path $gameDir 'Packages\com.nova.weather\Resources\Audio') -Filter *.wav -ErrorAction SilentlyContinue)
    Check 'game data exported (weather sounds follow the package)' ($x -and $x.files -gt 0 -and $wav.Count -eq 6) $(if ($x) { "$($x.files) files, weather sounds $($wav.Count)" } else { 'no result' })
    # 기준 그림 전에 에디터가 몇 프레임 돌게 (Weather Controller 가 날씨를 넣고 비 · 표면이 차게)
    Invoke-Nova 'window scene' | Out-Null
    Invoke-Nova 'camera --position 0,2.2,-4 --target 0,1.5,6' | Out-Null
    Invoke-Nova 'wait 240' | Out-Null
    Invoke-NovaJson "android reference --out `"$(Join-Path $Out 'weather_DirectX11.png')`" --width 960 --height 540 --frames 240" | Out-Null
}
finally
{
    Write-Host "  $(Stop-TestEditor $ed)"; Restore-Layout
    if ($editorSettingsBefore) { [IO.File]::WriteAllBytes($editorSettings, $editorSettingsBefore) }
}

# ---- 2) APK
Write-Host '[android-weather] build APK'
$py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
$py | Set-Content -Encoding utf8 (Join-Path $Out 'build_apk.txt')
$apk = Join-Path $Root 'Android\build\nova.apk'
Check 'APK build' ($py -match 'apk .*nova\.apk') (($py -split "`n" | Where-Object { $_ -match '^(built|apk|FAILED)' }) -join '; ')
$inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String).Trim()
Check 'install' ($inst -match 'Success') ($inst -split "`n" | Select-Object -Last 1)

# ---- 3) 기기: 예열 (비가 쌓이고 표면이 젖게) + 60 프레임
Write-Host '[android-weather] device'
& $Adb -s $serial logcat -c
& $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
& $Adb -s $serial shell "am start -W -n com.nova.engine/android.app.NativeActivity -e test scene -e size 960x540 -e warmup 600 -e frames 60" | Out-Null
$line = $null
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 180 -and -not $line)
{
    Start-Sleep -Milliseconds 500
    $line = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_TEST \{.*"image":"([^"]*)"' | Select-Object -Last 1)
}
& $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out 'logcat_weather.txt')
$j = $null
if ($line)
{
    $remote = ($line.Matches[0].Groups[1].Value -replace '/[^/]*$', '') + '/result_scene.json'
    $local = Join-Path $Out 'result_scene.json'
    & $Adb -s $serial pull $remote $local 2>&1 | Out-Null
    if (Test-Path $local) { $j = Get-Content -Raw $local | ConvertFrom-Json }
}
Check 'scene on device' ($j -and [bool]$j.ok) $(if ($j) { "{0}, load {1} ms, draw {2} ms {3}" -f $j.device, $j.loadMs, $j.drawMs, $j.error } else { 'no NOVA_TEST line / result file (logcat_weather.txt)' })
$log = Get-Content (Join-Path $Out 'logcat_weather.txt') -ErrorAction SilentlyContinue
$werr = @($log | Select-String '\[Weather\].*(fail|error)|clip not found.*weather')
Check 'weather on device: no errors (cover map, snow map, sounds)' ($werr.Count -eq 0) $(if ($werr.Count) { $werr[0].Line } else { 'no weather errors in logcat' })
if ($j -and $j.ok)
{
    # 비 · 눈 입자: 살아 있고 자리가 유한한가 (GLES 에서 Turbulence 가 NaN 을 만들던 일)
    $fx = @($j.vfx.effects | Where-Object { $_.object -eq 'Weather Precipitation' })[0]
    $alive = if ($fx) { [int]$fx.alive } else { 0 }
    $finite = $fx -and $fx.bounds -and @($fx.bounds[1] | Where-Object { $null -eq $_ }).Count -eq 0
    Check 'precipitation particles alive with finite bounds' ($alive -gt 1000 -and $finite) ("alive {0}, bounds max {1}" -f $alive, $(if ($fx) { ($fx.bounds[1] -join ', ') } else { '-' }))
    $bmp = Join-Path $Out 'weather_GLES.bmp'
    & $Adb -s $serial pull $j.image $bmp | Out-Null
    $ref = Join-Path $Out 'weather_DirectX11.png'
    if ((Test-Path $ref) -and (Test-Path $bmp))
    {
        $mr = Mean $ref; $mg = Mean $bmp
        $c = [NovaImageCompare]::Compare($ref, $bmp, (Join-Path $Out 'gles_dx_diff.png'))
        # 빗방울은 무작위라 같은 그림은 아니다 → 밝기 (먹구름 · 젖은 바닥) 가 DX11 과 비슷한지
        Check "GLES picture matches DX11 (${Profile} - darkened sky, wet ground)" ([math]::Abs($mg - $mr) -lt $mr * 0.15) ("mean brightness DX11 {0:N1}, GLES {1:N1}; diff mean {2:N2}, >8: {3:N2}%" -f $mr, $mg, $c[1], $c[2])
    }
    else { Check "GLES picture matches DX11 ($Profile)" $false 'reference or capture missing' }
    Check 'GLES: draw time (report)' $true ("{0} ms per frame (960x540)" -f $j.drawMs)
}
$crash = @($log | Select-String 'FATAL|signal \d')
Check 'no crash' ($crash.Count -eq 0) $(if ($crash.Count) { $crash[0].Line } else { 'logcat clean' })

if (-not $KeepEmulator) { MuMu @('control', '-v', $index, 'shutdown') | Out-Null }

$results | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
$fail = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
Write-Host ("{0} passed, {1} failed  → {2}" -f ($results.Count - $fail), $fail, $Out)
exit $fail
