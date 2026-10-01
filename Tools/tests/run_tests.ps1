# NOVA 자동 회귀 검사 — 실행 중인 에디터를 NOVA CLI 로 다뤄 확인한다 (테스트 프로젝트에서만, 저장하지 않음).
#
#   powershell -ExecutionPolicy Bypass -File Tools\tests\run_tests.ps1                 # quick (약 4~6 분)
#   ... -Suite full          + 성능(DX11 대 OpenGL), 파티클 Soft · Lit
#   ... -Interactive         + 실제 키 입력 검사 (에디터를 앞으로 띄운다 — 그동안 키보드·마우스를 쓰지 말 것)
#   ... -Only cli,render     골라서 (cli, physics, render, gfx, perf, particles, keys)
#   ... -Project <폴더>      테스트 프로젝트 (기본 = 환경 변수 NOVA_TEST_PROJECT, 없으면 E:\NovaTest\ScriptTest)
#
# 결과: 표(PASS/FAIL) + <Out>\results.json, 캡처·차이 그림은 <Out>\ (기본 TestResults\<시각>). 실패가 있으면 종료 코드 1.
# 테스트 프로젝트에 필요한 것: Assets/Scenes 의 Materials, Particles, Forest, Trees, Shadows, Culling, SampleScene (+ 지형 이름 Terrain).
param(
    [ValidateSet('quick', 'full')][string]$Suite = 'quick',
    [string[]]$Only = @(),
    [switch]$Interactive,
    [string]$Project = '',
    [string]$Out = ''
)
. (Join-Path $PSScriptRoot 'common.ps1')
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })   # -File 로 부르면 a,b 가 글자 하나로 온다
if (-not $Project) { $Project = if ($env:NOVA_TEST_PROJECT) { $env:NOVA_TEST_PROJECT } else { 'E:\NovaTest\ScriptTest' } }
$script:Project = $Project
if (-not (Test-Path (Join-Path $Project 'Assets'))) { throw "test project not found: $Project (use -Project or NOVA_TEST_PROJECT)" }
if (-not (Test-Path $Nova)) { throw "nova.exe not found — build first (build.bat)" }
if (-not $Out) { $Out = Join-Path $Root ('TestResults\' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $Out | Out-Null

$suites = if ($Only.Count) { $Only } else { @('cli', 'physics', 'render', 'gfx') + $(if ($Suite -eq 'full') { @('perf', 'particles') } else { @() }) + $(if ($Interactive) { @('keys') } else { @() }) }
Write-Host "NOVA tests: $($suites -join ', ')  (project $Project, out $Out)"
Backup-Layout

function Info { Invoke-NovaJson 'info' }

# ------------------------------------------------------------------ CLI · 씬 · 메모리
function Suite-Cli
{
    Write-Host '[cli]'
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'scene open Assets/Scenes/Materials.scene --force' | Out-Null; Invoke-Nova 'wait 30' | Out-Null
        $i0 = Info
        Add-Result cli 'scene open → not dirty' (-not $i0.dirty) "dirty=$($i0.dirty)"
        $n0 = [int]$i0.objects; $l0 = [int]$i0.liveObjects

        Invoke-Nova 'create empty --name LeakP' | Out-Null
        Invoke-Nova 'create cube --name C1 --parent LeakP' | Out-Null
        Invoke-Nova 'create sphere --name C2 --parent LeakP' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $l1 = [int](Info).liveObjects
        Invoke-Nova 'delete LeakP' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $l2 = [int](Info).liveObjects
        Invoke-Nova 'undo' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $l3 = [int](Info).liveObjects
        Invoke-Nova 'redo' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $l4 = [int](Info).liveObjects
        Add-Result cli 'delete frees objects (incl. children)' ($l1 -eq $l0 + 3 -and $l2 -eq $l0 -and $l3 -eq $l0 + 3 -and $l4 -eq $l0) "live $l0 → +3 $l1 → delete $l2 → undo $l3 → redo $l4"

        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 60' | Out-Null; Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 20' | Out-Null
        $l5 = [int](Info).liveObjects
        Add-Result cli 'play/stop does not leak' ($l5 -eq $l4) "live $l4 → after play/stop $l5"

        Invoke-Nova 'scene open Assets/Scenes/Particles.scene --force' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
        $i1 = Info
        Add-Result cli 'scene switch clears undo' (-not $i1.canUndo -and -not $i1.dirty) "canUndo=$($i1.canUndo) dirty=$($i1.dirty)"

        Invoke-Nova 'scene new --force' | Out-Null
        $re = Invoke-NovaJson 'scene open Assets/Scenes/Materials.scene --force'
        Add-Result cli 'new scene → reopen old scene' ($re -and [int]$re.objects -eq $n0) "objects $($re.objects) (expect $n0)"

        $e1 = Invoke-NovaJson 'exec 1 + 2 * 3'
        Add-Result cli 'exec expression' ($e1 -and $e1.result -eq '7') "result=$($e1.result)"
        $e2 = Invoke-NovaJson 'exec "var l = new List<string>(); l.Add(\"a\"); l.Add(\"b\"); return l;"'
        Add-Result cli 'exec statements' ($e2 -and $e2.result -eq '[a, b]') "result=$($e2.result)"
        $e3 = Invoke-Nova 'exec "this is not C#"'
        Add-Result cli 'exec compile error reported' ($e3 -match 'error CS') ($e3 -split "`n" | Select-Object -First 1)

        $bf = Join-Path $Out 'batch.txt'
        "# batch`ninfo`nexec 2 + 2`nwait 5" | Set-Content -Encoding utf8 $bf
        $b = (& $Nova batch $bf --project $Project 2>&1 | Out-String)
        Add-Result cli 'batch runs every line' ($b -match '3 commands, 0 failed' -and $b -match '"result": "4"') (($b -split "`n" | Where-Object { $_ -match 'batch:' }) -join '')
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

# ------------------------------------------------------------------ 물리: 지형 + 나무 충돌, 레이캐스트
function Suite-Physics
{
    Write-Host '[physics]'
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'scene open Assets/Scenes/Forest.scene --force' | Out-Null; Invoke-Nova 'wait 60' | Out-Null
        Invoke-Nova 'terrain-trees Terrain --count 200 --clear' | Out-Null
        Invoke-Nova 'add-component Terrain TerrainCollider' | Out-Null
        $logStart = (Get-Content $EditorLog -ErrorAction SilentlyContinue).Count
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 30' | Out-Null
        $line = Get-Content $EditorLog | Select-Object -Skip $logStart | Where-Object { $_ -match 'tree colliders \(first at' } | Select-Object -Last 1
        if (-not ($line -match 'first at ([-\d.]+) ([-\d.]+) ([-\d.]+), radius ([\d.]+), height ([\d.]+)'))
        {
            Add-Result physics 'tree colliders built' $false 'no "[Physics] … tree colliders" line'
            return
        }
        $x = [double]$Matches[1]; $y = [double]$Matches[2]; $z = [double]$Matches[3]; $r = [double]$Matches[4]; $h = [double]$Matches[5]
        Add-Result physics 'tree colliders built' $true $line.Substring($line.IndexOf('[Physics]'))
        $side = '{0:F2},{1:F2},{2:F2}' -f ($x - 6), ($y + 1.5), $z
        $hit = Invoke-NovaJson "raycast $side 1,0,0 --max 6.5"
        $ok = $hit -and $hit.hit -and [math]::Abs([double]$hit.distance - (6 - $r)) -lt 0.05
        Add-Result physics 'ray hits trunk' $ok "distance $($hit.distance) (expect $([math]::Round(6 - $r, 3)))"
        $top = '{0:F2},{1:F2},{2:F2}' -f $x, ($y + $h + 3), $z
        $down = Invoke-NovaJson "raycast $top 0,-1,0 --max 40"
        $ok = $down -and $down.hit -and [math]::Abs([double]$down.distance - 3) -lt 0.05
        Add-Result physics 'ray from above stops at tree top' $ok "distance $($down.distance) (expect 3)"
        Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
        Invoke-Nova 'set Terrain TerrainCollider.enableTreeColliders=false' | Out-Null
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 30' | Out-Null
        $off = Invoke-NovaJson "raycast $side 1,0,0 --max 6.5"
        Add-Result physics 'tree colliders off → ray passes' ($off -and -not $off.hit) "hit=$($off.hit)"
        Invoke-Nova 'stop' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

# ------------------------------------------------------------------ 그리기: 7 개 씬 DX11 / OpenGL 같은 카메라로 비교
$script:Targets = [ordered]@{ 'Trees' = @('Oak', 30); 'Forest' = @('Terrain', 80); 'Materials' = @('Gold', 12); 'Particles' = @('Campfire', 12);
    'Shadows' = @('Near Box', 15); 'Culling' = @('Obj 0_4', 25); 'SampleScene' = @('Canvas', 30) }

function Capture-Scenes([switch]$OpenGL, [string]$dir)
{
    New-Item -ItemType Directory -Force $dir | Out-Null
    $ed = Start-TestEditor -OpenGL:$OpenGL
    try
    {
        Invoke-Nova 'window scene' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
        foreach ($n in $Targets.Keys)
        {
            $t = $Targets[$n]
            Invoke-Nova "scene open Assets/Scenes/$n.scene --force" | Out-Null; Invoke-Nova 'wait 120' | Out-Null
            Invoke-Nova "camera --frame `"$($t[0])`" --distance $($t[1])" | Out-Null; Invoke-Nova 'wait 120' | Out-Null
            Invoke-Nova "screenshot $dir\$n.png --view scene" | Out-Null
        }
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

function Suite-Render
{
    Write-Host '[render] DirectX 11'
    Capture-Scenes -dir (Join-Path $Out 'render\dx')
    Write-Host '[render] OpenGL'
    $logStart = 0
    Capture-Scenes -OpenGL -dir (Join-Path $Out 'render\gl')
    $glWarnings = Get-Content $EditorLog | Where-Object { $_ -match '\[OpenGL\]' -and $_ -notmatch 'VecAdd' }
    Add-Result render 'OpenGL log clean' ($glWarnings.Count -eq 0) $(if ($glWarnings) { $glWarnings[0] } else { 'no [OpenGL] warnings (except old VecAdd sample)' })
    foreach ($n in $Targets.Keys)
    {
        $a = Join-Path $Out "render\dx\$n.png"; $b = Join-Path $Out "render\gl\$n.png"
        if (-not (Test-Path $a) -or -not (Test-Path $b)) { Add-Result render "$n DX = GL" $false 'capture missing'; continue }
        $c = [NovaImageCompare]::Compare($a, $b, (Join-Path $Out "render\${n}_diff.png"))
        if (-not $c) { Add-Result render "$n DX = GL" $false 'size differs'; continue }
        # 나무는 바람으로 잎이 움직인다 → 다른 픽셀 비율만, 나머지는 최대 차이
        $ok = if ($n -eq 'Trees') { $c[2] -le 6.0 } else { $c[0] -le 20 }
        Add-Result render "$n DX = GL" $ok ('max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2])
    }
}

# ------------------------------------------------------------------ gfx-test / rhi-test (DX11 장치 대 OpenGL 시험 장치)
function Suite-Gfx
{
    Write-Host '[gfx]'
    $ed = Start-TestEditor
    try
    {
        foreach ($t in 'gfx-test', 'rhi-test')
        {
            $j = Invoke-NovaJson $t
            $ok = $j -and $j.diff -and [double]$j.diff.max -le 2
            Add-Result gfx $t $ok $(if ($j -and $j.diff) { "diff max $($j.diff.max), mean $($j.diff.mean)" } else { 'no diff in result' })
        }
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

# ------------------------------------------------------------------ 성능 (참고용 — Release 빌드에서 의미가 있다)
function Suite-Perf
{
    Write-Host '[perf] (meaningful on a Release build)'
    $rows = @{}
    foreach ($api in 'dx', 'gl')
    {
        $ed = Start-TestEditor -OpenGL:($api -eq 'gl')
        try
        {
            Invoke-Nova 'window scene' | Out-Null
            foreach ($n in 'Materials', 'Trees')
            {
                $t = $Targets[$n]
                Invoke-Nova "scene open Assets/Scenes/$n.scene --force" | Out-Null; Invoke-Nova 'wait 200' | Out-Null
                Invoke-Nova "camera --frame `"$($t[0])`" --distance $($t[1])" | Out-Null; Invoke-Nova 'wait 60' | Out-Null
                $p = Invoke-NovaJson 'perf --frames 240'
                $rows["$api/$n"] = $p
            }
        }
        finally { Write-Host "  $(Stop-TestEditor $ed)" }
    }
    foreach ($n in 'Materials', 'Trees')
    {
        $d = $rows["dx/$n"]; $g = $rows["gl/$n"]
        if (-not $d -or -not $g) { Add-Result perf $n $false 'no perf result'; continue }
        $ratio = [double]$g.frameMs / [math]::Max(0.001, [double]$d.frameMs)
        Add-Result perf "$n OpenGL vs DX11" ($ratio -le 2.0) ('frame ms DX {0} / GL {1} (×{2:N2}), GPU ms DX {3} / GL {4}' -f $d.frameMs, $g.frameMs, $ratio, $d.gpuMs, $g.gpuMs)
    }
}

# ------------------------------------------------------------------ 파티클 Soft · Lit (모닥불 아래 바닥, 끔/켬)
function Suite-Particles
{
    Write-Host '[particles]'
    foreach ($api in 'dx', 'gl')
    {
        $ed = Start-TestEditor -OpenGL:($api -eq 'gl')
        try
        {
            Invoke-Nova 'window scene' | Out-Null
            Invoke-Nova 'scene open Assets/Scenes/Particles.scene --force' | Out-Null; Invoke-Nova 'wait 60' | Out-Null
            $cf = Invoke-NovaJson 'get Campfire'
            $p = $cf.position
            Invoke-Nova ('create plane --name Floor --position {0},{1},{2} --scale 2,1,2' -f $p[0], $p[1], $p[2]) | Out-Null
            Invoke-Nova ('camera --position {0},{1},{2} --target {3},{4},{5}' -f ($p[0] + 1.2), ($p[1] + 1.0), ($p[2] - 3.2), $p[0], ($p[1] + 0.7), $p[2]) | Out-Null
            $offPng = Join-Path $Out "particles_${api}_off.png"; $onPng = Join-Path $Out "particles_${api}_on.png"
            Invoke-Nova 'play' | Out-Null; Invoke-Nova 'window scene' | Out-Null; Invoke-Nova 'wait 240' | Out-Null
            Invoke-Nova "screenshot $offPng --view scene" | Out-Null; Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 20' | Out-Null
            foreach ($t in 'Campfire', 'Campfire/Smoke', 'Campfire/Embers')
            {
                Invoke-Nova ('set "{0}" ParticleSystem.renderer={{\"softParticles\":true,\"softDistance\":0.6,\"lit\":true}}' -f $t) | Out-Null
            }
            Invoke-Nova 'play' | Out-Null; Invoke-Nova 'window scene' | Out-Null; Invoke-Nova 'wait 240' | Out-Null
            Invoke-Nova "screenshot $onPng --view scene" | Out-Null; Invoke-Nova 'stop' | Out-Null
            $c = [NovaImageCompare]::Compare($offPng, $onPng, $null)
            # 켜면 달라져야 하고(> 1%), 불이 사라지면 안 된다(전체가 거의 다 달라지면 = 불·연기가 통째로 사라짐을 의심, 30% 미만)
            $ok = $c -and $c[2] -gt 1.0 -and $c[2] -lt 30.0
            Add-Result particles "Soft · Lit change the image ($api)" $ok $(if ($c) { 'pixels >8 different: {0:N2}%' -f $c[2] } else { 'capture missing' })
        }
        finally { Write-Host "  $(Stop-TestEditor $ed)" }
    }
}

# ------------------------------------------------------------------ 실제 키 입력 (에디터를 앞으로 — -Interactive)
function Suite-Keys
{
    Write-Host '[keys] (the editor comes to the front — do not use the keyboard/mouse)'
    if (-not ('NovaKeys' -as [type]))
    {
        Add-Type @"
using System; using System.Runtime.InteropServices;
public static class NovaKeys {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  public static void Press(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(40); keybd_event(vk, 0, 2, UIntPtr.Zero); }
  public static void Chord(byte mod, byte vk) { keybd_event(mod, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(30); Press(vk); System.Threading.Thread.Sleep(30); keybd_event(mod, 0, 2, UIntPtr.Zero); }
}
"@
    }
    function Open-Front
    {
        $o = & $Nova open $Project --timeout 300 2>&1 | Out-String
        if ($o -notmatch 'pid (\d+)') { throw 'open failed' }
        $id = [int]$Matches[1]
        for ($i = 0; $i -lt 150; $i++) { $pr = Get-Process -Id $id -ErrorAction SilentlyContinue; if ($pr -and $pr.MainWindowTitle -match 'NOVA Game Engine Editor') { break }; Start-Sleep -Milliseconds 200 }
        Start-Sleep -Seconds 1
        $script:KeyPid = $id
        $script:KeyHwnd = (Get-Process -Id $id).MainWindowHandle
        Invoke-Nova 'scene open Assets/Scenes/Materials.scene --force' | Out-Null; Invoke-Nova 'wait 20' | Out-Null
    }
    function Front { [void][NovaKeys]::SetForegroundWindow($script:KeyHwnd); Start-Sleep -Milliseconds 300 }
    function Alive { [bool](Get-Process -Id $script:KeyPid -ErrorAction SilentlyContinue) }
    function Close-Front { if (Alive) { Invoke-Nova 'window scene' | Out-Null; & $Nova quit --force --project $Project | Out-Null; Start-Sleep -Seconds 1 } }

    # Ctrl+N: 바뀐 것이 있으면 확인 창 → Esc 그대로 / D 새 씬
    Open-Front
    Invoke-Nova 'create cube --name Dirty' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
    Front; [NovaKeys]::Chord(0x11, 0x4E); Invoke-Nova 'wait 10' | Out-Null; Front; [NovaKeys]::Press(0x1B); Invoke-Nova 'wait 10' | Out-Null
    $a = Info
    Add-Result keys 'Ctrl+N → Esc keeps the scene' ($a.dirty -and $a.scene -match 'Materials') "scene=$($a.scene) dirty=$($a.dirty)"
    Front; [NovaKeys]::Chord(0x11, 0x4E); Invoke-Nova 'wait 10' | Out-Null; Front; [NovaKeys]::Press(0x44); Invoke-Nova 'wait 10' | Out-Null
    $b = Info
    Add-Result keys "Ctrl+N → D (Don't Save) new scene" (-not $b.scene -and -not $b.dirty) "scene='$($b.scene)' dirty=$($b.dirty)"
    # Ctrl+P: Play / Stop
    Front; [NovaKeys]::Chord(0x11, 0x50); Invoke-Nova 'wait 20' | Out-Null; $p1 = (Info).playing
    Front; [NovaKeys]::Chord(0x11, 0x50); Invoke-Nova 'wait 20' | Out-Null; $p2 = (Info).playing
    Add-Result keys 'Ctrl+P toggles Play' ($p1 -and -not $p2) "after 1st $p1, after 2nd $p2"
    # Ctrl+O: 파일 대화상자 "Open Scene" → 취소
    Front; [NovaKeys]::Chord(0x11, 0x4F)
    $dlg = [IntPtr]::Zero
    for ($i = 0; $i -lt 25 -and $dlg -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 200; $dlg = [NovaKeys]::FindWindow('#32770', 'Open Scene') }
    Add-Result keys 'Ctrl+O opens "Open Scene"' ($dlg -ne [IntPtr]::Zero) "dialog found: $($dlg -ne [IntPtr]::Zero)"
    if ($dlg -ne [IntPtr]::Zero) { [void][NovaKeys]::PostMessage($dlg, 0x0111, [IntPtr]2, [IntPtr]::Zero); Start-Sleep -Milliseconds 500 }
    # 창 닫기: 바뀐 것이 있으면 확인 창 → Esc 그대로, Alt+F4 → D 닫힘
    Invoke-Nova 'scene open Assets/Scenes/Materials.scene --force' | Out-Null; Invoke-Nova 'create cube --name Unsaved' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
    [void][NovaKeys]::PostMessage($script:KeyHwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero); Invoke-Nova 'wait 10' | Out-Null
    $stay = Alive
    Front; [NovaKeys]::Press(0x1B); Invoke-Nova 'wait 10' | Out-Null
    Add-Result keys 'close with changes asks first (Esc = stay)' ($stay -and (Alive)) "alive after close: $stay"
    Front; [NovaKeys]::Chord(0x12, 0x73); Invoke-Nova 'wait 10' | Out-Null; Front; [NovaKeys]::Press(0x44); Start-Sleep -Seconds 2
    Add-Result keys "Alt+F4 → D closes" (-not (Alive)) "alive: $(Alive)"
    Close-Front
}

# ------------------------------------------------------------------ 실행
$sw = [Diagnostics.Stopwatch]::StartNew()
try
{
    foreach ($s in $suites)
    {
        try
        {
            switch ($s)
            {
                'cli' { Suite-Cli }
                'physics' { Suite-Physics }
                'render' { Suite-Render }
                'gfx' { Suite-Gfx }
                'perf' { Suite-Perf }
                'particles' { Suite-Particles }
                'keys' { Suite-Keys }
                default { Write-Host "unknown suite $s" }
            }
        }
        catch { Add-Result $s 'suite ran' $false $_.Exception.Message }
    }
}
finally { Restore-Layout }

$failed = @($Results | Where-Object { $_.Result -eq 'FAIL' })
$Results | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $Out 'results.json')
Write-Host ''
$Results | Format-Table Suite, Test, Result, Detail -AutoSize -Wrap | Out-String -Width 220 | Write-Host
Write-Host ('{0} passed, {1} failed  ({2:N0} s)  → {3}' -f ($Results.Count - $failed.Count), $failed.Count, $sw.Elapsed.TotalSeconds, $Out)
exit $(if ($failed.Count) { 1 } else { 0 })
