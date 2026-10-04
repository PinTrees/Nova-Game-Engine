# 안드로이드 검사 (MuMu 플레이어 · 창 없이 터미널로): docs/ANDROID.md
#   powershell Tools/tests/android.ps1 [-Project 테스트프로젝트] [-Vm "NOVA Test"] [-KeepEmulator] [-SkipEditor]
#  1) MuMu 의 검사 전용 VM 을 켜고 창을 숨긴다 (없으면 만든다 — 사용자의 다른 VM 은 건드리지 않는다)
#  2) 에디터 (NOVA_ENGINE) 로 셰이더를 OpenGL ES 3.20 으로 내보내고 (assets/Shaders), DX11 RHI 검사 그림을 기준으로 남긴다
#  3) Android/build.py 로 APK → adb 설치 → am start -e test rhi | gfx → logcat 의 "NOVA_TEST {json}" → adb pull 로 그림 → 화소 비교
#     (rhi = RHI 층, gfx = Gfx 층 = 엔진 렌더러가 쓰는 D3D11 모양 층의 GLES 구현)
#  4) 플레이어 셸: 창 표면 · 프레임 루프 · input tap · HOME 뒤 다시 열기 (NOVA_EVENT 줄 + screencap)
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Vm = 'NOVA Test', [switch]$KeepEmulator, [switch]$SkipEditor, [int]$MaxDiff = 20)
. (Join-Path $PSScriptRoot 'common.ps1')
$script:Project = $Project
$ErrorActionPreference = 'Continue'
$Out = Join-Path $Root ("TestResults\android-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $Out | Out-Null
$results = @()
function Check([string]$name, [bool]$ok, [string]$detail) { $script:results += [pscustomobject]@{ Test = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail }; Write-Host ("  [{0}] {1} — {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail) }

$MuMu = 'D:\Program Files\Netease\MuMuPlayer\nx_main\MuMuManager.exe'
if (-not (Test-Path $MuMu)) { $MuMu = (Get-ChildItem 'C:\Program Files\Netease', 'D:\Program Files\Netease' -Recurse -Filter MuMuManager.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
$Sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$Adb = Join-Path $Sdk 'platform-tools\adb.exe'
function MuMu([string[]]$a) { (& $MuMu @a 2>&1 | Out-String) }

# ---- 1) 에뮬레이터
Write-Host '[android] emulator'
$all = MuMu @('info', '-v', 'all') | ConvertFrom-Json
$index = $null
foreach ($p in $all.PSObject.Properties) { if ($p.Value.name -eq $Vm) { $index = $p.Name } }
if (-not $index)
{
    $created = MuMu @('create', '-n', '1', '-m', '-ver', '12') | ConvertFrom-Json
    $index = @($created.PSObject.Properties)[0].Name
    MuMu @('rename', '-v', $index, '-n', $Vm) | Out-Null
    MuMu @('setting', '-v', $index, '-k', 'renderer_mode', '-val', 'vk') | Out-Null
}
$info = MuMu @('info', '-v', $index) | ConvertFrom-Json
if (-not $info.is_android_started) { MuMu @('control', '-v', $index, 'launch') | Out-Null }
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 180)
{
    MuMu @('control', '-v', $index, 'hide_window') | Out-Null   # 창 없이
    $info = MuMu @('info', '-v', $index) | ConvertFrom-Json
    if ($info.player_state -eq 'start_finished' -and $info.adb_port) { break }
    Start-Sleep -Seconds 2
}
$serial = "127.0.0.1:$($info.adb_port)"
& $Adb connect $serial | Out-Null
$abi = (& $Adb -s $serial shell getprop ro.product.cpu.abi | Out-String).Trim()
Check 'emulator ready' ($info.player_state -eq 'start_finished') ("VM {0} '{1}', adb {2}, abi {3}" -f $index, $Vm, $serial, $abi)

# ---- 2) 셰이더 · 기준 그림 (에디터)
$assets = Join-Path $Root 'Android\build\assets\Shaders'
if (-not $SkipEditor)
{
    Write-Host '[android] shaders + DX11 reference'
    Backup-Layout
    $ed = Start-TestEditor
    try
    {
        $s = Invoke-NovaJson "android shaders --out `"$assets`""
        Check 'GLES shaders exported' ($s -and $s.written -gt 0) $(if ($s) { "effects $($s.written)/$($s.effects), passes failed $($s.passesFailed)/$($s.passes)" } else { 'no result' })
        $r = Invoke-NovaJson "rhi-test DirectX11 --out `"$Out`""
        $g = Invoke-NovaJson "gfx-test DirectX11 --out `"$Out`""
        Check 'DX11 reference' ((Test-Path (Join-Path $Out 'rhi_DirectX11.png')) -and (Test-Path (Join-Path $Out 'gfx_DirectX11.png'))) 'rhi_DirectX11.png, gfx_DirectX11.png'
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)"; Restore-Layout }
}

# ---- 3) APK
Write-Host '[android] build APK'
$py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
$apk = Join-Path $Root 'Android\build\nova.apk'
Check 'APK build' ($py -match 'apk .*nova\.apk') (($py -split "`n" | Where-Object { $_ -match '^(built|apk|FAILED)' }) -join '; ')

# ---- 4) 설치 · 실행 · 결과
Write-Host '[android] run on emulator'
$inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String).Trim()
Check 'install' ($inst -match 'Success') ($inst -split "`n" | Select-Object -Last 1)
function DeviceTest([string]$test, [string]$label)
{
    & $Adb -s $serial logcat -c
    & $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
    & $Adb -s $serial shell am start -W -n com.nova.engine/android.app.NativeActivity -e test $test -e size 960x540 | Out-Null
    $line = $null
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 120 -and -not $line)
    {
        Start-Sleep -Milliseconds 500
        $line = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_TEST (\{.*\})' | Select-Object -Last 1)
    }
    & $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out "logcat_$test.txt")
    if (-not $line) { Check "$test test on device" $false "no NOVA_TEST line in 120 s (logcat_$test.txt)"; return }
    $j = $line.Matches[0].Groups[1].Value | ConvertFrom-Json
    Check "$test test on device" ([bool]$j.ok) ("{0}, load {1} ms, draw {2} ms {3}" -f $j.device, $j.loadMs, $j.drawMs, $j.error)
    if (-not $j.ok) { return }
    $bmp = Join-Path $Out "${test}_GLES.bmp"
    & $Adb -s $serial pull $j.image $bmp | Out-Null
    $ref = Join-Path $Out "${test}_DirectX11.png"
    if ((Test-Path $bmp) -and (Test-Path $ref))
    {
        $c = [NovaImageCompare]::Compare($ref, $bmp, (Join-Path $Out "${test}_diff_GLES.png"))
        if ($c) { Check "GLES = DX11 ($label)" ($c[0] -le $MaxDiff) ('max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2]) }
        else { Check "GLES = DX11 ($label)" $false 'size differs' }
    }
    else { Check "GLES = DX11 ($label)" $false 'image missing' }
}
DeviceTest 'rhi' 'RHI scene'
DeviceTest 'gfx' 'Gfx layer scene'
# ---- 5) 플레이어 셸: 창 표면 · 프레임 루프 · 터치 · 내렸다 올리기 · 회전 (logcat 의 NOVA_EVENT 와 화면 캡처)
Write-Host '[android] player shell'
Add-Type -AssemblyName System.Drawing
function Events { @(& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_EVENT (\{.*\})' | ForEach-Object { $_.Matches[0].Groups[1].Value | ConvertFrom-Json }) }
function WaitEvent([string]$name, [int]$after = 0, [int]$timeout = 20)
{
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $timeout)
    {
        $e = @(Events | Where-Object { $_.event -eq $name }) | Select-Object -Skip $after -First 1
        if ($e) { return $e }
        Start-Sleep -Milliseconds 250
    }
    return $null
}
function Screen([string]$name)
{
    $file = Join-Path $Out "$name.png"
    & $Adb -s $serial shell screencap -p /sdcard/nova_shot.png | Out-Null
    & $Adb -s $serial pull /sdcard/nova_shot.png $file | Out-Null
    $bytes = [IO.File]::ReadAllBytes($file)   # 파일을 잠그지 않게 메모리에서
    return New-Object System.Drawing.Bitmap (New-Object IO.MemoryStream (, $bytes))
}
# 우리 앱이 맨 앞인지 (MuMu 가 켜진 직후 광고 창을 띄우기도 한다 → 뒤로 가기로 닫고 다시 앞으로)
function EnsureFront
{
    for ($i = 0; $i -lt 6; $i++)
    {
        $focus = (& $Adb -s $serial shell dumpsys window | Select-String 'mCurrentFocus' | Select-Object -First 1).Line
        if ($focus -match 'com\.nova\.engine') { return $true }
        Write-Host "  front window is not NOVA: $($focus.Trim()) — closing it"
        & $Adb -s $serial shell input keyevent KEYCODE_BACK | Out-Null
        Start-Sleep -Milliseconds 800
        & $Adb -s $serial shell am start -n com.nova.engine/android.app.NativeActivity | Out-Null
        Start-Sleep -Milliseconds 1200
    }
    return $false
}
function Near($c, [int]$r, [int]$g, [int]$b) { [Math]::Abs($c.R - $r) -le 12 -and [Math]::Abs($c.G - $g) -le 12 -and [Math]::Abs($c.B - $b) -le 12 }
function Rgb($c) { "rgb($($c.R),$($c.G),$($c.B))" }

& $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
& $Adb -s $serial logcat -c
& $Adb -s $serial shell am start -W -n com.nova.engine/android.app.NativeActivity | Out-Null
$win = WaitEvent 'window'
$start = @(Events | Where-Object { $_.event -eq 'start' }) | Select-Object -First 1
Check 'shell window surface' ($win -and $win.width -gt 0 -and $win.height -gt 0) $(if ($win) { "{0}x{1}, {2}" -f $win.width, $win.height, $start.gl } else { 'no window event (logcat)' })
$front = $win -and (EnsureFront)
if ($win) { Check 'NOVA is the front window' $front 'mCurrentFocus = com.nova.engine' }
if ($win -and $front)
{
    Start-Sleep -Milliseconds 800
    $w = [int]$win.width; $h = [int]$win.height
    $s1 = Screen 'shell_first'
    $bg = $s1.GetPixel([int]($w / 2), [int]($h / 3))
    Check 'shell draws to the window' (Near $bg 20 31 56) ("background {0} (want rgb(20,31,56)), screen {1}x{2}" -f (Rgb $bg), $s1.Width, $s1.Height)
    Start-Sleep -Milliseconds 300
    $s2 = Screen 'shell_second'
    $moved = 0
    for ($x = 0; $x -lt [Math]::Min($s1.Width, $s2.Width); $x += 4) { if ($s1.GetPixel($x, $h - 20).ToArgb() -ne $s2.GetPixel($x, $h - 20).ToArgb()) { $moved++ } }
    Check 'frame loop runs' ($moved -gt 0) "bottom bar moved between two captures ($moved samples differ)"

    $tx = [int]($w * 0.3); $ty = [int]($h * 0.4)
    & $Adb -s $serial shell input tap $tx $ty | Out-Null
    $up = WaitEvent 'touch-up'
    $s3 = Screen 'shell_tap'
    $c = $s3.GetPixel($tx, $ty)
    Check 'touch reaches the app' ($up -and [Math]::Abs($up.x - $tx) -le 2 -and [Math]::Abs($up.y - $ty) -le 2) $(if ($up) { "touch-up at {0},{1} (tapped {2},{3})" -f $up.x, $up.y, $tx, $ty } else { 'no touch-up event' })
    Check 'touch position on screen' (Near $c 255 128 0) ("pixel at tap {0} (want orange)" -f (Rgb $c))

    & $Adb -s $serial shell input keyevent KEYCODE_HOME | Out-Null
    $lost = WaitEvent 'window-lost'
    $pause = WaitEvent 'pause'
    & $Adb -s $serial shell am start -n com.nova.engine/android.app.NativeActivity | Out-Null
    $again = WaitEvent 'window' 1
    EnsureFront | Out-Null
    Start-Sleep -Milliseconds 800
    $s4 = Screen 'shell_resumed'
    $c4 = $s4.GetPixel($tx, $ty)
    Check 'background → foreground keeps state' ($lost -and $pause -and $again -and (Near $c4 255 128 0)) ("pause {0}, window lost {1}, window again {2}, tap mark {3}" -f [bool]$pause, [bool]$lost, [bool]$again, (Rgb $c4))

    # 회전은 검사하지 않는다: MuMu (태블릿 모드) 는 user_rotation · wm size 를 바꿔도 앱 창 크기를 바꾸지 않는다
    $crash = @(& $Adb -s $serial logcat -d -s AndroidRuntime:E DEBUG:F libc:F | Select-String 'FATAL|signal')
    Check 'no crash' ($crash.Count -eq 0) $(if ($crash.Count) { $crash[0].Line } else { 'logcat clean' })
}
& $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out 'logcat_shell.txt')
& $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
if (-not $KeepEmulator) { MuMu @('control', '-v', $index, 'shutdown') | Out-Null }

$results | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
$fail = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
Write-Host ("{0} passed, {1} failed  → {2}" -f ($results.Count - $fail), $fail, $Out)
exit $fail
