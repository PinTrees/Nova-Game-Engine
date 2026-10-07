# NOVA 자동 검사 공용 (run_tests.ps1 이 dot-source 한다).
#  - 테스트 에디터를 백그라운드로 띄우고 VRAM·로그 감시를 건다 (이상하면 그 에디터만 끈다 — 사용자의 다른 에디터는 건드리지 않는다)
#  - nova 명령 실행, 레이아웃 파일(Binaries/nova_layout_v2.ini, 사용자 에디터와 공유) 백업·복원, 이미지 비교(C#, 파이썬 없이)
$ErrorActionPreference = 'Continue'
$script:Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:Nova = Join-Path $Root 'Binaries\nova.exe'
# NOVA_ENGINE (복사 엔진의 NovaEngine.exe) 을 쓰면 그 옆의 nova.exe 를 (명령 목록이 엔진과 같게)
if ($env:NOVA_ENGINE -and (Test-Path (Join-Path (Split-Path $env:NOVA_ENGINE) 'nova.exe'))) { $script:Nova = Join-Path (Split-Path $env:NOVA_ENGINE) 'nova.exe' }
$script:EditorLog = Join-Path $Root 'Binaries\Logs\Editor.log'
$script:LayoutIni = Join-Path $Root 'Binaries\nova_layout_v2.ini'
# 복사 엔진이면 로그 · 배치 파일도 그 엔진의 것 (저장소 Binaries 의 옛 로그를 읽지 않게)
if ($env:NOVA_ENGINE -and (Test-Path $env:NOVA_ENGINE))
{
    $script:EditorLog = Join-Path (Split-Path $env:NOVA_ENGINE) 'Logs\Editor.log'
    $script:LayoutIni = Join-Path (Split-Path $env:NOVA_ENGINE) 'nova_layout_v2.ini'
}

function Get-VramMB
{
    try { [int]((& nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits 2>$null | Select-Object -First 1).Trim()) } catch { 0 }
}

# 에디터를 띄우고 감시 작업을 건다. 돌려주는 객체로 Invoke-Nova / Stop-TestEditor
function Start-TestEditor([switch]$OpenGL, [switch]$Vulkan, [int]$MaxGrowMB = 1500, [int]$WatchSeconds = 900)
{
    $base = Get-VramMB
    if ($OpenGL) { $out = & $Nova open $script:Project --background --graphics opengl --timeout 300 2>&1 | Out-String }
    elseif ($Vulkan) { $out = & $Nova open $script:Project --background --graphics vulkan --timeout 300 2>&1 | Out-String }
    else { $out = & $Nova open $script:Project --background --timeout 300 2>&1 | Out-String }
    if ($out -notmatch 'pid (\d+)') { throw "editor did not open: $out" }
    $editorPid = [int]$Matches[1]
    $job = Start-Job -ArgumentList $editorPid, $base, $MaxGrowMB, $WatchSeconds, $EditorLog -ScriptBlock {
        param($editorPid, $base, $maxGrow, $seconds, $log)
        $sw = [Diagnostics.Stopwatch]::StartNew(); $peak = $base
        while ($sw.Elapsed.TotalSeconds -lt $seconds)
        {
            Start-Sleep -Milliseconds 250
            if (-not (Get-Process -Id $editorPid -ErrorAction SilentlyContinue)) { return "editor closed, VRAM peak +$($peak - $base) MB" }
            $v = 0; try { $v = [int]((& nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | Select-Object -First 1).Trim()) } catch { }
            if ($v -gt $peak) { $peak = $v }
            if ($base -gt 0 -and $v - $base -gt $maxGrow) { Stop-Process -Id $editorPid -Force; return "STOP: VRAM +$($v - $base) MB - editor killed" }
            $bad = Select-String -Path $log -Pattern 'device removed|DEVICE_REMOVED|device lost|Present failed|VRAM budget: refused|\[CRASH\]' -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($bad) { Stop-Process -Id $editorPid -Force; return "STOP: $($bad.Line) - editor killed" }
        }
        return "watch ended, VRAM peak +$($peak - $base) MB"
    }
    [pscustomobject]@{ Pid = $editorPid; Job = $job; OpenGL = [bool]$OpenGL }
}

function Test-EditorAlive($editor) { [bool](Get-Process -Id $editor.Pid -ErrorAction SilentlyContinue) }

# nova 명령 한 줄 (셸처럼 "…" 로 묶기). 돌려주는 값 = 출력 글자 (오류 포함)
function Invoke-Nova([string]$line)
{
    $parts = @($line -split ' (?=(?:[^"]*"[^"]*")*[^"]*$)' | ForEach-Object { $_.Trim('"') })
    (& $Nova @parts --project $script:Project 2>&1 | Out-String).TrimEnd()
}

# JSON 결과 (--json)
function Invoke-NovaJson([string]$line)
{
    $t = Invoke-Nova "$line --json"
    try { $t | ConvertFrom-Json } catch { $null }
}

# 에디터 닫기 (Scene 탭을 앞으로 되돌린 뒤 — 레이아웃 파일은 사용자 에디터와 공유) + 감시 결과
function Stop-TestEditor($editor)
{
    if (Test-EditorAlive $editor)
    {
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        & $Nova quit --force --project $script:Project 2>&1 | Out-Null
        for ($i = 0; $i -lt 40 -and (Test-EditorAlive $editor); $i++) { Start-Sleep -Milliseconds 250 }
    }
    Stop-Job $editor.Job -ErrorAction SilentlyContinue
    $r = Receive-Job $editor.Job -ErrorAction SilentlyContinue
    Remove-Job $editor.Job -Force -ErrorAction SilentlyContinue
    if ($r) { $r } else { 'watch ok' }
}

function Backup-Layout { if (Test-Path $LayoutIni) { Copy-Item $LayoutIni "$LayoutIni.testbackup" -Force } }
function Restore-Layout { if (Test-Path "$LayoutIni.testbackup") { Copy-Item "$LayoutIni.testbackup" $LayoutIni -Force; Remove-Item "$LayoutIni.testbackup" -Force } }

# ---- 이미지 비교: 최대 차이, 평균, 8 넘게 다른 픽셀 비율(%). 크기가 다르면 $null. diffPng 를 주면 차이 그림(×8)
if (-not ('NovaImageCompare' -as [type]))
{
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class NovaImageCompare
{
    static byte[] Pixels(Bitmap b)
    {
        var r = new Rectangle(0, 0, b.Width, b.Height);
        BitmapData d = b.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        byte[] px = new byte[d.Stride * b.Height];
        Marshal.Copy(d.Scan0, px, 0, px.Length);
        b.UnlockBits(d);
        return px;
    }
    public static double[] Compare(string pathA, string pathB, string diffPng)
    {
        using (var a = new Bitmap(pathA))
        using (var b = new Bitmap(pathB))
        {
            if (a.Width != b.Width || a.Height != b.Height) return null;
            byte[] pa = Pixels(a), pb = Pixels(b);
            int max = 0, over = 0; double sum = 0; int n = a.Width * a.Height;
            byte[] pd = !string.IsNullOrEmpty(diffPng) ? new byte[pa.Length] : null;   // PowerShell 의 $null 은 "" 로 온다
            for (int i = 0; i < pa.Length; i += 4)
            {
                int m = 0;
                for (int c = 0; c < 3; ++c)
                {
                    int v = Math.Abs(pa[i + c] - pb[i + c]);
                    sum += v;
                    if (v > m) m = v;
                    if (pd != null) pd[i + c] = (byte)Math.Min(255, v * 8);
                }
                if (pd != null) pd[i + 3] = 255;
                if (m > max) max = m;
                if (m > 8) over++;
            }
            if (pd != null)
            {
                using (var o = new Bitmap(a.Width, a.Height, PixelFormat.Format32bppArgb))
                {
                    BitmapData d = o.LockBits(new Rectangle(0, 0, o.Width, o.Height), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                    Marshal.Copy(pd, 0, d.Scan0, pd.Length);
                    o.UnlockBits(d);
                    o.Save(diffPng, ImageFormat.Png);
                }
            }
            return new double[] { max, sum / (3.0 * n), 100.0 * over / n };
        }
    }
}
"@
}

# ---- 결과 모으기
$script:Results = New-Object System.Collections.Generic.List[object]
function Add-Result([string]$suite, [string]$name, [bool]$pass, [string]$detail)
{
    $script:Results.Add([pscustomobject]@{ Suite = $suite; Test = $name; Result = $(if ($pass) { 'PASS' } else { 'FAIL' }); Detail = $detail })
    $mark = if ($pass) { 'PASS' } else { 'FAIL' }
    Write-Host ("  [{0}] {1} — {2}" -f $mark, $name, $detail)
}

# MuMu 를 화면에 보이지 않게 켠다 (검사 전용 VM): 켜는 순간부터 0.3 초마다 창을 화면 밖으로 옮기고 숨긴다.
#  MuMu 는 창 자리를 기억한다 (window_save_rect) → 한 번 밖으로 옮겨 두면 다음부터는 처음부터 화면 밖에서 뜬다
function Start-MuMuHidden([string]$MuMuExe, [string]$Index, [int]$TimeoutSec = 180)
{
    $state = (& $MuMuExe info -v $Index 2>&1 | Out-String) | ConvertFrom-Json
    # VM (MuMuVMMHeadless) 이 죽었는데 창 쪽은 start_finished 로 남는 일이 있다 → 껐다 다시 켠다
    if ($state.is_android_started -and $state.headless_pid -and -not (Get-Process -Id $state.headless_pid -ErrorAction SilentlyContinue))
    {
        & $MuMuExe control -v $Index shutdown 2>&1 | Out-Null
        Start-Sleep -Seconds 3
        $state = (& $MuMuExe info -v $Index 2>&1 | Out-String) | ConvertFrom-Json
    }
    if (-not $state.is_android_started) { & $MuMuExe control -v $Index launch 2>&1 | Out-Null }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $TimeoutSec)
    {
        & $MuMuExe control -v $Index layout_window -px -32000 -py -32000 2>&1 | Out-Null
        & $MuMuExe control -v $Index hide_window 2>&1 | Out-Null
        $state = (& $MuMuExe info -v $Index 2>&1 | Out-String) | ConvertFrom-Json
        if ($state.player_state -eq 'start_finished' -and $state.adb_port) { break }
        Start-Sleep -Milliseconds 300
    }
    & $MuMuExe control -v $Index hide_window 2>&1 | Out-Null
    return $state
}

# 플레이어 (안드로이드 · 웹) 내비게이션 검사 장면: 3D (바닥 + 가운데 벽, NavMeshAgent 캡슐) + 2D (x 100 · y 100 둘레의 XY — 스프라이트 바닥 + Box Collider 2D 벽,
#  NavMeshSurface Plane = 2D) + 검사 스크립트 (Tools/tests/nav_player_probe.cs). 두 NavMesh 를 편집기에서 굽고 씬과 함께 저장한다.
#  돌려주는 값: 씬 경로 (실패하면 $null). 정리: Remove-NavPlayerScene
function New-NavPlayerScene([string]$OutDir)
{
    $probeDir = Join-Path $script:Project 'Assets\NavPlayerProbe'
    New-Item -ItemType Directory -Force $probeDir | Out-Null
    $gameDll = Join-Path $script:Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
    $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
    Copy-Item (Join-Path $PSScriptRoot 'nav_player_probe.cs') (Join-Path $probeDir 'NavPlayerProbe.cs') -Force
    $sw = [Diagnostics.Stopwatch]::StartNew()
    do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
    while ($sw.Elapsed.TotalSeconds -lt 60 -and (($inf -and $inf.compiling) -or $now -eq $dllBefore))
    Invoke-Nova 'package add com.nova.ai.navigation' | Out-Null
    foreach ($l in @('scene new --force',
                     'create cube --name Ground --position 0,-0.5,0 --scale 20,1,20', 'create cube --name Wall --position 0,1,0 --scale 0.5,2,8',
                     'create empty --name Surface3D', 'add-component Surface3D NavMeshSurface',
                     'create capsule --name Npc3D --position -5,1,0', 'remove-component Npc3D CapsuleCollider', 'add-component Npc3D NavMeshAgent --values "{\"baseOffset\":1}"',
                     'create empty --name Ground2D --position 100,100,1 --scale 20,14,1', 'add-component Ground2D SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[0.85,0.85,0.8,1]}"',
                     'create empty --name Wall2D --position 100,100,0 --scale 0.5,8,1', 'add-component Wall2D SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[0.3,0.3,0.35,1],\"sortingOrder\":1}"',
                     'add-component Wall2D BoxCollider2D',
                     'create empty --name Surface2D --position 100,100,0', 'add-component Surface2D NavMeshSurface --values "{\"plane\":1}"',
                     'create empty --name Npc2D --position 95,100,-0.5', 'add-component Npc2D SpriteRenderer --values "{\"sprite\":\"builtin:Circle\",\"sortingOrder\":2}"',
                     'add-component Npc2D NavMeshAgent --values "{\"radius\":0.3}"',
                     'create empty --name Probe', 'add-component Probe NavPlayerProbe',
                     'set "Main Camera" --position 0,9,-9 --rotation 45,0,0')) { Invoke-Nova $l | Out-Null }
    $bake = Join-Path $OutDir 'nav_player_bake.cs'
    'GameObject.Find("Surface3D").GetComponent<NovaEngine.AI.NavMeshSurface>().BuildNavMesh(); GameObject.Find("Surface2D").GetComponent<NovaEngine.AI.NavMeshSurface>().BuildNavMesh(); return "baked";' | Set-Content -Encoding utf8 $bake
    $b = Invoke-NovaJson "exec --file `"$bake`""
    if (-not $b -or "$($b.result)" -ne 'baked') { return $null }
    $scene = 'Assets/Scenes/NavPlayer.scene'
    Invoke-Nova "scene save --as $scene" | Out-Null
    return $scene
}

function Remove-NavPlayerScene
{
    foreach ($f in @('Assets\Scenes\NavPlayer.scene', 'Assets\Scenes\NavPlayer.scene.meta', 'Assets\NavMesh-Surface3D.navmesh', 'Assets\NavMesh-Surface3D.navmesh.meta',
                     'Assets\NavMesh-Surface2D.navmesh', 'Assets\NavMesh-Surface2D.navmesh.meta', 'Assets\NavPlayerProbe.meta'))
    {
        Remove-Item (Join-Path $script:Project $f) -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -Recurse -Force (Join-Path $script:Project 'Assets\NavPlayerProbe') -ErrorAction SilentlyContinue
}

# "NavProbe start …" · "NavProbe done …" 로그 두 줄 → 검사 결과 (안드로이드 · 웹 같은 기준)
function Test-NavPlayerLog([string[]]$lines, [string]$platform)
{
    $start = [string]($lines | Where-Object { $_ -match 'NavProbe start' } | Select-Object -Last 1)
    $done = [string]($lines | Where-Object { $_ -match 'NavProbe done' } | Select-Object -Last 1)
    $s = $start -replace '^.*NavProbe start ', ''
    $d = $done -replace '^.*NavProbe done ', ''
    $pathOk = $start -match "platform=$platform" -and $start -match 'path3d=True:(\d+):([\d.]+)' -and [int]$Matches[1] -ge 3 -and [double]$Matches[2] -gt 4 -and
        $start -match 'path2d=True:(\d+):([\d.]+)' -and [int]$Matches[1] -ge 3 -and [double]$Matches[2] -gt 4 -and $start -match 'dest=True,True'
    $walkOk = $done -match 'arrived=True,True' -and $done -match 'p2=[\d.\-]+,[\d.\-]+,(-?[\d.]+)' -and [math]::Abs([double]$Matches[1] + 0.5) -lt 0.01
    return [pscustomobject]@{ PathOk = [bool]$pathOk; WalkOk = [bool]$walkOk; Start = $s; Done = $d }
}
