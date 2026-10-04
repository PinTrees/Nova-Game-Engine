# 안드로이드 검사 (MuMu 플레이어 · 창 없이 터미널로): docs/ANDROID.md
#   powershell Tools/tests/android.ps1 [-Project 테스트프로젝트] [-Vm "NOVA Test"] [-KeepEmulator] [-SkipEditor]
#  1) MuMu 의 검사 전용 VM 을 켜고 창을 숨긴다 (없으면 만든다 — 사용자의 다른 VM 은 건드리지 않는다)
#  2) 에디터 (NOVA_ENGINE) 로 셰이더를 OpenGL ES 3.20 으로 내보내고 (assets/Shaders), DX11 RHI 검사 그림을 기준으로 남긴다
#  3) Android/build.py 로 APK → adb 설치 → am start -e test rhi | gfx → logcat 의 "NOVA_TEST {json}" → adb pull 로 그림 → 화소 비교
#     (rhi = RHI 층, gfx = Gfx 층 = 엔진 렌더러가 쓰는 D3D11 모양 층의 GLES 구현)
#  4) 플레이어 셸: 창 표면 · 프레임 루프 · input tap · HOME 뒤 다시 열기 (NOVA_EVENT 줄 + screencap)
#  5) 엔진 플레이어 (창) 6) 텍스처 압축: -TextureScene 을 ASTC · ETC2 로 구워 APK 마다 실행 → DX11 기준과 비교 (APK 의 게임 데이터는 마지막 형식으로 남는다)
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Vm = 'NOVA Test', [switch]$KeepEmulator, [switch]$SkipEditor, [int]$MaxDiff = 20,
    [string]$Scene = 'Assets\Scenes\Shadows.scene', [string]$TextureScene = 'Assets\Scenes\Materials.scene', [string[]]$TextureFormats = @('astc', 'etc2'),
    [string]$ModelScene = 'Assets\Scenes\AndroidModels.scene', [string]$TouchScene = 'Assets\Scenes\AndroidTouch.scene', [string]$ScriptScene = 'Assets\Scenes\AndroidScript.scene')
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

# ---- 리버브 (PC 에서): 안드로이드 믹서의 리버브 (Android/Source/Engine/AudioReverb.h) 임펄스 응답 — RT60 이 프리셋의 DecayTime 과 맞는지
Write-Host '[android] reverb impulse response (PC)'
$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if ($vcvars)
{
    $env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;" + $env:PATH
    $exe = Join-Path $Out 'android_reverb_test.exe'
    $rv = (cmd /c "`"$vcvars`" >nul 2>nul & cl /nologo /O2 /EHsc /std:c++20 `"$(Join-Path $PSScriptRoot 'android_reverb_test.cpp')`" /Fe:`"$exe`" /Fo:`"$Out\\`" >nul & `"$exe`"" | Out-String)
    Check 'reverb RT60 follows the preset (PC)' ($LASTEXITCODE -eq 0 -and $rv -match 'Hangar.*OK') (($rv -split "`n" | Where-Object { $_ -match 'RT60' } | ForEach-Object { ($_ -replace '\s+', ' ').Trim() }) -join '; ')
}

# ---- 2) 셰이더 · 기준 그림 (에디터)
$assets = Join-Path $Root 'Android\build\assets\Shaders'
# 복사 엔진이면 안드로이드 C# 런타임 (Mono) 을 엔진 배포판 자리 (Android/Player/x86_64/mono) 에 (Tools/fetch_android_mono.ps1 로 받은 것)
$engineRoot = if ($env:NOVA_ENGINE) { Split-Path (Split-Path $env:NOVA_ENGINE) } else { $Root }
$monoSrc = Join-Path $Root 'ThirdParty\MonoAndroid\x86_64\runtimes\android-x64'
$monoDst = Join-Path $engineRoot 'Android\Player\x86_64\mono'
if ((Test-Path "$monoSrc\native\libmonosgen-2.0.so") -and $engineRoot -ne $Root -and -not (Test-Path "$monoDst\native\libmonosgen-2.0.so"))
{
    New-Item -ItemType Directory -Force "$monoDst\lib", "$monoDst\native" | Out-Null
    Copy-Item "$monoSrc\lib\net8.0\*.dll" "$monoDst\lib"
    Get-ChildItem "$monoSrc\native" -File | Where-Object { $_.Extension -in '.so', '.dll' } | Copy-Item -Destination "$monoDst\native"
}
if (-not $SkipEditor)
{
    Write-Host '[android] shaders + DX11 reference'
    Backup-Layout
    # 검사 씬을 저장하면 에디터의 '마지막 씬' 이 바뀐다 → 끝나면 되돌린다 (다른 검사가 이 씬 (long.mp3 등) 으로 열리지 않게)
    $editorSettings = Join-Path $Project 'Assets\EditorSettings.json'
    $editorSettingsBefore = if (Test-Path $editorSettings) { [IO.File]::ReadAllBytes($editorSettings) } else { $null }
    $ed = Start-TestEditor
    try
    {
        $s = Invoke-NovaJson "android shaders --out `"$assets`""
        Check 'GLES shaders exported' ($s -and $s.written -gt 0) $(if ($s) { "effects $($s.written)/$($s.effects), passes failed $($s.passesFailed)/$($s.passes)" } else { 'no result' })
        $r = Invoke-NovaJson "rhi-test DirectX11 --out `"$Out`""
        $g = Invoke-NovaJson "gfx-test DirectX11 --out `"$Out`""
        Check 'DX11 reference' ((Test-Path (Join-Path $Out 'rhi_DirectX11.png')) -and (Test-Path (Join-Path $Out 'gfx_DirectX11.png'))) 'rhi_DirectX11.png, gfx_DirectX11.png'
        # 엔진 장면: 게임 데이터 (APK 의 assets/game) + 같은 씬을 플레이어 순서로 그린 DX11 기준 (화면 없는 검사 960x540 · 창 1600x900)
        $x = Invoke-NovaJson "android export --out `"$(Split-Path $assets)`" --scenes `"$Scene`""
        Check 'game data exported' ($x -and $x.files -gt 0) $(if ($x) { "$($x.files) files, $([math]::Round($x.bytes / 1MB, 1)) MB, json paths converted $($x.jsonConverted)" } else { 'no result' })
        Invoke-Nova "scene open `"$($Scene -replace '\\', '/')`" --force" | Out-Null; Invoke-Nova 'wait 30' | Out-Null
        $sr = Invoke-NovaJson "android reference --out `"$(Join-Path $Out 'scene_DirectX11.png')`" --width 960 --height 540 --frames 10"
        $wr = Invoke-NovaJson "android reference --out `"$(Join-Path $Out 'window_DirectX11.png')`" --width 1600 --height 900 --frames 10"
        Check 'DX11 scene reference' ((Test-Path (Join-Path $Out 'scene_DirectX11.png')) -and (Test-Path (Join-Path $Out 'window_DirectX11.png'))) "$Scene (960x540, 1600x900)"
        # 텍스처 압축: 같은 씬을 ASTC · ETC2 로 구운 게임 데이터 (6 단계에서 하나씩 APK 에 넣어 실행) + DX11 기준 (PC 는 BC)
        foreach ($tc in $TextureFormats)
        {
            $t = Invoke-NovaJson "android export --out `"$(Join-Path $Out "tex_$tc")`" --scenes `"$TextureScene`" --texture-compression $tc"
            $baked = @($t.textures | Where-Object { $_.format })
            Check "textures baked ($tc)" ($t -and $baked.Count -gt 0 -and $baked.Count -eq @($t.textures).Count) $(if ($t) { ($t.textures | ForEach-Object { "{0} {1} {2} {3} dB" -f (Split-Path $_.path -Leaf), $_.format, $_.size, $_.psnr }) -join '; ' } else { 'no result' })
        }
        Invoke-Nova "scene open `"$($TextureScene -replace '\\', '/')`" --force" | Out-Null; Invoke-Nova 'wait 30' | Out-Null
        $texRef = Join-Path $Out 'tex_DirectX11.png'
        Invoke-NovaJson "android reference --out `"$texRef`" --width 960 --height 540 --frames 10" | Out-Null
        foreach ($tc in $TextureFormats) { if (Test-Path $texRef) { Copy-Item $texRef (Join-Path $Out "tex_${tc}_DirectX11.png") } }
        # 모델: 기본 캐릭터 (FBX · Animator — 패키지) + VRM 캐릭터를 둔 씬 → 메시 캐시만 구워 넣은 게임 데이터 + DX11 기준 (편집 중 = 첫 자세)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 8,1,8' | Out-Null
        Invoke-Nova 'create character --name Hero --position -0.7,0,0 --rotation 0,180,0' | Out-Null
        Invoke-Nova 'create character --name Chibi --model Assets\Models\ChibiRig.vrm --position 0.7,0,0 --rotation 0,180,0' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.1,-3.2 --rotation 6,0,0' | Out-Null
        Invoke-Nova "scene save --as $($ModelScene -replace '\\', '/')" | Out-Null
        Invoke-Nova 'wait 30' | Out-Null
        $m = Invoke-NovaJson "android export --out `"$(Join-Path $Out 'models')`" --scenes `"$ModelScene`""
        $mOk = @($m.models | Where-Object { -not $_.error })
        Check 'models baked to mesh caches' ($m -and $mOk.Count -gt 0 -and $mOk.Count -eq @($m.models).Count) $(if ($m) { "$($mOk.Count)/$(@($m.models).Count) models, " + (($m.models | Select-Object -First 4 | ForEach-Object { "{0} {1}{2} KB (source {3} KB)" -f (Split-Path $_.path -Leaf), $(if ($_.error) { $_.error + ' ' } else { "skinned $($_.skinnedMeshes), clips $($_.clips), " }), [math]::Round($_.bytes / 1KB), [math]::Round($_.sourceBytes / 1KB) }) -join '; ') } else { 'no result' })
        Invoke-NovaJson "android reference --out `"$(Join-Path $Out 'models_DirectX11.png')`" --width 960 --height 540 --frames 10" | Out-Null
        # 터치 → UI: Toggle (5 배, 체크 상자 450,450) + Slider (4 배, 손잡이 520,668 · 막대 480..1120) — 앱 창 1600x900, 캔버스 가운데 800,450
        #  + 소리: AudioSource (long.mp3, 반복, Play On Awake) — 안드로이드 XAudio2 (소프트웨어 믹서 + AAudio)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create ui:Toggle --name Tg' | Out-Null
        Invoke-Nova 'set Tg --scale 5,5,1' | Out-Null
        Invoke-Nova 'create ui:Slider --name Sl' | Out-Null
        Invoke-Nova 'set Sl --position 0,-220,0 --scale 4,4,1' | Out-Null
        Invoke-Nova 'create audio-source --name Music' | Out-Null
        Invoke-Nova 'set Music --component AudioSource --values "{\"clip\":\"Assets/TestAssets/Audio/long.mp3\",\"loop\":true,\"playOnAwake\":true,\"volume\":0.8}"' | Out-Null
        Invoke-Nova "scene save --as $($TouchScene -replace '\\', '/')" | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $tt = Invoke-NovaJson "android export --out `"$(Join-Path $Out 'touch')`" --scenes `"$TouchScene`""
        Check 'touch scene exported' ($tt -and $tt.files -gt 0) $(if ($tt) { "$($tt.files) files" } else { 'no result' })
        # C# 스크립트: 검사 스크립트 (Start · Update · LINQ · Transform) 를 붙인 상자 → Mono 런타임 · BCL (참조하는 것만) · Assembly-CSharp 를 넣은 게임 데이터
        $probeDir = Join-Path $Project 'Assets\AndroidProbe'
        New-Item -ItemType Directory -Force $probeDir | Out-Null
        $probeFile = Join-Path $probeDir 'AndroidScriptProbe.cs'
        $probeBefore = if (Test-Path $probeFile) { Get-Content $probeFile -Raw } else { '' }
        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        @'
using System.Collections.Generic;
using System.Linq;
using NovaEngine;

// Android C# probe (Tools/tests/android.ps1): Start, Update, Transform and BCL (LINQ, strings) on the device Mono runtime
public class AndroidScriptProbe : MonoBehaviour
{
    public float speed = 90f;
    int frames;
    float yaw;
    bool resumed;

    void Start()
    {
        var squares = Enumerable.Range(1, 10).Select(i => i * i).ToList();
        var words = new Dictionary<string, int> { ["nova"] = 4, ["mono"] = 4 };
        Debug.Log($"AndroidScriptProbe start {name} speed={speed} sum={squares.Sum()} words={string.Join(",", words.Keys)}");
        Rect safe = Screen.safeArea;
        Debug.Log($"AndroidScriptProbe platform={Application.platform} mobile={Application.isMobilePlatform} screen={Screen.width}x{Screen.height} safe={safe.width:F0}x{safe.height:F0} orientation={Screen.orientation}");
        Screen.orientation = ScreenOrientation.LandscapeLeft;
    }

    void Update()
    {
        yaw += speed * Time.deltaTime;
        transform.rotation = Quaternion.Euler(0, yaw, 0);
        if (++frames == 60)
            Debug.Log($"AndroidScriptProbe frames={frames} yaw={transform.eulerAngles.y:F1} time={Time.time:F2}");
        for (int i = 0; i < Input.touchCount; i++)
        {
            Touch t = Input.GetTouch(i);
            if (t.phase == TouchPhase.Began)
                Debug.Log($"AndroidScriptProbe touch id={t.fingerId} at={t.position.x:F0},{t.position.y:F0} count={Input.touchCount}");
        }
        if (resumed)
        {
            resumed = false;
            Debug.Log($"AndroidScriptProbe resume dt={Time.deltaTime:F3}");
        }
    }

    void OnApplicationPause(bool paused)
    {
        Debug.Log($"AndroidScriptProbe pause={paused}");
        if (!paused) resumed = true;
    }

    void OnApplicationFocus(bool focused) => Debug.Log($"AndroidScriptProbe focus={focused}");
}
'@ | Set-Content -Encoding utf8 $probeFile
        $probeChanged = (Get-Content $probeFile -Raw) -ne $probeBefore
        # 에디터는 1 초마다 바뀐 스크립트를 찾는다 → Assembly-CSharp.dll 이 새로 써질 때까지 (안 바뀌었으면 그대로)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw.Elapsed.TotalSeconds -lt 90 -and (($inf -and $inf.compiling) -or ($probeChanged -and $now -eq $dllBefore)))
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Spinner --position 0,0.5,0' | Out-Null
        Invoke-Nova 'add-component Spinner AndroidScriptProbe' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.5,-4 --rotation 12,0,0' | Out-Null
        Invoke-Nova "scene save --as $($ScriptScene -replace '\\', '/')" | Out-Null
        $cs = Invoke-NovaJson "android export --out `"$(Join-Path $Out 'script')`" --scenes `"$ScriptScene`""
        Check 'C# runtime exported (Mono, referenced BCL only)' ($cs -and $cs.csharp.included) $(if ($cs) { "$($cs.csharp.assemblies) assemblies, $([math]::Round($cs.csharp.bytes / 1MB, 1)) MB $($cs.csharp.error)" } else { 'no result' })
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"; Restore-Layout
        if ($editorSettingsBefore) { [IO.File]::WriteAllBytes($editorSettings, $editorSettingsBefore) }
    }
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
function DeviceTest([string]$test, [string]$label, [string]$extra = '', [string]$name = $test)
{
    & $Adb -s $serial logcat -c
    & $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
    & $Adb -s $serial shell "am start -W -n com.nova.engine/android.app.NativeActivity -e test $test -e size 960x540 $extra" | Out-Null
    $line = $null
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 120 -and -not $line)
    {
        Start-Sleep -Milliseconds 500
        $line = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_TEST (\{.*\})' | Select-Object -Last 1)
    }
    & $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out "logcat_$name.txt")
    if (-not $line) { Check "$name test on device" $false "no NOVA_TEST line in 120 s (logcat_$name.txt)"; return $null }
    $j = $line.Matches[0].Groups[1].Value | ConvertFrom-Json
    Check "$name test on device" ([bool]$j.ok) ("{0}, load {1} ms, draw {2} ms {3}" -f $j.device, $j.loadMs, $j.drawMs, $j.error)
    if (-not $j.ok) { return $null }
    $bmp = Join-Path $Out "${name}_GLES.bmp"
    & $Adb -s $serial pull $j.image $bmp | Out-Null
    $ref = Join-Path $Out "${name}_DirectX11.png"
    if (-not ((Test-Path $bmp) -and (Test-Path $ref))) { Check "GLES = DX11 ($label)" $false 'image missing'; return $null }
    $c = [NovaImageCompare]::Compare($ref, $bmp, (Join-Path $Out "${name}_diff_GLES.png"))
    if (-not $c) { Check "GLES = DX11 ($label)" $false 'size differs'; return $null }
    if ($name -eq $test) { Check "GLES = DX11 ($label)" ($c[0] -le $MaxDiff) ('max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2]) }
    return $c
}
DeviceTest 'rhi' 'RHI scene' | Out-Null
DeviceTest 'gfx' 'Gfx layer scene' | Out-Null
DeviceTest 'scene' "engine scene $Scene" '-e frames 10' | Out-Null   # 엔진 전체 (EditorApp · 플레이어 순서) 로 게임 데이터의 첫 씬

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
        & $Adb -s $serial shell "am start -n com.nova.engine/android.app.NativeActivity $($script:StartMode)" | Out-Null
        Start-Sleep -Milliseconds 1200
    }
    return $false
}
function Near($c, [int]$r, [int]$g, [int]$b) { [Math]::Abs($c.R - $r) -le 12 -and [Math]::Abs($c.G - $g) -le 12 -and [Math]::Abs($c.B - $b) -le 12 }
function Rgb($c) { "rgb($($c.R),$($c.G),$($c.B))" }

& $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
$script:StartMode = '-e mode shell'
& $Adb -s $serial logcat -c
& $Adb -s $serial shell am start -W -n com.nova.engine/android.app.NativeActivity -e mode shell | Out-Null
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
    & $Adb -s $serial shell am start -n com.nova.engine/android.app.NativeActivity -e mode shell | Out-Null
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
$script:StartMode = ''

# ---- 엔진 플레이어 (창): 그냥 실행하면 게임 데이터의 첫 씬을 화면에 — 엔진 시작 이벤트, 프레임이 이어지는지, 화면 = DX11 기준 (1600x900)
Write-Host '[android] engine player (window)'
& $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
& $Adb -s $serial logcat -c
& $Adb -s $serial shell am start -W -n com.nova.engine/android.app.NativeActivity | Out-Null
$sw = [Diagnostics.Stopwatch]::StartNew(); $eng = $null
while ($sw.Elapsed.TotalSeconds -lt 60 -and -not $eng)
{
    Start-Sleep -Milliseconds 500
    $eng = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_EVENT (\{"event":"engine".*\})' | Select-Object -Last 1)
}
Check 'engine starts in the window' ([bool]$eng) $(if ($eng) { $eng.Matches[0].Groups[1].Value } else { 'no engine event in 60 s' })
if ($eng)
{
    # 시작 시간: 셰이더 효과는 pass 를 처음 쓸 때 만든다 (예전에는 모든 technique 을 미리 + 샘플러마다 정규식 — 약 10 초)
    $loadMs = [double]($eng.Matches[0].Groups[1].Value | ConvertFrom-Json).loadMs
    Check 'engine starts quickly' ($loadMs -lt 3000) "loadMs $loadMs (< 3000)"
}
if ($eng)
{
    EnsureFront | Out-Null
    Start-Sleep -Seconds 3
    $ws = Screen 'engine_window'
    $ref = Join-Path $Out 'window_DirectX11.png'
    if (Test-Path $ref)
    {
        $c = [NovaImageCompare]::Compare($ref, (Join-Path $Out 'engine_window.png'), (Join-Path $Out 'window_diff.png'))
        # 창에서는 프레임이 계속 돈다 (TAA 지터 · 시간) → 화소 차이는 조금 있다. 8 넘는 화소가 1 % 아래면 같은 그림
        if ($c) { Check 'window = DX11 (engine player)' ($c[2] -lt 1.0) ('max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2]) }
        else { Check 'window = DX11 (engine player)' $false 'size differs' }
    }
    $sw = [Diagnostics.Stopwatch]::StartNew(); $fr = $null
    while ($sw.Elapsed.TotalSeconds -lt 30 -and -not $fr)
    {
        Start-Sleep -Milliseconds 500
        $fr = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_EVENT (\{"event":"frame".*\})' | Select-Object -Last 1)
    }
    Check 'engine frames keep coming' ([bool]$fr) $(if ($fr) { $fr.Matches[0].Groups[1].Value } else { 'no frame event (300 frames) in 30 s' })
}
& $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out 'logcat_engine.txt')
& $Adb -s $serial shell am force-stop com.nova.engine | Out-Null

# ---- 6) 텍스처 압축 (Unity 의 Android Texture Compression): 구운 ASTC · ETC2 를 기기가 그대로 GPU 에 올려 그린 씬 = DX11 기준 (PC 는 BC → 압축 차이만큼은 허용)
$gameDir = Join-Path $Root 'Android\build\assets\game'
foreach ($tc in $TextureFormats)
{
    $src = Join-Path $Out "tex_$tc\game"
    if (-not (Test-Path $src)) { continue }
    Write-Host "[android] texture compression $tc"
    Remove-Item -Recurse -Force $gameDir -ErrorAction SilentlyContinue
    Copy-Item -Recurse $src $gameDir
    $py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
    $inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String)
    if (-not ($py -match 'apk .*nova\.apk' -and $inst -match 'Success')) { Check "texture $tc APK" $false 'build or install failed'; continue }
    $c = DeviceTest 'scene' "textures $tc" '-e frames 10' "tex_$tc"
    if ($c)
    {
        # 같은 씬이 압축 형식 차이만 남기면 8 넘는 화소는 조금 (텍스처가 빠지거나 깨지면 화면 대부분이 바뀐다)
        Check "GLES $tc = DX11 BC ($TextureScene)" ($c[1] -lt 2.0 -and $c[2] -lt 5.0) ('max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2])
    }
    $glErr = @(Get-Content (Join-Path $Out "logcat_tex_$tc.txt") -ErrorAction SilentlyContinue | Select-String 'texture|\.dds' | Select-String -Pattern 'fail|error|not found')
    Check "no texture errors ($tc)" ($glErr.Count -eq 0) $(if ($glErr.Count) { $glErr[0].Line.Trim() } else { 'logcat clean' })
}

# ---- 7) 모델: Assimp 가 없는 기기가 PC 가 구운 메시 캐시로 캐릭터를 그리고, 엔진에 함께 넣은 Animation 패키지의 Animator 가 움직인다
$src = Join-Path $Out 'models\game'
if (Test-Path $src)
{
    Write-Host '[android] models (mesh cache + Animator package)'
    Remove-Item -Recurse -Force $gameDir -ErrorAction SilentlyContinue
    Copy-Item -Recurse $src $gameDir
    $py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
    $inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String)
    if (-not ($py -match 'apk .*nova\.apk' -and $inst -match 'Success')) { Check 'models APK' $false 'build or install failed' }
    else
    {
        Copy-Item (Join-Path $Out 'models_DirectX11.png') (Join-Path $Out 'models_late_DirectX11.png') -ErrorAction SilentlyContinue
        $c = DeviceTest 'scene' 'models' '-e frames 10' 'models'
        # DX11 기준은 편집 중 (첫 자세), 기기는 플레이어라 10 프레임 동안 Idle 이 조금 움직인다 → 캐릭터 둘레만 다를 수 있다
        if ($c) { Check "GLES models = DX11 ($ModelScene)" ($c[1] -lt 3.0 -and $c[2] -lt 6.0) ('max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2]) }
        $log = Get-Content (Join-Path $Out 'logcat_models.txt') -ErrorAction SilentlyContinue
        $pk = @($log | Select-String '\[Packages\] (\S+) \(static\)' | ForEach-Object { $_.Matches[0].Groups[1].Value })
        Check 'packages built into the engine' ($pk -contains 'com.nova.animation') ($pk -join ', ')
        $meshErr = @($log | Select-String 'mesh|model|Assimp' | Select-String -Pattern 'fail|error|not available|missing')
        Check 'no model load errors' ($meshErr.Count -eq 0) $(if ($meshErr.Count) { $meshErr[0].Line.Trim() } else { 'logcat clean' })
        $late = DeviceTest 'scene' 'models later' '-e frames 90' 'models_late'
        $a = Join-Path $Out 'models_GLES.bmp'; $b = Join-Path $Out 'models_late_GLES.bmp'
        if ((Test-Path $a) -and (Test-Path $b))
        {
            # 같은 씬을 10 · 90 프레임 그린 두 그림: Animator 가 돌면 캐릭터 화소가 바뀐다 (멈춰 있으면 0)
            $d = [NovaImageCompare]::Compare($a, $b, (Join-Path $Out 'models_anim_diff.png'))
            Check 'Animator plays on device' ($d -and $d[2] -gt 0.05) $(if ($d) { 'frame 10 vs 90: max {0}, >8: {1:N2}% of pixels' -f $d[0], $d[2] } else { 'compare failed' })
        }
    }
}

# ---- 7b) 터치 → UI · Input: 앱 창에서 input tap (Toggle 끄기) · input swipe (Slider 끌기) → 화면이 바뀌고, 엔진 Input 이 Touch (Began · Ended) 를 받는다
$src = Join-Path $Out 'touch\game'
if (Test-Path $src)
{
    Write-Host '[android] touch → UI · Input'
    Remove-Item -Recurse -Force $gameDir -ErrorAction SilentlyContinue
    Copy-Item -Recurse $src $gameDir
    $py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
    $inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String)
    & $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
    & $Adb -s $serial logcat -c
    & $Adb -s $serial shell am start -W -n com.nova.engine/android.app.NativeActivity | Out-Null
    $sw = [Diagnostics.Stopwatch]::StartNew(); $eng = $null
    while ($sw.Elapsed.TotalSeconds -lt 60 -and -not $eng)
    {
        Start-Sleep -Milliseconds 500
        $eng = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_EVENT \{"event":"engine"' | Select-Object -Last 1)
    }
    if (-not ($eng -and (EnsureFront))) { Check 'touch scene starts' $false 'no engine event / not in front' }
    else
    {
        Start-Sleep -Seconds 2
        # 두 그림의 한 영역에서 (16 넘게) 다른 화소 수
        function RegionDiff($a, $b, [int]$x0, [int]$y0, [int]$x1, [int]$y1)
        {
            $n = 0
            for ($y = $y0; $y -lt $y1; $y += 2) { for ($x = $x0; $x -lt $x1; $x += 2) {
                $p = $a.GetPixel($x, $y); $q = $b.GetPixel($x, $y)
                if ([Math]::Abs($p.R - $q.R) + [Math]::Abs($p.G - $q.G) + [Math]::Abs($p.B - $q.B) -gt 48) { $n++ } } }
            return $n
        }
        $t0 = Screen 'touch_0'
        & $Adb -s $serial shell input tap 450 450 | Out-Null   # 체크 상자 (탭 하나 = 한 프레임보다 짧을 수 있다 — 잃지 않아야 한다)
        Start-Sleep -Milliseconds 1200
        $t1 = Screen 'touch_1_toggle'
        $dToggle = RegionDiff $t0 $t1 405 405 495 495
        Check 'tap toggles the UI Toggle' ($dToggle -gt 100) "check box pixels changed: $dToggle"
        & $Adb -s $serial shell input swipe 520 668 1000 668 500 | Out-Null   # 손잡이를 오른쪽으로
        Start-Sleep -Milliseconds 1200
        $t2 = Screen 'touch_2_slider'
        $dSlider = RegionDiff $t1 $t2 470 625 1130 710
        Check 'swipe drags the UI Slider' ($dSlider -gt 200) "slider pixels changed: $dSlider"
        $log = @(& $Adb -s $serial logcat -d -s NOVA:I | Select-String '\[Input\] touch (\d+) (began|ended) at (\d+),(\d+)')
        $began = @($log | Where-Object { $_.Matches[0].Groups[2].Value -eq 'began' })
        $first = if ($began.Count) { $began[0].Matches[0] } else { $null }
        Check 'engine Input gets touches (Input.GetTouch)' ($began.Count -ge 2 -and $first -and [Math]::Abs([int]$first.Groups[3].Value - 450) -le 2 -and [Math]::Abs([int]$first.Groups[4].Value - 450) -le 2) `
            $(if ($first) { "$($began.Count) began / $(@($log).Count - $began.Count) ended, first at $($first.Groups[3].Value),$($first.Groups[4].Value) (tapped 450,450)" } else { 'no [Input] touch line' })
        # 소리: frame 이벤트의 audioFrames 가 늘고 (AAudio 가 돈다) 레벨이 0 보다 크다, 오디오 서버에 이 앱의 트랙이 재생 중 → HOME 이면 멈춘다
        $sw = [Diagnostics.Stopwatch]::StartNew(); $af = @()
        while ($sw.Elapsed.TotalSeconds -lt 20 -and $af.Count -lt 2)
        {
            Start-Sleep -Milliseconds 500
            $af = @(& $Adb -s $serial logcat -d -s NOVA:I | Select-String '"audioFrames":(\d+),"audioPeak":([\d.]+)' | ForEach-Object { $_.Matches[0] })
        }
        $active = { @(& $Adb -s $serial shell dumpsys media.audio_flinger | Select-String '(\d+) Tracks of which (\d+) are active' | ForEach-Object { [int]$_.Matches[0].Groups[2].Value } | Measure-Object -Sum).Sum }
        $playing = & $active
        if ($af.Count -ge 2)
        {
            $grow = [int64]$af[-1].Groups[1].Value - [int64]$af[-2].Groups[1].Value
            $peak = [double]$af[-1].Groups[2].Value
            Check 'audio plays (XAudio2 → AAudio)' ($grow -gt 48000 -and $peak -gt 0.01 -and $playing -ge 1) ("{0} frames in 300 game frames, peak {1}, active tracks {2}" -f $grow, $peak, $playing)
        }
        else { Check 'audio plays (XAudio2 → AAudio)' $false 'no audioFrames in frame events' }
        & $Adb -s $serial shell input keyevent KEYCODE_HOME | Out-Null
        Start-Sleep -Seconds 2
        $after = & $active
        Check 'audio stops in the background' ($after -lt $playing) "active tracks $playing → $after after HOME"
    }
    & $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F | Set-Content -Encoding utf8 (Join-Path $Out 'logcat_touch.txt')
    & $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
}

# ---- 7c) C# 스크립트: 기기의 Mono 가 Assembly-CSharp 를 읽어 Start · Update 를 부른다 (logcat 의 Debug.Log)
$src = Join-Path $Out 'script\game'
if (Test-Path (Join-Path $src 'Managed'))
{
    Write-Host '[android] C# scripts (Mono)'
    Remove-Item -Recurse -Force $gameDir -ErrorAction SilentlyContinue
    Copy-Item -Recurse $src $gameDir
    $py = (& python (Join-Path $Root 'Android\build.py') --abi $abi 2>&1 | Out-String)
    $inst = (& $Adb -s $serial install -r $apk 2>&1 | Out-String)
    & $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
    & $Adb -s $serial logcat -c
    & $Adb -s $serial shell am start -W -n com.nova.engine/android.app.NativeActivity | Out-Null
    $sw = [Diagnostics.Stopwatch]::StartNew(); $done = $null
    while ($sw.Elapsed.TotalSeconds -lt 40 -and -not $done)
    {
        Start-Sleep -Milliseconds 500
        $done = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'AndroidScriptProbe frames=60' | Select-Object -Last 1)
    }
    $log = @(& $Adb -s $serial logcat -d -s NOVA:* AndroidRuntime:E DEBUG:F libc:F)
    $log | Set-Content -Encoding utf8 (Join-Path $Out 'logcat_script.txt')
    $ready = $log | Select-String '\[Script\] (Mono runtime ready.*)' | Select-Object -First 1
    $loaded = $log | Select-String '\[Script\] (Assembly-CSharp loaded.*)' | Select-Object -First 1
    Check 'Mono runtime starts on the device' ([bool]$ready -and [bool]$loaded) $(if ($ready) { $ready.Matches[0].Groups[1].Value + '; ' + $(if ($loaded) { $loaded.Matches[0].Groups[1].Value } else { 'no Assembly-CSharp' }) } else { 'no "[Script] Mono runtime ready" (logcat_script.txt)' })
    $start = $log | Select-String 'AndroidScriptProbe start (.*)' | Select-Object -First 1
    Check 'C# Start runs (LINQ, Dictionary)' ($start -and $start.Line -match 'sum=385' -and $start.Line -match 'words=nova,mono') $(if ($start) { $start.Matches[0].Groups[1].Value } else { 'no Start log' })
    Check 'C# Update runs (Transform, Time)' ($done -and $done.Line -match 'yaw=(\d+)' -and [double]$Matches[1] -gt 0) $(if ($done) { ($done.Line -split 'Log: ')[-1] } else { 'no frame 60 log in 40 s' })
    $plat = $log | Select-String 'AndroidScriptProbe (platform=.*)' | Select-Object -First 1
    Check 'C# Application.platform · Screen.safeArea' ($plat -and $plat.Line -match 'platform=Android' -and $plat.Line -match 'mobile=True' -and $plat.Line -match 'screen=\d{3,}x\d{3,}' -and $plat.Line -match 'safe=\d{3,}x\d{3,}') $(if ($plat) { $plat.Matches[0].Groups[1].Value } else { 'no platform log' })
    $orient = $log | Select-String 'orientation-request.*"android":(\d+)' | Select-Object -First 1
    Check 'C# Screen.orientation = LandscapeLeft → setRequestedOrientation' ($orient -and $orient.Matches[0].Groups[1].Value -eq '0') $(if ($orient) { $orient.Line.Substring($orient.Line.IndexOf('{')) } else { 'no orientation-request event' })
    # 터치: 앱 창 (1600x900) 의 800,300 → C# 은 Unity 처럼 왼쪽 아래 기준 (800, 600)
    EnsureFront | Out-Null
    & $Adb -s $serial logcat -c
    & $Adb -s $serial shell input tap 800 300 | Out-Null
    Start-Sleep -Milliseconds 1500
    $touch = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'AndroidScriptProbe touch (.*)' | Select-Object -First 1)
    Check 'C# Input.GetTouch (Unity coordinates)' ($touch -and $touch.Line -match 'at=800,600') $(if ($touch) { $touch.Matches[0].Groups[1].Value + ' (tapped 800,300 from the top)' } else { 'no touch log' })
    # 앱이 뒤로 → 앞으로: OnApplicationPause / OnApplicationFocus, 다시 돌아온 첫 프레임의 deltaTime 이 뒤에 있던 시간만큼 튀지 않는다
    & $Adb -s $serial logcat -c
    & $Adb -s $serial shell input keyevent KEYCODE_HOME | Out-Null
    Start-Sleep -Seconds 3
    & $Adb -s $serial shell am start -n com.nova.engine/android.app.NativeActivity | Out-Null
    Start-Sleep -Seconds 3
    $pl = @(& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'AndroidScriptProbe (pause|focus|resume)' | ForEach-Object { ($_.Line -split 'AndroidScriptProbe ')[-1] })
    $dt = ($pl | Where-Object { $_ -match '^resume dt=([\d.]+)' } | Select-Object -First 1)
    Check 'C# OnApplicationPause · OnApplicationFocus' (($pl -contains 'pause=True') -and ($pl -contains 'pause=False') -and ($pl -contains 'focus=False') -and ($pl -contains 'focus=True')) ($pl -join ', ')
    Check 'time does not jump after resume' ($dt -and [double]($dt -replace '.*dt=', '') -lt 0.2) $(if ($dt) { $dt + ' (in the background 3 s)' } else { 'no resume log' })
    $crash = @($log | Select-String 'FATAL|signal \d')
    Check 'no crash with Mono' ($crash.Count -eq 0) $(if ($crash.Count) { $crash[0].Line } else { 'logcat clean' })
    & $Adb -s $serial shell am force-stop com.nova.engine | Out-Null
}

# ---- 8) Build Settings 의 Android Build And Run (에디터가 셰이더 · 게임 데이터 → APK → 설치 → 실행, 플레이어 라이브러리는 3 단계에서 만든 것)
if (-not $SkipEditor)
{
    Write-Host '[android] editor Build And Run'
    $engineRoot = if ($env:NOVA_ENGINE) { Split-Path (Split-Path $env:NOVA_ENGINE) } else { $Root }
    $playerDir = Join-Path $engineRoot 'Android\Player\x86_64'
    New-Item -ItemType Directory -Force $playerDir | Out-Null
    Copy-Item (Join-Path $Root 'Android\build\cmake\x86_64-Release\libnova.so') $playerDir -Force
    # Build Settings 창이 Android 를 고른 채로 열리게 (검사 프로젝트의 EditorBuildSettings.json)
    $ebs = Join-Path $Project 'ProjectSettings\EditorBuildSettings.json'
    if (Test-Path $ebs) { $ej = Get-Content $ebs -Raw | ConvertFrom-Json; $ej | Add-Member -NotePropertyName activePlatform -NotePropertyValue 'Android' -Force; $ej | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 $ebs }
    Backup-Layout
    $ed = Start-TestEditor
    try
    {
        $apkOut = Join-Path $Out 'buildrun\Game.apk'
        & $Adb -s $serial logcat -c   # 앞 단계의 engine 이벤트와 섞이지 않게
        $st = Invoke-NovaJson "android build --out `"$apkOut`" --run --device $serial"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Start-Sleep -Milliseconds 1000; $b = Invoke-NovaJson 'android build-status' } while ($sw.Elapsed.TotalSeconds -lt 300 -and $b -and $b.running)
        Check 'editor Android build (APK)' ($st -and $b -and $b.success) $(if ($b) { if ($b.success) { "{0:N1} MB, {1:N1} s, device {2}" -f ($b.bytes / 1MB), $b.seconds, $b.device } else { $b.error } } else { 'no status' })
        if ($b -and $b.success)
        {
            # 라이브러리를 deflate 로 (전에는 압축 없이 30 MB) · Default Orientation → manifest 의 screenOrientation (Auto Rotation = fullUser 13)
            Check 'APK libraries are compressed' ($b.bytes -lt 25MB) ("{0:N1} MB" -f ($b.bytes / 1MB))
            $bt = (Get-ChildItem (Join-Path $Sdk 'build-tools') -Directory | Sort-Object { [version]($_.Name -replace '[^\d.]', '') } | Select-Object -Last 1).FullName
            $xml = (& (Join-Path $bt 'aapt2.exe') dump xmltree --file AndroidManifest.xml $apkOut 2>&1 | Out-String)
            Check 'manifest screenOrientation from Player Settings' ($xml -match 'screenOrientation.*=13') (($xml -split "`n" | Where-Object { $_ -match 'screenOrientation' } | Select-Object -First 1) -replace '\s+', ' ')
        }
        Invoke-Nova 'window build-settings' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
        Invoke-Nova "screenshot `"$(Join-Path $Out 'build_settings_android.png')`" --view editor" | Out-Null
        Invoke-Nova 'window build-settings --close' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)"; Restore-Layout }
    if ($b -and $b.success)
    {
        $man = (& $Adb -s $serial shell "dumpsys window" | Select-String 'mCurrentFocus' | Select-Object -First 1).Line
        $sw = [Diagnostics.Stopwatch]::StartNew(); $eng = $null
        while ($sw.Elapsed.TotalSeconds -lt 60 -and -not $eng)
        {
            Start-Sleep -Milliseconds 500
            $eng = (& $Adb -s $serial logcat -d -s NOVA:I | Select-String 'NOVA_EVENT (\{"event":"engine".*\})' | Select-Object -Last 1)
        }
        Check 'built game runs on the device' ([bool]$eng -and $man -notmatch 'com\.nova\.engine') $(if ($eng) { "$($man.Trim()) — $($eng.Matches[0].Groups[1].Value)" } else { "no engine event in 60 s ($($man))" })
    }
}

if (-not $KeepEmulator) { MuMu @('control', '-v', $index, 'shutdown') | Out-Null }

$results | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
$fail = @($results | Where-Object { $_.Result -eq 'FAIL' }).Count
Write-Host ("{0} passed, {1} failed  → {2}" -f ($results.Count - $fail), $fail, $Out)
exit $fail
