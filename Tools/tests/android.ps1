# 안드로이드 검사 (MuMu 플레이어 · 창 없이 터미널로): docs/ANDROID.md
#   powershell Tools/tests/android.ps1 [-Project 테스트프로젝트] [-Vm "NOVA Test"] [-KeepEmulator] [-SkipEditor]
#  1) MuMu 의 검사 전용 VM 을 켜고 창을 숨긴다 (없으면 만든다 — 사용자의 다른 VM 은 건드리지 않는다)
#  2) 에디터 (NOVA_ENGINE) 로 셰이더를 OpenGL ES 3.20 으로 내보내고 (assets/Shaders), DX11 RHI 검사 그림을 기준으로 남긴다
#  3) Android/build.py 로 APK → adb 설치 → am start -e test rhi → logcat 의 "NOVA_TEST {json}" → adb pull 로 그림 → 화소 비교
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
        Check 'DX11 reference' (Test-Path (Join-Path $Out 'rhi_DirectX11.png')) 'rhi_DirectX11.png'
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
& $Adb -s $serial logcat -c
& $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
& $Adb -s $serial shell am start -W -n com.nova.engine/android.app.NativeActivity -e test rhi -e size 960x540 | Out-Null
$line = $null
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 120 -and -not $line)
{
    Start-Sleep -Milliseconds 500
    $line = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_TEST (\{.*\})' | Select-Object -Last 1)
}
& $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out 'logcat.txt')
if (-not $line) { Check 'rhi test on device' $false 'no NOVA_TEST line in 120 s (logcat.txt)' }
else
{
    $j = $line.Matches[0].Groups[1].Value | ConvertFrom-Json
    Check 'rhi test on device' ([bool]$j.ok) ("{0}, load {1} ms, draw {2} ms {3}" -f $j.device, $j.loadMs, $j.drawMs, $j.error)
    if ($j.ok)
    {
        $bmp = Join-Path $Out 'rhi_GLES.bmp'
        & $Adb -s $serial pull $j.image $bmp | Out-Null
        $ref = Join-Path $Out 'rhi_DirectX11.png'
        if ((Test-Path $bmp) -and (Test-Path $ref))
        {
            $c = [NovaImageCompare]::Compare($ref, $bmp, (Join-Path $Out 'rhi_diff_GLES.png'))
            if ($c) { Check 'GLES = DX11 (RHI scene)' ($c[0] -le $MaxDiff) ('max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2]) }
            else { Check 'GLES = DX11 (RHI scene)' $false 'size differs' }
        }
        else { Check 'GLES = DX11 (RHI scene)' $false 'image missing' }
    }
}
if (-not $KeepEmulator) { MuMu @('control', '-v', $index, 'shutdown') | Out-Null }

$results | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
$fail = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
Write-Host ("{0} passed, {1} failed  → {2}" -f ($results.Count - $fail), $fail, $Out)
exit $fail
