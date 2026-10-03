# NOVA 자동 회귀 검사 — 실행 중인 에디터를 NOVA CLI 로 다뤄 확인한다 (테스트 프로젝트에서만, 저장하지 않음).
#
#   powershell -ExecutionPolicy Bypass -File Tools\tests\run_tests.ps1                 # quick (약 4~6 분)
#   ... -Suite full          + 성능(DX11 대 OpenGL), 파티클 Soft · Lit
#   ... -Interactive         + 실제 키 입력 검사 (에디터를 앞으로 띄운다 — 그동안 키보드·마우스를 쓰지 말 것)
#   ... -Only cli,render     골라서 (cli, physics, animation, import, ui, packages, model, anim2d, layers, sprites, physics2d, shadergraph, decal, reflectionprobe, recovery, render, gfx, perf, particles, keys)
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

$suites = if ($Only.Count) { $Only } else { @('cli', 'physics', 'animation', 'import', 'ui', 'packages', 'model', 'anim2d', 'layers', 'sprites', 'physics2d', 'shadergraph', 'decal', 'reflectionprobe', 'audio', 'recovery', 'render', 'gfx') + $(if ($Suite -eq 'full') { @('perf', 'particles') } else { @() }) + $(if ($Interactive) { @('keys') } else { @() }) }
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
        Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 10' | Out-Null

        # Character Controller: 땅에 서기 → 0.25 m 턱 오르기 → 벽에서 멈추기 (Play 중 exec 로 Move)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name CCGround --position 0,-0.5,0 --scale 20,1,6' | Out-Null
        Invoke-Nova 'create cube --name CCStep --position 2.5,0.125,0 --scale 1,0.25,4' | Out-Null
        Invoke-Nova 'create cube --name CCWall --position 6,1.5,0 --scale 0.5,3,6' | Out-Null
        Invoke-Nova 'create empty --name CCPlayer --position -3,1.3,0' | Out-Null
        Invoke-Nova 'add-component CCPlayer CharacterController' | Out-Null
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
        $cf = Join-Path $Out 'cc_move.cs'
        @'
var cc = GameObject.Find("CCPlayer").GetComponent<CharacterController>();
var f = CollisionFlags.None;
for (int i = 0; i < 30; i++) f = cc.Move(new Vector3(0, -0.05f, 0));
float groundY = cc.transform.position.y; bool grounded = cc.isGrounded;
float maxY = 0;
for (int i = 0; i < 60; i++) { f = cc.Move(new Vector3(0.2f, -0.05f, 0)); maxY = Mathf.Max(maxY, cc.transform.position.y); }
var p = cc.transform.position;
return $"{groundY:F3} {grounded} {maxY:F3} {p.x:F3} {f}";
'@ | Set-Content -Encoding utf8 $cf
        $m = Invoke-NovaJson "exec --file $cf"
        $v = if ($m) { "$($m.result)" -split ' ', 5 } else { @() }
        if ($v.Count -lt 5) { Add-Result physics 'character controller' $false "exec failed: $m" }
        else
        {
            Add-Result physics 'character controller stands on ground' ([math]::Abs([double]$v[0] - 1.0) -lt 0.03 -and $v[1] -eq 'True') "y $($v[0]) grounded $($v[1]) (expect 1.00 True)"
            Add-Result physics 'character controller climbs 0.25 m step' ([double]$v[2] -gt 1.2) "max y $($v[2]) (expect ≥ 1.2)"
            Add-Result physics 'character controller stops at wall' ([math]::Abs([double]$v[3] - 5.25) -lt 0.05 -and $v[4] -match 'Sides') "x $($v[3]) flags $($v[4]) (expect 5.25, Sides)"
        }
        Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 10' | Out-Null

        # Joint: 진자(Hinge, 길이 2 m 유지), Fixed(월드에 고정), Break Force(10 kg × g = 98 N > 50 → 끊어져 떨어짐), 모터 속도
        $jl = @(
            'scene new --force',
            'create cube --name JGround --position 6,-0.5,0 --scale 30,1,10',
            'create sphere --name JBob --position 2,3,0 --scale 0.5,0.5,0.5', 'add-component JBob RigidBody',
            'add-component JBob HingeJoint --values "{\"anchor\":[-4,0,0],\"axis\":[0,0,1]}"',
            'create cylinder --name JWheel --position 5,1.5,0', 'add-component JWheel RigidBody --values "{\"useGravity\":false}"',
            'add-component JWheel HingeJoint --values "{\"axis\":[0,1,0],\"useMotor\":true,\"motor\":{\"targetVelocity\":180,\"force\":100,\"freeSpin\":false}}"',
            'create cube --name JFixed --position 8,3,0', 'add-component JFixed RigidBody', 'add-component JFixed FixedJoint',
            'create cube --name JBreak --position 14,3,0', 'add-component JBreak RigidBody --values "{\"mass\":10}"',
            'add-component JBreak FixedJoint --values "{\"breakForce\":50}"')
        foreach ($l in $jl) { Invoke-Nova $l | Out-Null }
        Invoke-Nova 'play' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 1.5) { Invoke-Nova 'wait 20' | Out-Null }
        $bob = (Invoke-NovaJson 'get JBob').position; $fix = (Invoke-NovaJson 'get JFixed').position; $brk = (Invoke-NovaJson 'get JBreak').position
        $len = [math]::Sqrt([math]::Pow([double]$bob[0], 2) + [math]::Pow([double]$bob[1] - 3, 2))
        Add-Result physics 'hinge joint keeps the pendulum length' ([math]::Abs($len - 2) -lt 0.03 -and [double]$bob[1] -lt 2.9) ("length {0:F3} (expect 2), y {1:F2}" -f $len, [double]$bob[1])
        Add-Result physics 'fixed joint holds against gravity' ([math]::Abs([double]$fix[1] - 3) -lt 0.01) ("y {0:F3} (expect 3)" -f [double]$fix[1])
        Add-Result physics 'break force breaks the joint' ([double]$brk[1] -lt 1.0) ("y {0:F2} (fell to the ground)" -f [double]$brk[1])
        $jf = Join-Path $Out 'joint_motor.cs'
        'return GameObject.Find("JWheel").GetComponent<HingeJoint>().velocity;' | Set-Content -Encoding utf8 $jf
        $mv = Invoke-NovaJson "exec --file $jf"
        Add-Result physics 'hinge motor reaches target velocity' ($mv -and [math]::Abs([double]$mv.result - 180) -lt 5) "velocity $($mv.result) deg/s (expect 180)"
        Invoke-Nova 'stop' | Out-Null

        # Animator Blend Tree: 엔진 클립 두 개(1D 문턱값 0 · 1) → Blend 0.5 의 한 바퀴 길이 = 두 길이의 평균
        $bc = Join-Path $Project 'Assets\NovaTestBlend.controller'
        '{ "parameters": [ { "name": "Blend", "type": "Float" } ], "layers": [ { "name": "Base Layer", "defaultState": "Move", "states": [ { "name": "Move", "blendTree": { "type": 0, "parameter": "Blend", "children": [ { "clipPath": "Resources\\Packages\\Character\\Animations\\Rapier_Idle.fbx", "threshold": 0 }, { "clipPath": "Resources\\Packages\\Character\\Animations\\GreatSword_Idle_Pose.FBX", "threshold": 1 } ] } } ] } ] }' | Set-Content -Encoding utf8 $bc
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create character --name ACh --controller Assets\NovaTestBlend.controller' | Out-Null
        Wait-Compile   # Animator 는 Animation 패키지 — 처음 넣었으면 C# 이 다시 컴파일된다
        $af = Join-Path $Out 'anim_blend.cs'
        'var a = GameObject.Find("ACh").GetComponent<Animator>(); a.SetFloat("Blend", 0); float l0 = a.GetCurrentAnimatorStateInfo(0).length; a.SetFloat("Blend", 1); float l1 = a.GetCurrentAnimatorStateInfo(0).length; a.SetFloat("Blend", 0.5f); var i = a.GetCurrentAnimatorStateInfo(0); return l0.ToString("F3") + " " + l1.ToString("F3") + " " + i.length.ToString("F3") + " " + i.IsName("Move");' | Set-Content -Encoding utf8 $af
        $ab = Invoke-NovaJson "exec --file $af"
        $av = if ($ab) { "$($ab.result)" -split ' ' } else { @() }
        $ok = $av.Count -eq 4 -and $av[3] -eq 'True' -and [double]$av[0] -ne [double]$av[1] -and [math]::Abs([double]$av[2] - ([double]$av[0] + [double]$av[1]) / 2) -lt 0.01
        Add-Result physics 'animator 1D blend tree mixes two clips' $ok "lengths $($av[0]) / $($av[1]), at 0.5 → $($av[2]) (expect the mean), state Move $($av[3])"
        Remove-Item -LiteralPath $bc -ErrorAction SilentlyContinue

        # Humanoid 리타게팅: 3ds Max Biped 클립(Rapier_Idle) → UE 마네킹 기본 캐릭터 (본 이름이 전혀 다르다)
        $rc = Join-Path $Project 'Assets\NovaTestRetarget.controller'
        '{ "parameters": [], "layers": [ { "name": "Base Layer", "defaultState": "Idle", "states": [ { "name": "Idle", "clipPath": "Resources\\Packages\\Character\\Animations\\Rapier_Idle.fbx", "clipIndex": 0 } ] } ] }' | Set-Content -Encoding utf8 $rc
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create character --name RCh --controller Assets\NovaTestRetarget.controller' | Out-Null
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
        Invoke-Nova 'stop' | Out-Null
        $log = ((Invoke-Nova 'log -n 400 --grep "humanoid avatar"') + (Invoke-Nova 'log -n 400 --grep "retarget "')) -join "`n"   # --grep 은 정규식이 아니다
        $ok = $log -match 'humanoid avatar Rapier_Idle\.fbx: valid' -and $log -match 'retarget .*Rapier_Idle\.fbx -> Model_Unity_Ver1\.FBX'
        $line = ($log -split "`n" | Where-Object { $_ -match 'retarget ' } | Select-Object -Last 1)
        Add-Result physics 'humanoid retarget across skeletons (Biped → UE)' $ok $(if ($line) { $line.Substring($line.IndexOf('retarget')) } else { 'no retarget line' })
        Remove-Item -LiteralPath $rc -ErrorAction SilentlyContinue
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

# ------------------------------------------------------------------ 패키지: 레지스트리 → 프로젝트에 넣기 → DLL·C# → 빼기 (쓰지 않으면 바로 내림)
# ------------------------------------------------------------------ 절차적 애니메이션: Legs Animator (턱 위 발) · Look Animator (머리 돌리기)
function Suite-Animation
{
    Write-Host '[animation]'
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name IKGround --position 0,-0.5,0 --scale 10,1,10' | Out-Null
        Invoke-Nova 'create character --name LCh' | Out-Null
        Wait-Compile
        Invoke-Nova 'add-component LCh LegsAnimator' | Out-Null
        Invoke-Nova 'add-component LCh LookAnimator' | Out-Null
        $rf = Join-Path $Out 'ik_read.cs'
        'var a = GameObject.Find("LCh").GetComponent<Animator>(); var l = a.GetBonePosition(HumanBodyBones.LeftFoot); var r = a.GetBonePosition(HumanBodyBones.RightFoot); var h = a.GetBonePosition(HumanBodyBones.Head); var q = a.GetBoneRotation(HumanBodyBones.Head); var f = new float[] { l.x, l.y, l.z, r.x, r.y, r.z, h.x, h.y, h.z, q.x, q.y, q.z, q.w }; var s = new string[f.Length]; for (int i = 0; i < f.Length; i++) s[i] = f[i].ToString("F4", System.Globalization.CultureInfo.InvariantCulture); return string.Join(" ", s);' | Set-Content -Encoding utf8 $rf
        function Read-Bones { $m = Invoke-NovaJson "exec --file $rf"; if ($m) { @("$($m.result)" -split ' ' | ForEach-Object { [double]$_ }) } else { @() } }
        function Wait-Sec([double]$s) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Invoke-Nova 'wait 10' | Out-Null } }

        # 1) 평지: 발 위치 · 머리 위치를 읽는다
        Invoke-Nova 'play' | Out-Null; Wait-Sec 0.5
        $flat = Read-Bones
        Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        if ($flat.Count -ne 13 -or [math]::Abs($flat[0] - $flat[3]) -lt 0.08)
        {
            Add-Result animation 'humanoid bone positions (GetBonePosition)' $false "feet $($flat[0..5] -join ' ')"
            return
        }
        Add-Result animation 'humanoid bone positions (GetBonePosition)' $true ("left foot x {0:F2}, right foot x {1:F2}, head y {2:F2}" -f $flat[0], $flat[3], $flat[7])

        # 2) 왼발 밑에만 0.2 m 턱 (오른발 쪽으로 넘어가지 않게 바깥으로 치우쳐) + 오른쪽 45° 에 볼 대상
        $side = [math]::Sign($flat[0] - $flat[3])
        $stepX = $flat[0] + $side * 0.12
        Invoke-Nova ('create cube --name IKStep --position {0:F3},0.1,{1:F3} --scale 0.3,0.2,0.5' -f $stepX, $flat[2]) | Out-Null
        Invoke-Nova ('create sphere --name LookT --position {0:F3},{1:F3},{2:F3} --scale 0.2,0.2,0.2' -f ($flat[6] + 2 * 0.7071), $flat[7], ($flat[8] + 2 * 0.7071)) | Out-Null
        Invoke-Nova 'play' | Out-Null
        $tf = Join-Path $Out 'ik_target.cs'
        'GameObject.Find("LCh").GetComponent<LookAnimator>().target = GameObject.Find("LookT"); return 1;' | Set-Content -Encoding utf8 $tf
        Invoke-Nova "exec --file $tf" | Out-Null
        Wait-Sec 1.5
        $on = Read-Bones
        # 끄기 (Play 중에는 nova set 이 막혀 있다 → C#): Legs 는 바로, Look 은 Speed 로 부드럽게 돌아온다
        $xf = Join-Path $Out 'ik_off.cs'
        'var g = GameObject.Find("LCh"); g.GetComponent<LegsAnimator>().weight = 0; g.GetComponent<LookAnimator>().weight = 0; return 1;' | Set-Content -Encoding utf8 $xf
        Invoke-Nova "exec --file $xf" | Out-Null
        Wait-Sec 1.5
        $off = Read-Bones
        Invoke-Nova 'stop' | Out-Null
        if ($on.Count -ne 13 -or $off.Count -ne 13) { Add-Result animation 'legs animator' $false "read failed: on $($on.Count) off $($off.Count)"; return }
        $dl = $on[1] - $off[1]; $dr = $on[4] - $off[4]
        Add-Result animation 'legs animator lifts the foot onto a 0.2 m step' ([math]::Abs($dl - 0.2) -lt 0.04 -and [math]::Abs($dr) -lt 0.03) ("left +{0:F3} m (expect 0.2), right {1:F3} m (expect 0)" -f $dl, $dr)
        $dot = [math]::Abs($on[9] * $off[9] + $on[10] * $off[10] + $on[11] * $off[11] + $on[12] * $off[12])
        $ang = 2 * [math]::Acos([math]::Min(1.0, $dot)) * 180 / [math]::PI
        Add-Result animation 'look animator turns the head toward a target 45° aside' ($ang -gt 35 -and $ang -lt 55) ("head turned {0:F1}° (expect ~45)" -f $ang)

        # 3) Foot Locking: 서 있으면 두 발 고정 → 몸을 0.1 m 옮겨도 발은 제자리, 0.5 m 면 놓는다
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name IKGround --position 0,-0.5,0 --scale 10,1,10' | Out-Null
        Invoke-Nova 'create character --name LCh' | Out-Null
        Invoke-Nova 'add-component LCh LegsAnimator' | Out-Null
        Invoke-Nova 'add-component LCh HandsAnimator' | Out-Null
        Invoke-Nova 'play' | Out-Null; Wait-Sec 1.0
        $lf = Join-Path $Out 'ik_lock.cs'
        'var g = GameObject.Find("LCh"); var l = g.GetComponent<LegsAnimator>(); var a = g.GetComponent<Animator>(); var p = a.GetBonePosition(HumanBodyBones.LeftFoot); return l.IsFootLocked(AvatarIKGoal.LeftFoot) + " " + l.IsFootLocked(AvatarIKGoal.RightFoot) + " " + p.x.ToString("F4", System.Globalization.CultureInfo.InvariantCulture) + " " + p.z.ToString("F4", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $lf
        $mf = Join-Path $Out 'ik_move.cs'
        function Lock { $m = Invoke-NovaJson "exec --file $lf"; if ($m) { "$($m.result)" -split ' ' } else { @() } }
        function MoveX([double]$x) { "GameObject.Find(`"LCh`").transform.position = new Vector3($($x.ToString([Globalization.CultureInfo]::InvariantCulture))f, 0, 0); return 1;" | Set-Content -Encoding utf8 $mf; Invoke-Nova "exec --file $mf" | Out-Null; Wait-Sec 0.3 }
        $k0 = Lock
        MoveX 0.1
        $k1 = Lock
        MoveX 0.6
        Wait-Sec 0.5
        $k2 = Lock
        $ok = $k0.Count -eq 4 -and $k0[0] -eq 'True' -and $k0[1] -eq 'True' -and [math]::Abs([double]$k1[2] - [double]$k0[2]) -lt 0.02 -and [math]::Abs([double]$k2[2] - [double]$k0[2] - 0.6) -lt 0.05
        Add-Result animation 'legs animator foot locking' $ok ("locked {0}/{1}, left foot x {2} → body +0.1: {3} (stays) → body +0.6: {4} (released, follows)" -f $k0[0], $k0[1], $k0[2], $k1[2], $k2[2])

        # 4) Hands Animator: 오른손을 앞 · 옆의 목표에
        MoveX 0
        $hf = Join-Path $Out 'ik_hand.cs'
        @'
var g = GameObject.Find("LCh"); var a = g.GetComponent<Animator>(); var h = g.GetComponent<HandsAnimator>();
var s = a.GetBonePosition(HumanBodyBones.RightUpperArm);
var t = s + new Vector3(0.15f, -0.15f, 0.35f);
h.SetIKPosition(AvatarIKGoal.RightHand, t);
return t.x.ToString("F4", System.Globalization.CultureInfo.InvariantCulture) + " " + t.y.ToString("F4", System.Globalization.CultureInfo.InvariantCulture) + " " + t.z.ToString("F4", System.Globalization.CultureInfo.InvariantCulture);
'@ | Set-Content -Encoding utf8 $hf
        $ht = Invoke-NovaJson "exec --file $hf"
        Wait-Sec 1.0
        $rf2 = Join-Path $Out 'ik_hand2.cs'
        'var p = GameObject.Find("LCh").GetComponent<Animator>().GetBonePosition(HumanBodyBones.RightHand); return p.x.ToString("F4", System.Globalization.CultureInfo.InvariantCulture) + " " + p.y.ToString("F4", System.Globalization.CultureInfo.InvariantCulture) + " " + p.z.ToString("F4", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $rf2
        $hp = Invoke-NovaJson "exec --file $rf2"
        $tv = if ($ht) { @("$($ht.result)" -split ' ' | ForEach-Object { [double]$_ }) } else { @() }
        $pv = if ($hp) { @("$($hp.result)" -split ' ' | ForEach-Object { [double]$_ }) } else { @() }
        $dist = if ($tv.Count -eq 3 -and $pv.Count -eq 3) { [math]::Sqrt([math]::Pow($tv[0] - $pv[0], 2) + [math]::Pow($tv[1] - $pv[1], 2) + [math]::Pow($tv[2] - $pv[2], 2)) } else { 99 }
        Add-Result animation 'hands animator reaches the target' ($dist -lt 0.03) ("hand {0:F3} m from the target" -f $dist)
        Invoke-Nova 'stop' | Out-Null

        # 5) Body Lean: 15° 오르막 (앞 = +Z 가 올라감) → 앞으로 기울인다
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name IKRamp --position 0,-0.1,0 --rotation -15,0,0 --scale 6,0.2,8' | Out-Null
        Invoke-Nova 'create character --name LCh' | Out-Null
        Invoke-Nova 'add-component LCh LegsAnimator' | Out-Null
        Invoke-Nova 'play' | Out-Null; Wait-Sec 1.5
        $bf = Join-Path $Out 'ik_lean.cs'
        'return GameObject.Find("LCh").GetComponent<LegsAnimator>().lean.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $bf
        $lean = Invoke-NovaJson "exec --file $bf"
        Invoke-Nova 'stop' | Out-Null
        $lv = if ($lean) { [double]$lean.result } else { 0 }
        Add-Result animation 'legs animator leans forward uphill' ($lv -gt 4 -and $lv -lt 10) ("lean {0:F2}° on a 15° ramp (body lean 0.5 → ~7.5)" -f $lv)

        # 6) VRM 캐릭터 (Seed-san, VRM 컨소시엄 샘플 — 테스트 프로젝트에 있을 때만): 스킨 메시 · 재질 꺼내기 · Humanoid · Dynamic Bone
        $vrm = 'Assets\TestAssets\SeedSan\Seed-san.vrm'
        if (Test-Path (Join-Path $Project $vrm))
        {
            Invoke-Nova 'scene new --force' | Out-Null
            Invoke-Nova "create character --name VCh --model $vrm" | Out-Null
            Invoke-Nova 'wait 10' | Out-Null
            $found = Invoke-NovaJson 'find --component SkinnedMeshRenderer'   # (PS 5.1: 배열이 한 덩어리로 온다 — @() 로 감싸지 않음)
            $smr = if ($found) { $found.Count } else { 0 }
            $mats = @(Get-ChildItem (Join-Path $Project 'Assets\TestAssets\SeedSan\Seed-san.Materials') -Filter *.mat -ErrorAction SilentlyContinue).Count
            $vi = Invoke-NovaJson "import-settings $vrm"
            Add-Result animation 'VRM: 5 skinned meshes, 17 materials extracted, Humanoid from the file, meters' ($smr -ge 5 -and $mats -eq 17 -and $vi.imported.humanoid -and [math]::Abs($vi.imported.unitScale - 1) -lt 0.001) "skinned=$smr mats=$mats humanoid=$($vi.imported.humanoid) unitScale=$($vi.imported.unitScale)"
            Invoke-Nova 'play' | Out-Null; Wait-Sec 1.5
            $df = Join-Path $Out 'dyn_read.cs'
            'var d = GameObject.Find("VCh").GetComponent<DynamicBone>(); Vector3 a; d.TryGetTailPosition(0, 5, out a); return d.chainCount + " " + d.colliderCount + " " + a.x.ToString("F3", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $df
            $d0 = Invoke-NovaJson "exec --file $df"
            $wf = Join-Path $Out 'dyn_wind.cs'
            'var d = GameObject.Find("VCh").GetComponent<DynamicBone>(); d.wind = new Vector3(6, 0, 0); d.windTurbulence = 0; return "ok";' | Set-Content -Encoding utf8 $wf
            Invoke-Nova "exec --file $wf" | Out-Null
            Wait-Sec 1.5
            $d1 = Invoke-NovaJson "exec --file $df"
            Invoke-Nova 'stop' | Out-Null
            $a0 = if ($d0) { "$($d0.result)" -split ' ' } else { @() }
            $a1 = if ($d1) { "$($d1.result)" -split ' ' } else { @() }
            $shift = if ($a0.Count -eq 3 -and $a1.Count -eq 3) { [double]$a1[2] - [double]$a0[2] } else { 0 }
            Add-Result animation 'VRM spring bones → Dynamic Bone (9 chains, 8 colliders), wind swings the ponytail' ($a0.Count -eq 3 -and $a0[0] -eq '9' -and $a0[1] -eq '8' -and $shift -gt 0.1) ("chains {0} colliders {1}, tail x shift with wind {2:F3} m" -f $a0[0], $a0[1], $shift)
        }

        # 7) VRM 0.x (VRoid 공식 샘플 AvatarSample_B — 테스트 프로젝트에 있을 때만): +Z 를 본다, MToon → lilToon 재질, 옷 껍질의 잘라내기 구멍 없음
        $vrm0 = 'Assets\TestAssets\VRoid\AvatarSample_B.vrm'
        if (Test-Path (Join-Path $Project $vrm0))
        {
            $matDir = Join-Path $Project 'Assets\TestAssets\VRoid\AvatarSample_B.Materials'
            Remove-Item $matDir -Recurse -Force -ErrorAction SilentlyContinue   # 다시 꺼낸다
            Invoke-Nova 'scene new --force' | Out-Null
            Invoke-Nova "create character --name V0 --model $vrm0" | Out-Null
            Invoke-Nova 'wait 10' | Out-Null
            $ff = Join-Path $Out 'vrm0_facing.cs'
            'var a = GameObject.Find("V0").GetComponent<Animator>(); var f = a.GetBonePosition(HumanBodyBones.LeftFoot); var t = a.GetBonePosition(HumanBodyBones.LeftToes); return f.x.ToString("F3", System.Globalization.CultureInfo.InvariantCulture) + " " + (t.z - f.z).ToString("F3", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $ff
            $fr = Invoke-NovaJson "exec --file $ff"
            $fa = if ($fr) { "$($fr.result)" -split ' ' } else { @() }
            $mats0 = @(Get-ChildItem $matDir -Filter *.mat -ErrorAction SilentlyContinue)
            $body = if (Test-Path (Join-Path $matDir 'F00_000_00_Body_00_SKIN.mat')) { Get-Content (Join-Path $matDir 'F00_000_00_Body_00_SKIN.mat') -Raw | ConvertFrom-Json } else { $null }
            $lil = @($mats0 | Where-Object { (Get-Content $_.FullName -Raw) -match '"Shader":\s*"lilToon"' }).Count
            $okMat = $body -and $body.Shader -eq 'lilToon' -and $body.AlphaClipping -eq 1 -and $body.Properties.UseOutline -and $body.Properties.OutlineWidth -gt 0.01 -and $body.Properties.OutlineWidth -lt 1
            Add-Result animation 'VRM 0.x (VRoid): faces +Z, MToon → lilToon (outline cm, alpha clip)' ($fa.Count -eq 2 -and [double]$fa[0] -lt 0 -and [double]$fa[1] -gt 0 -and $lil -eq $mats0.Count -and $okMat) ("left foot x {0}, toes ahead {1} m, lilToon {2}/{3}, body outline {4} cm" -f $fa[0], $fa[1], $lil, $mats0.Count, $(if ($body) { $body.Properties.OutlineWidth } else { '-' }))
            # 잘라내기 재질이 깊이 사전 패스에 깊이만 남기면 그 자리에 카메라 배경색 (#314D79) 이 그대로 보인다
            Invoke-Nova 'set "Main Camera" --position 0,0.9,1.6 --rotation 4,180,0' | Out-Null
            Invoke-Nova 'window game' | Out-Null
            Invoke-Nova 'wait 10' | Out-Null
            $shot = Join-Path $Out 'vrm0_front.png'
            Invoke-Nova "screenshot $shot --view game" | Out-Null
            $holes = -1
            if (Test-Path $shot)
            {
                Add-Type -AssemblyName System.Drawing
                $bmp = [System.Drawing.Bitmap]::FromFile($shot)
                $holes = 0
                for ($y = 0; $y -lt $bmp.Height; $y += 2) { for ($x = 0; $x -lt $bmp.Width; $x += 2) { $c = $bmp.GetPixel($x, $y); if ([math]::Abs($c.R - 49) -le 2 -and [math]::Abs($c.G - 77) -le 2 -and [math]::Abs($c.B - 121) -le 2) { $holes++ } } }
                $bmp.Dispose()
            }
            Add-Result animation 'VRM 0.x alpha-clipped clothes leave no depth-only holes' ($holes -ge 0 -and $holes -lt 50) "camera-clear pixels $holes (sampled every 2nd px)"
            $ef = Join-Path $Out 'vrm0_expr.cs'
            'var g = GameObject.Find("V0"); var e = g.GetComponent<Expressions>(); e.SetWeight("happy", 1f); float sum = 0; int shapes = 0; foreach (var r in g.GetComponentsInChildren<SkinnedMeshRenderer>()) { shapes += r.sharedMesh.blendShapeCount; for (int i = 0; i < r.sharedMesh.blendShapeCount; i++) sum += r.GetBlendShapeWeight(i); } return e.count + " " + shapes + " " + sum.ToString("F0", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $ef
            $exr = Invoke-NovaJson "exec --file $ef"
            $exa = if ($exr) { "$($exr.result)" -split ' ' } else { @() }
            Add-Result animation 'VRM 0.x expressions → Expressions (happy moves face blend shapes)' ($exa.Count -eq 3 -and [int]$exa[0] -ge 10 -and [int]$exa[1] -gt 20 -and [double]$exa[2] -gt 50) "expressions $($exa[0]) blend shapes $($exa[1]) weight sum $($exa[2])"
        }
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

# ------------------------------------------------------------------ Import Settings (.meta): 텍스처 Max Size · 압축, 오디오 Load Type · Mono, 모델 Scale · Rig
function Suite-Import
{
    Write-Host '[import]'
    $dir = Join-Path $Project 'Assets\NovaTestImport'
    New-Item -ItemType Directory -Force $dir | Out-Null
    # 3000 x 1500 불투명 PNG (기본 Max Size 2048 보다 크다)
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap 3000, 1500
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::FromArgb(255, 40, 120, 200)); $g.FillEllipse([System.Drawing.Brushes]::Orange, 500, 200, 2000, 1100); $g.Dispose()
    $bmp.Save((Join-Path $dir 'big.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    function Settings([string]$asset, $values) {
        if ($null -eq $values) { return Invoke-NovaJson "import-settings $asset" }
        if ($values -eq 'reset') { return Invoke-NovaJson "import-settings $asset --reset" }
        $v = ($values | ConvertTo-Json -Compress -Depth 5).Replace('"', '\"')
        Invoke-NovaJson "import-settings $asset --values `"$v`""
    }
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'scene new --force' | Out-Null
        # 텍스처
        $tex = 'Assets\NovaTestImport\big.png'
        $t0 = Settings $tex $null
        Add-Result import 'texture default: Max Size 2048 + Normal Quality (BC1)' ($t0 -and -not $t0.hasMeta -and $t0.imported.width -eq 2048 -and $t0.imported.height -eq 1024 -and $t0.imported.mips -gt 1 -and $t0.imported.format -match 'BC1') "$($t0.imported.sourceWidth)x$($t0.imported.sourceHeight) → $($t0.imported.width)x$($t0.imported.height) $($t0.imported.format), $($t0.imported.mips) mips, meta $($t0.hasMeta)"
        $t1 = Settings $tex @{ maxSize = 512; compression = 'None' }
        Add-Result import 'texture Max Size 512 + no compression' ($t1 -and $t1.hasMeta -and $t1.imported.width -eq 512 -and $t1.imported.height -eq 256 -and $t1.imported.format -notmatch 'BC') "$($t1.imported.width)x$($t1.imported.height) $($t1.imported.format), $($t1.imported.bytes) bytes"
        $t3 = Settings $tex @{ maxSize = 512; compression = 'NormalQuality'; textureType = 'NormalMap' }
        Add-Result import 'texture type Normal map → linear BC' ($t3 -and $t3.imported.format -match 'BC' -and $t3.imported.format -notmatch 'sRGB' -and $t3.settings.textureType -eq 'NormalMap') "$($t3.imported.format) (was $($t0.imported.format))"
        $t4 = Settings $tex @{ textureType = 'Sprite'; compression = 'None'; mipmaps = $false }   # Inspector 는 Sprite 로 바꿀 때 밉을 끈다
        Add-Result import 'texture type Sprite → no mip maps' ($t4 -and $t4.imported.mips -eq 1 -and $t4.settings.mipmaps -eq $false) "$($t4.imported.mips) mips"
        $t2 = Settings $tex 'reset'
        Add-Result import 'texture reset removes .meta' ($t2 -and -not $t2.hasMeta -and $t2.imported.width -eq 2048 -and $t2.imported.format -eq $t0.imported.format) "$($t2.imported.width)x$($t2.imported.height) $($t2.imported.format)"

        # 오디오 (long.mp3 가 있을 때)
        $mp3 = 'Assets\TestAssets\Audio\long.mp3'
        if (Test-Path (Join-Path $Project $mp3))
        {
            $a0 = Settings $mp3 $null
            $a1 = Settings $mp3 @{ loadType = 'Streaming'; forceToMono = $true }
            $a2 = Settings $mp3 'reset'
            $ok = $a0 -and -not $a0.imported.streaming -and $a1.imported.streaming -and $a1.imported.channels -eq 1 -and $a2 -and -not $a2.imported.streaming -and $a2.imported.channels -eq $a0.imported.channels
            Add-Result import 'audio Load Type + Force To Mono' $ok "default streaming=$($a0.imported.streaming) ch $($a0.imported.channels) → Streaming+Mono streaming=$($a1.imported.streaming) ch $($a1.imported.channels) (source $($a1.imported.sourceChannels)) → reset streaming=$($a2.imported.streaming)"
        }

        # 모델 (Cameron, Unity 샘플 — 테스트 프로젝트에만)
        $fbx = 'Assets\TestAssets\Cameron\Cameron_Model.fbx'
        if (Test-Path (Join-Path $Project $fbx))
        {
            $hf = Join-Path $Out 'import_head.cs'
            'return GameObject.Find("ICh").GetComponent<Animator>().GetBonePosition(HumanBodyBones.Head).y.ToString("F4", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $hf
            function Head { Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 20' | Out-Null; $h = Invoke-NovaJson "exec --file $hf"; Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 5' | Out-Null; if ($h) { [double]$h.result } else { 0 } }
            $m0 = Settings $fbx $null
            Invoke-Nova "create character --name ICh --model $fbx --controller Assets\TestAssets\Cameron\Locomotion.controller" | Out-Null
            Wait-Compile
            $h0 = Head
            $m1 = Settings $fbx @{ scaleFactor = 2 }
            $h1 = Head
            Add-Result import 'model Scale Factor 2 doubles the character' ($m0 -and $m1 -and [math]::Abs([double]$m1.imported.unitScale / [double]$m0.imported.unitScale - 2) -lt 1e-4 -and $h0 -gt 0.5 -and [math]::Abs($h1 / $h0 - 2) -lt 0.05) ("unitScale {0} → {1}, head y {2:F3} → {3:F3}" -f $m0.imported.unitScale, $m1.imported.unitScale, $h0, $h1)
            $m2 = Settings $fbx @{ scaleFactor = 1; animationType = 'Generic' }
            # 고치기: Neck 없음 (선택 본) + LeftFoot 을 무릎 노드로 (이미 LeftLowerLeg 라 무시돼야 — 두 본이 한 노드면 길이 0)
            $knee = $m0.imported.bones.LeftLowerLeg
            $opt = @('Neck', 'Chest', 'UpperChest', 'LeftShoulder', 'RightShoulder') | Where-Object { $m0.imported.bones.$_ } | Select-Object -First 1   # 모델에 있는 선택 본
            $m3 = Settings $fbx @{ animationType = 'Humanoid'; humanBones = @{ $opt = 'None'; LeftFoot = $knee } }
            $ok = $m0.imported.humanoid -and -not $m2.imported.humanoid -and $m3.imported.humanoid -and $opt -and -not $m3.imported.bones.$opt -and $m3.imported.bones.LeftFoot -eq $m0.imported.bones.LeftFoot
            Add-Result import 'model Rig: Generic / Humanoid bone override' $ok "auto humanoid $($m0.imported.humanoid) ($($m0.imported.humanBones) bones), Generic → $($m2.imported.humanoid), $opt $($m0.imported.bones.$opt) → None '$($m3.imported.bones.$opt)', LeftFoot → $($m3.imported.bones.LeftFoot) (duplicate ignored)"
            $m4 = Settings $fbx 'reset'
            Add-Result import 'model reset restores the automatic import' ($m4 -and -not $m4.hasMeta -and [math]::Abs([double]$m4.imported.unitScale - [double]$m0.imported.unitScale) -lt 1e-6 -and $m4.imported.bones.LeftFoot -eq $m0.imported.bones.LeftFoot) "unitScale $($m4.imported.unitScale), LeftFoot $($m4.imported.bones.LeftFoot)"
        }
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        foreach ($m in @('Assets\TestAssets\Audio\long.mp3.meta', 'Assets\TestAssets\Cameron\Cameron_Model.fbx.meta')) { Remove-Item -LiteralPath (Join-Path $Project $m) -ErrorAction SilentlyContinue }
        Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# ------------------------------------------------------------------ UI Text (SDF + TextMeshPro 기능): Rich Text · 링크 · 넘침 · Auto Size · textInfo
function Suite-UI
{
    Write-Host '[ui]'
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $bf = Join-Path $Out 'ui_text.cs'
        @'
var canvas = new GameObject("Canvas"); canvas.AddComponent<Canvas>();
System.Func<string, Vector2, Vector2, Text> make = (name, pos, size) => {
    var go = new GameObject(name); go.transform.SetParent(canvas.transform, false);
    var t = go.AddComponent<Text>(); var rt = go.GetComponent<RectTransform>(); rt.anchoredPosition = pos; rt.sizeDelta = size; return t; };
var body = make("Body", new Vector2(0, 0), new Vector2(800, 200));
body.text = "<b>굵게</b> <color=#FF0000>빨강</color> <size=150%>큰</size> <link=\"shop\">상점</link> <unknown>"; body.fontSize = 30;
var plain = make("Plain", new Vector2(0, -200), new Vector2(800, 60));
plain.text = "<b>x</b>"; plain.richText = false;
var el = make("Ellipsis", new Vector2(0, 200), new Vector2(200, 40));
el.text = "아주 긴 문장은 칸을 넘으면 말줄임표로 끝난다"; el.fontSize = 26; el.enableWordWrapping = false; el.overflowMode = TMPro.TextOverflowModes.Ellipsis;
var fit = make("Fit", new Vector2(0, -300), new Vector2(300, 50));
fit.text = "Auto Size 로 칸에 맞춘다"; fit.enableAutoSizing = true; fit.fontSizeMin = 8; fit.fontSizeMax = 80; fit.enableWordWrapping = false;
return "made";
'@ | Set-Content -Encoding utf8 $bf
        Invoke-Nova "exec --file $bf" | Out-Null
        Invoke-Nova 'wait 5' | Out-Null   # RectTransform 레이아웃 (다음 프레임) 뒤에 읽는다
        $rf = Join-Path $Out 'ui_text_read.cs'
        @'
var plain = GameObject.Find("Plain").GetComponent<Text>();
var el = GameObject.Find("Ellipsis").GetComponent<Text>();
var fit = GameObject.Find("Fit").GetComponent<Text>();
var tmp = GameObject.Find("Body").GetComponent<TMPro.TextMeshProUGUI>();
var info = tmp.textInfo;
int hit = -1; string id = "";
if (info.linkCount > 0) {
    var li = info.linkInfo[0];
    var c = info.characterInfo[li.linkTextfirstCharacterIndex];
    hit = TMPro.TMP_TextUtilities.FindIntersectingLink(tmp, tmp.transform.position + (c.bottomLeft + c.topRight) * 0.5f, null);
    id = li.GetLinkID() + "/" + li.GetLinkText();
}
return info.characterCount + "|" + info.linkCount + "|" + hit + "|" + id + "|" + plain.textInfo.characterCount + "|" + el.isTextOverflowing + "|" + fit.fontSizeUsed + "|" + tmp.GetParsedText();
'@ | Set-Content -Encoding utf8 $rf
        $r = Invoke-NovaJson "exec --file $rf"
        Invoke-Nova 'stop' | Out-Null
        $v = if ($r) { "$($r.result)" -split '\|' } else { @() }
        if ($v.Count -lt 8) { Add-Result ui 'text info' $false "exec failed: $r"; return }
        # "굵게 빨강 큰 상점 <unknown>" = 2+1+2+1+1+1+2+1+9 = 20 (태그는 빼고, 모르는 태그는 글자 그대로)
        Add-Result ui 'rich text tags are parsed (unknown tags stay)' ($v[0] -eq '20' -and $v[7] -eq '굵게 빨강 큰 상점 <unknown>') "chars $($v[0]) (expect 20), parsed '$($v[7])'"
        Add-Result ui 'link info + FindIntersectingLink' ($v[1] -eq '1' -and $v[2] -eq '0' -and $v[3] -eq 'shop/상점') "links $($v[1]), hit $($v[2]), $($v[3])"
        Add-Result ui 'rich text off shows tags as text' ($v[4] -eq '8') "chars $($v[4]) (expect 8)"
        Add-Result ui 'ellipsis overflow' ($v[5] -eq 'True') "isTextOverflowing $($v[5])"
        Add-Result ui 'auto size fits the box' ([int]$v[6] -ge 12 -and [int]$v[6] -lt 40) "font size used $($v[6]) (box 300x50)"

        # 자동 레이아웃: Horizontal(같은 폭 · flexible 2 배) · Vertical + Content Size Fitter · Grid · Aspect Ratio Fitter
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $lf = Join-Path $Out 'ui_layout.cs'
        @'
var canvas = new GameObject("Canvas"); canvas.AddComponent<Canvas>();
System.Func<string, Transform, Vector2, GameObject> make = (name, parent, size) => {
    var go = new GameObject(name); go.transform.SetParent(parent, false); go.AddComponent<Image>(); go.GetComponent<RectTransform>().sizeDelta = size; return go; };
var h = make("H", canvas.transform, new Vector2(600, 100));
var hg = h.AddComponent<HorizontalLayoutGroup>(); hg.spacing = 10; hg.padding = new RectOffset(10, 10, 5, 5);
for (int i = 0; i < 3; i++) make("H" + i, h.transform, new Vector2(50, 50));
var h2 = make("H2", canvas.transform, new Vector2(400, 60)); h2.GetComponent<RectTransform>().anchoredPosition = new Vector2(0, 150);
var hg2 = h2.AddComponent<HorizontalLayoutGroup>(); hg2.childForceExpandWidth = false;
var a = make("A", h2.transform, new Vector2(10, 10)); var ae = a.AddComponent<LayoutElement>(); ae.flexibleWidth = 1;
var b = make("B", h2.transform, new Vector2(10, 10)); var be = b.AddComponent<LayoutElement>(); be.flexibleWidth = 2;
var v = make("V", canvas.transform, new Vector2(300, 10)); v.GetComponent<RectTransform>().anchoredPosition = new Vector2(-400, 0);
var vg = v.AddComponent<VerticalLayoutGroup>(); vg.spacing = 4; vg.childForceExpandHeight = false;
var fit = v.AddComponent<ContentSizeFitter>(); fit.verticalFit = ContentSizeFitter.FitMode.PreferredSize;
for (int i = 0; i < 4; i++) { var t = new GameObject("T" + i); t.transform.SetParent(v.transform, false); var tx = t.AddComponent<Text>(); tx.text = "줄 " + i; tx.fontSize = 20; }
var g = make("G", canvas.transform, new Vector2(300, 300)); g.GetComponent<RectTransform>().anchoredPosition = new Vector2(400, 0);
var gg = g.AddComponent<GridLayoutGroup>(); gg.cellSize = new Vector2(90, 90); gg.spacing = new Vector2(10, 10);
for (int i = 0; i < 7; i++) make("G" + i, g.transform, new Vector2(10, 10));
var r = make("R", canvas.transform, new Vector2(400, 50)); r.GetComponent<RectTransform>().anchoredPosition = new Vector2(0, -250);
var ar = r.AddComponent<AspectRatioFitter>(); ar.aspectMode = AspectRatioFitter.AspectMode.WidthControlsHeight; ar.aspectRatio = 2;
return "made";
'@ | Set-Content -Encoding utf8 $lf
        Invoke-Nova "exec --file $lf" | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $lr = Join-Path $Out 'ui_layout_read.cs'
        @'
System.Func<string, Rect> R = n => GameObject.Find(n).GetComponent<RectTransform>().rect;
System.Func<string, Vector2> P = n => GameObject.Find(n).GetComponent<RectTransform>().anchoredPosition;
System.Func<float, string> F = x => x.ToString("F1", System.Globalization.CultureInfo.InvariantCulture);
var t0 = R("T0");
return F(R("H0").width) + " " + F(R("H1").width) + " " + F(P("H1").x - P("H0").x) + " " + F(R("A").width) + " " + F(R("B").width) + " "
    + F(R("V").height) + " " + F(t0.height) + " " + F(P("G3").x - P("G0").x) + " " + F(P("G0").y - P("G3").y) + " " + F(R("R").height);
'@ | Set-Content -Encoding utf8 $lr
        $lo = Invoke-NovaJson "exec --file $lr"
        Invoke-Nova 'stop' | Out-Null
        $l = if ($lo) { @("$($lo.result)" -split ' ' | ForEach-Object { [double]$_ }) } else { @() }
        if ($l.Count -lt 10) { Add-Result ui 'layout' $false "exec failed: $lo"; return }
        $cell = (600 - 20 - 20) / 3.0
        Add-Result ui 'horizontal layout group splits the width' ([math]::Abs($l[0] - $cell) -lt 0.2 -and [math]::Abs($l[1] - $cell) -lt 0.2 -and [math]::Abs($l[2] - ($cell + 10)) -lt 0.2) ("widths {0} {1} (expect {2:F1}), step {3}" -f $l[0], $l[1], $cell, $l[2])
        Add-Result ui 'layout element flexible width 1 : 2' ([math]::Abs($l[4] / [math]::Max(0.01, $l[3]) - 2) -lt 0.02 -and [math]::Abs($l[3] + $l[4] - 400) -lt 0.5) "A $($l[3]) B $($l[4]) (expect 133.3 / 266.7)"
        Add-Result ui 'vertical group + content size fitter' ($l[6] -gt 15 -and [math]::Abs($l[5] - (4 * $l[6] + 3 * 4)) -lt 0.5) "height $($l[5]) = 4 x $($l[6]) + 3 x 4"
        Add-Result ui 'grid layout wraps after 3 columns' ([math]::Abs($l[7]) -lt 0.1 -and [math]::Abs($l[8] - 100) -lt 0.1) "G3 - G0 = ($($l[7]), $($l[8])) (expect 0, 100)"
        Add-Result ui 'aspect ratio fitter (width controls height)' ([math]::Abs($l[9] - 200) -lt 0.1) "height $($l[9]) (expect 200)"

        # World Space · Screen Space - Camera 캔버스: 배치 + 카메라 광선으로 맞추기 (UIRaycast.Pick)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create empty --name WC' | Out-Null
        Invoke-Nova 'add-component WC Canvas --values "{\"renderMode\":2}"' | Out-Null
        Invoke-Nova 'add-component WC GraphicRaycaster' | Out-Null
        Invoke-Nova 'create empty --name CC' | Out-Null
        Invoke-Nova 'add-component CC Canvas --values "{\"renderMode\":1,\"planeDistance\":10}"' | Out-Null
        Invoke-Nova 'add-component CC GraphicRaycaster' | Out-Null
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $wf = Join-Path $Out 'ui_world.cs'
        @'
var cam = Camera.main.transform;
var wc = GameObject.Find("WC");
wc.transform.position = cam.position + cam.forward * 5f; wc.transform.rotation = cam.rotation; wc.transform.localScale = new Vector3(0.01f, 0.01f, 0.01f);
wc.GetComponent<RectTransform>().sizeDelta = new Vector2(400, 200);
var w = new GameObject("WBtn"); w.transform.SetParent(wc.transform, false); w.AddComponent<Image>().color = new Color(0.2f, 0.6f, 1f, 1f);
w.GetComponent<RectTransform>().sizeDelta = new Vector2(200, 100);
var cc = GameObject.Find("CC").GetComponent<Canvas>(); cc.worldCamera = Camera.main;
var c = new GameObject("CBtn"); c.transform.SetParent(cc.transform, false); c.AddComponent<Image>().color = new Color(1f, 0.5f, 0.2f, 1f);
var crt = c.GetComponent<RectTransform>(); crt.sizeDelta = new Vector2(150, 80); crt.anchoredPosition = new Vector2(-300, 150);
return "made";
'@ | Set-Content -Encoding utf8 $wf
        Invoke-Nova "exec --file $wf" | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $wr = Join-Path $Out 'ui_world_read.cs'
        @'
float W = Screen.width, H = Screen.height;
System.Func<float, float, string> pick = (x, y) => { var g = UIRaycast.Pick(new Vector2(x, y)); return g == null ? "none" : g.name; };
var cam = Camera.main.transform;
var cc = GameObject.Find("CC");
float dist = (cc.transform.position - cam.position).magnitude;
var cv = cc.GetComponent<Canvas>();
return pick(W / 2, H / 2) + " " + pick(W / 2 - 300, H / 2 + 150) + " " + pick(W - 5, 5) + " " + dist.ToString("F2", System.Globalization.CultureInfo.InvariantCulture) + " " + (int)cv.renderMode + " " + (cv.worldCamera != null);
'@ | Set-Content -Encoding utf8 $wr
        $wo = Invoke-NovaJson "exec --file $wr"
        Invoke-Nova 'stop' | Out-Null
        $wv = if ($wo) { "$($wo.result)" -split ' ' } else { @() }
        if ($wv.Count -lt 6) { Add-Result ui 'world canvas' $false "exec failed: $wo"; return }
        Add-Result ui 'world space canvas is hit by the camera ray' ($wv[0] -eq 'WBtn' -and $wv[2] -eq 'none') "center → $($wv[0]) (expect WBtn), corner → $($wv[2]) (expect none)"
        # Dropdown · Scrollbar · Scroll View 의 스크롤바
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create ui:Dropdown --name DD' | Out-Null
        Invoke-Nova 'create ui:Scrollbar --name SB' | Out-Null
        Invoke-Nova 'create ui:ScrollView --name SV' | Out-Null
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $df = Join-Path $Out 'ui_dropdown.cs'
        @'
var dd = GameObject.Find("DD").GetComponent<TMPro.TMP_Dropdown>();
int before = dd.options.Count;
dd.AddOptions(new System.Collections.Generic.List<string> { "D 옵션", "E 옵션" });
dd.value = 2;
dd.onValueChanged.AddListener(v => dd.gameObject.name = "DD" + v);   // 리스너가 불렸는지 = 이름으로 확인
dd.Show();
GameObject.Find("SB").GetComponent<Scrollbar>().value = 0.5f;
GameObject.Find("Scrollbar Vertical").GetComponent<Scrollbar>().value = 0f;
return before + " " + dd.options.Count + " " + dd.IsExpanded;
'@ | Set-Content -Encoding utf8 $df
        $d1 = Invoke-NovaJson "exec --file $df"
        Invoke-Nova 'wait 5' | Out-Null
        $cf = Join-Path $Out 'ui_dropdown_click.cs'
        @'
var item = GameObject.Find("Item 4: E 옵션");
var caption = GameObject.Find("Label").GetComponent<Text>().text;
if (item == null) return "noitem " + caption;
var tg = item.GetComponent<Toggle>(); tg.isOn = !tg.isOn;   // 항목을 누른 것과 같다
return "ok " + caption;
'@ | Set-Content -Encoding utf8 $cf
        $d2 = Invoke-NovaJson "exec --file $cf"
        Invoke-Nova 'wait 5' | Out-Null
        $rf3 = Join-Path $Out 'ui_dropdown_read.cs'
        @'
var dd = GameObject.Find("DD4");
var d = dd != null ? dd.GetComponent<Dropdown>() : null;
var handle = GameObject.Find("SB").transform.GetChild(0).GetChild(0).GetComponent<RectTransform>();
var sv = GameObject.Find("SV").GetComponent<ScrollRect>();
var vbar = GameObject.Find("Scrollbar Vertical").GetComponent<Scrollbar>();
var F = (System.Func<float, string>)(x => x.ToString("F2", System.Globalization.CultureInfo.InvariantCulture));
return (d != null ? d.value + " " + d.IsExpanded + " " + GameObject.Find("Label").GetComponent<Text>().text.Replace(" ", "_") : "none none none")
    + " " + (GameObject.Find("Dropdown List") != null) + " " + F(handle.anchorMin.x) + " " + F(handle.anchorMax.x) + " " + F(vbar.size) + " " + F(sv.normalizedPosition.y);
'@ | Set-Content -Encoding utf8 $rf3
        $d3 = Invoke-NovaJson "exec --file $rf3"
        Invoke-Nova 'stop' | Out-Null
        $dv = if ($d3) { "$($d3.result)" -split ' ' } else { @() }
        $ok = $d1 -and "$($d1.result)" -eq '3 5 True' -and $d2 -and "$($d2.result)" -eq 'ok Option C' -and $dv.Count -ge 8 -and $dv[0] -eq '4' -and $dv[1] -eq 'False' -and $dv[2] -eq 'E_옵션' -and $dv[3] -eq 'False'
        Add-Result ui 'dropdown: options, show list, pick item, caption' $ok "add $($d1.result); before pick caption '$($d2.result)'; after: value $($dv[0]) expanded $($dv[1]) caption $($dv[2]) list $($dv[3]) (listener renamed DD → DD4)"
        if ($dv.Count -ge 8)
        {
            Add-Result ui 'scrollbar handle follows value and size' ([math]::Abs([double]$dv[4] - 0.4) -lt 0.01 -and [math]::Abs([double]$dv[5] - 0.6) -lt 0.01) "anchors $($dv[4]) ~ $($dv[5]) (expect 0.40 ~ 0.60)"
            Add-Result ui 'scroll view vertical scrollbar drives the content' ([double]$dv[6] -gt 0.3 -and [double]$dv[6] -lt 0.95 -and [double]$dv[7] -lt 0.02) "bar size $($dv[6]), normalized y $($dv[7]) (expect 0 after bar = 0)"
        }
        Add-Result ui 'screen space camera canvas sits at plane distance' ($wv[1] -eq 'CBtn' -and [math]::Abs([double]$wv[3] - 10) -lt 0.01 -and $wv[4] -eq '1' -and $wv[5] -eq 'True') "(-300, 150) → $($wv[1]) (expect CBtn), distance $($wv[3]) (expect 10), mode $($wv[4]), camera $($wv[5])"
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

function Wait-Compile { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 60) { Invoke-Nova 'wait 20' | Out-Null; if (-not (Info).compiling) { return } } }

function Suite-Packages
{
    Write-Host '[packages]'
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $before = if (Test-Path $manifest) { Get-Content $manifest -Raw } else { $null }
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'package remove com.nova.cameras' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        $l = Invoke-NovaJson 'package list'
        $cam = $l.packages | Where-Object { $_.name -eq 'com.nova.cameras' }
        Add-Result packages 'registry lists com.nova.cameras' ($cam -and -not $cam.inProject -and -not $cam.loaded) "inProject=$($cam.inProject) loaded=$($cam.loaded)"
        $a = Invoke-NovaJson 'package add com.nova.cameras'
        $inManifest = (Test-Path $manifest) -and ((Get-Content $manifest -Raw) -match 'com.nova.cameras')
        Add-Result packages 'add → DLL loaded + manifest' ($a -and $a.loaded -and $inManifest) "loaded=$($a.loaded) manifest=$inManifest"
        Wait-Compile
        $cf = Join-Path $Out 'pkg_cs.cs'
        'var f = GameObject.Find("Main Camera").AddComponent<FollowCamera>(); f.distance = 6.5f; return f.distance;' | Set-Content -Encoding utf8 $cf
        $e = Invoke-NovaJson "exec --file $cf"
        Add-Result packages 'C# API of the package (FollowCamera)' ($e -and $e.result -eq '6.5') "result=$($e.result)"
        Invoke-Nova 'remove-component "Main Camera" FollowCamera' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $r = Invoke-NovaJson 'package remove com.nova.cameras'
        $l2 = Invoke-NovaJson 'package list'
        $cam2 = $l2.packages | Where-Object { $_.name -eq 'com.nova.cameras' }
        Add-Result packages 'remove (unused) → unloaded now' ($r -and -not $r.restartRequired -and -not $cam2.loaded) "restartRequired=$($r.restartRequired) loaded=$($cam2.loaded)"
        # 의존성: starter-assets 를 넣으면 cameras 도 같이 들어간다
        Invoke-Nova 'package add com.nova.starter-assets' | Out-Null
        $m = if (Test-Path $manifest) { Get-Content $manifest -Raw } else { '' }
        Add-Result packages 'dependency is added too (starter-assets → cameras)' ($m -match 'com.nova.cameras' -and $m -match 'com.nova.starter-assets') (($m -replace '\s+', ' ').Trim())
        Invoke-Nova 'package remove com.nova.starter-assets' | Out-Null
        Invoke-Nova 'package remove com.nova.cameras' | Out-Null

        # AI Navigation: 벽(가운데 8 m) 바닥 굽기 → 길이 벽 끝을 돌아간다 → NPC 가 도착
        Invoke-Nova 'package add com.nova.ai.navigation' | Out-Null
        Wait-Compile
        foreach ($l in @('scene new --force', 'create cube --name NGround --position 0,-0.5,0 --scale 30,1,30', 'create cube --name NWall --position 0,1,0 --scale 0.5,2,8',
                         'create empty --name NSurface', 'add-component NSurface NavMeshSurface', 'create capsule --name NNpc --position -5,1,0',
                         'remove-component NNpc CapsuleCollider', 'add-component NNpc NavMeshAgent --values "{\"baseOffset\":1}"')) { Invoke-Nova $l | Out-Null }
        $nf = Join-Path $Out 'nav_bake.cs'
        'GameObject.Find("NSurface").GetComponent<NovaEngine.AI.NavMeshSurface>().BuildNavMesh(); var p = new NovaEngine.AI.NavMeshPath(); NovaEngine.AI.NavMesh.CalculatePath(new Vector3(-5,0,0), new Vector3(5,0,0), NovaEngine.AI.NavMesh.AllAreas, p); float maxZ = 0; foreach (var c in p.corners) maxZ = Mathf.Max(maxZ, Mathf.Abs(c.z)); return p.corners.Length + " " + maxZ.ToString("F2");' | Set-Content -Encoding utf8 $nf
        $nb = Invoke-NovaJson "exec --file $nf"
        $nv = if ($nb) { "$($nb.result)" -split ' ' } else { @() }
        Add-Result packages 'navmesh bake + path goes around the wall' ($nv.Count -eq 2 -and [int]$nv[0] -ge 3 -and [double]$nv[1] -gt 4.0) "corners $($nv[0]), max |z| $($nv[1]) (expect ≥ 3, > 4)"
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $gf = Join-Path $Out 'nav_go.cs'
        'return GameObject.Find("NNpc").GetComponent<NovaEngine.AI.NavMeshAgent>().SetDestination(new Vector3(5, 0, 0));' | Set-Content -Encoding utf8 $gf
        Invoke-Nova "exec --file $gf" | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 6) { Invoke-Nova 'wait 20' | Out-Null }
        $np = (Invoke-NovaJson 'get NNpc').position
        Add-Result packages 'nav mesh agent walks to the destination' ([math]::Abs([double]$np[0] - 5) -lt 0.2 -and [math]::Abs([double]$np[2]) -lt 0.2) ("NPC {0:F2}, {1:F2} (expect 5, 0)" -f [double]$np[0], [double]$np[2])
        Invoke-Nova 'stop' | Out-Null

        # NavMesh Link: 2 m 틈으로 떨어진 두 발판 → 링크로 이어진 길 → NPC 가 건너편에 도착
        foreach ($l in @('scene new --force', 'create cube --name NA --position -6,-0.5,0 --scale 10,1,10', 'create cube --name NB --position 6,-0.5,0 --scale 10,1,10',
                         'create empty --name NSurface', 'add-component NSurface NavMeshSurface', 'create empty --name NLink',
                         'add-component NLink NavMeshLink --values "{\"startPoint\":[-1.6,0,0],\"endPoint\":[1.6,0,0]}"',
                         'create capsule --name NNpc --position -6,1,0', 'remove-component NNpc CapsuleCollider', 'add-component NNpc NavMeshAgent --values "{\"baseOffset\":1}"')) { Invoke-Nova $l | Out-Null }
        Invoke-Nova 'wait 3' | Out-Null
        $lf = Join-Path $Out 'nav_link.cs'
        'GameObject.Find("NSurface").GetComponent<NovaEngine.AI.NavMeshSurface>().BuildNavMesh(); var p = new NovaEngine.AI.NavMeshPath(); NovaEngine.AI.NavMesh.CalculatePath(new Vector3(-6,0,0), new Vector3(6,0,0), NovaEngine.AI.NavMesh.AllAreas, p); return p.corners.Length + " " + p.corners[p.corners.Length - 1].x.ToString("F2");' | Set-Content -Encoding utf8 $lf
        $lr = Invoke-NovaJson "exec --file $lf"
        $lv = if ($lr) { "$($lr.result)" -split ' ' } else { @() }
        Add-Result packages 'navmesh link joins two platforms' ($lv.Count -eq 2 -and [int]$lv[0] -eq 4 -and [math]::Abs([double]$lv[1] - 6) -lt 0.1) "corners $($lv[0]), end x $($lv[1]) (expect 4, 6)"
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        Invoke-Nova "exec --file $gf" | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 6) { Invoke-Nova 'wait 20' | Out-Null }
        $np = (Invoke-NovaJson 'get NNpc').position
        Add-Result packages 'agent crosses the off-mesh link' ([math]::Abs([double]$np[0] - 5) -lt 0.2) ("NPC x {0:F2} (expect 5)" -f [double]$np[0])
        Invoke-Nova 'stop' | Out-Null

        # NavMesh Obstacle Carve: 콜라이더 없는 2x2x12 상자가 Play 중 멈춰 있으면 길이 돌아간다, 치우면 다시 곧게
        foreach ($l in @('scene new --force', 'create cube --name NGround --position 0,-0.5,0 --scale 20,1,20',
                         'create empty --name NSurface', 'add-component NSurface NavMeshSurface',
                         'create cube --name NObs --position 0,1,0 --scale 2,2,12', 'remove-component NObs BoxCollider',
                         'add-component NObs NavMeshObstacle --values "{\"carve\":true}"')) { Invoke-Nova $l | Out-Null }
        $of = Join-Path $Out 'nav_obs.cs'
        'var p = new NovaEngine.AI.NavMeshPath(); NovaEngine.AI.NavMesh.CalculatePath(new Vector3(-5,0,0), new Vector3(5,0,0), NovaEngine.AI.NavMesh.AllAreas, p); float mz = 0; foreach (var c in p.corners) mz = Mathf.Max(mz, Mathf.Abs(c.z)); return mz.ToString("F2");' | Set-Content -Encoding utf8 $of
        $obf = Join-Path $Out 'nav_obs_bake.cs'
        'GameObject.Find("NSurface").GetComponent<NovaEngine.AI.NavMeshSurface>().BuildNavMesh(); return "ok";' | Set-Content -Encoding utf8 $obf
        Invoke-Nova "exec --file $obf" | Out-Null
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 1.2) { Invoke-Nova 'wait 10' | Out-Null }
        $z1 = [double](Invoke-NovaJson "exec --file $of").result
        $omf = Join-Path $Out 'nav_obs_move.cs'
        'GameObject.Find("NObs").transform.position = new Vector3(0, 1, 40); return "moved";' | Set-Content -Encoding utf8 $omf
        Invoke-Nova "exec --file $omf" | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 1.2) { Invoke-Nova 'wait 10' | Out-Null }
        $z2 = [double](Invoke-NovaJson "exec --file $of").result
        Add-Result packages 'carving obstacle reroutes the path' ($z1 -gt 6.0 -and $z2 -lt 0.1) ("max |z| {0:F2} with obstacle, {1:F2} after moving it (expect > 6, 0)" -f $z1, $z2)
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'package remove com.nova.ai.navigation' | Out-Null
        Remove-Item (Join-Path $Project 'Assets\NavMesh-NSurface.navmesh') -ErrorAction SilentlyContinue
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        if ($before) { [IO.File]::WriteAllText($manifest, $before) }
    }
}

# ------------------------------------------------------------------ 오디오: Audio Mixer 그룹으로 보내기 · 노출 파라미터 · 스냅숏 전환 (그룹 레벨 미터로 확인)
function Suite-Model
{
    # 모델 편집기 패키지: CLI 연산 (만들기 · 돌출 · Inset · Loop Cut · Bevel · Subsurf · Mirror · Undo) → 점 · 면 수 · 경계 상자,
    # 내보내기 (FBX · OBJ · GLB) → 다시 가져와 같은 모양인지, PNG 렌더, .nmodel 저장 · 열기
    Write-Host '[model]'
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $before = if (Test-Path $manifest) { Get-Content $manifest -Raw } else { $null }
    $dir = Join-Path $Out 'model'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $ed = Start-TestEditor
    try
    {
        $a = Invoke-NovaJson 'package add com.nova.modeling'
        Add-Result model 'package loads (NovaModeling.dll)' ($a -and $a.loaded) "loaded=$($a.loaded)"
        $h = Invoke-NovaJson 'model help'
        Add-Result model 'model help lists ops' ($h -and $h.extrude -and $h.loopcut -and $h.render) "ops=$(@($h.PSObject.Properties).Count)"
        function M([string]$line) { Invoke-NovaJson "model $line" }
        function Closed($r) { $r -and $r.boundaryEdges -eq 0 -and $r.nonManifoldEdges -eq 0 }
        function Near([double]$a, [double]$b) { [math]::Abs($a - $b) -lt 0.002 }
        function Wait-Sec([double]$s) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Invoke-Nova 'wait 10' | Out-Null } }

        # 정육면체 → 윗면 돌출 → Inset → 다시 돌출 → Undo
        M 'new' | Out-Null
        $c = M 'add --type cube --size 2'
        Add-Result model 'add cube (8 verts, 6 faces, 2 m)' ($c -and $c.verts -eq 8 -and $c.faces -eq 6 -and (Near $c.size[0] 2)) "verts=$($c.verts) faces=$($c.faces) size=$($c.size -join ',')"
        M 'mode --mode edit --select face' | Out-Null
        $s = M 'select.normal --direction 0,1,0'
        Add-Result model 'select.normal up → 1 face' ($s -and $s.selection.faces -eq 1) "faces=$($s.selection.faces)"
        $e = M 'extrude --distance 1'
        Add-Result model 'extrude top face 1 m (12 verts, 10 faces, top y=2, closed)' ($e -and $e.verts -eq 12 -and $e.faces -eq 10 -and (Near $e.max[1] 2) -and (Closed $e)) "verts=$($e.verts) faces=$($e.faces) maxY=$($e.max[1]) open=$($e.boundaryEdges)"
        $i = M 'inset --thickness 0.25'
        Add-Result model 'inset (16 verts, 14 faces, closed)' ($i -and $i.verts -eq 16 -and $i.faces -eq 14 -and (Closed $i)) "verts=$($i.verts) faces=$($i.faces) open=$($i.boundaryEdges)"
        $e2 = M 'extrude --distance 0.5'
        $sel = M 'get --what verts --selected'
        $inner = $sel -and $sel.total -eq 4 -and @($sel.items | Where-Object { [math]::Abs([math]::Abs($_[1]) - 0.75) -lt 0.002 }).Count -eq 4
        Add-Result model 'extrude inset face (top y=2.5, cap = inner 1.5 m square)' ($e2 -and (Near $e2.max[1] 2.5) -and $inner -and (Closed $e2)) "maxY=$($e2.max[1]) cap verts=$($sel.total)"
        $u = M 'undo'
        Add-Result model 'undo → top back at y=2' ($u -and (Near $u.max[1] 2) -and $u.faces -eq 14) "maxY=$($u.max[1]) faces=$($u.faces)"
        $tower = M 'extrude --distance 0.5'
        M 'mode --mode object' | Out-Null
        foreach ($ext in 'fbx', 'obj', 'glb')
        {
            $x = M "export $dir\tower.$ext"
            $f = Join-Path $dir "tower.$ext"
            Add-Result model "export .$ext" ($x -and (Test-Path $f) -and (Get-Item $f).Length -gt 200) "bytes=$(if (Test-Path $f) { (Get-Item $f).Length })"
        }
        # 다시 가져오기: FBX · OBJ 는 사각형 그대로 (같은 점 · 면 수), GLB 는 삼각형 (같은 삼각형 수), 경계 상자 같음
        foreach ($ext in 'fbx', 'obj', 'glb')
        {
            $r = M "import $dir\tower.$ext"
            $sameShape = $r -and (Near $r.min[0] $tower.min[0]) -and (Near $r.max[1] $tower.max[1]) -and (Near $r.min[2] $tower.min[2]) -and (Near $r.max[2] $tower.max[2])
            $counts = if ($ext -eq 'glb') { $r.tris -eq $tower.tris } else { $r.verts -eq $tower.verts -and $r.faces -eq $tower.faces }
            Add-Result model "re-import .$ext (same shape$(if ($ext -ne 'glb') { ', quads kept' }))" ($sameShape -and $counts) "verts $($r.verts)/$($tower.verts) faces $($r.faces)/$($tower.faces) tris $($r.tris)/$($tower.tris) min $($r.min -join ',') max $($r.max -join ',')"
        }

        # Loop Cut: 정육면체 변 하나 → 고리 4 면이 나뉜다
        M 'new' | Out-Null; M 'add --type cube' | Out-Null; M 'mode --mode edit' | Out-Null
        $l = M 'loopcut --edge 0'
        Add-Result model 'loop cut on a cube (12 verts, 10 faces, closed)' ($l -and $l.verts -eq 12 -and $l.faces -eq 10 -and (Closed $l)) "verts=$($l.verts) faces=$($l.faces) open=$($l.boundaryEdges)"
        # Bevel: 변 하나 → 띠 면 + 점 2 개
        M 'new' | Out-Null; M 'add --type cube' | Out-Null; M 'mode --mode edit --select edge' | Out-Null
        M 'select.edges --ids [0]' | Out-Null
        $b = M 'bevel --offset 0.1'
        Add-Result model 'bevel one cube edge (10 verts, 7 faces, closed)' ($b -and $b.verts -eq 10 -and $b.faces -eq 7 -and (Closed $b)) "verts=$($b.verts) faces=$($b.faces) open=$($b.boundaryEdges) nonManifold=$($b.nonManifoldEdges)"
        # Subdivision Surface: 정육면체 1 단계 = 24 면, 26 점, 둥글어진다
        M 'new' | Out-Null; M 'add --type cube --size 2' | Out-Null; M 'mode --mode edit' | Out-Null
        $ss = M 'subsurf --levels 1'
        Add-Result model 'subsurf level 1 (26 verts, 24 faces, closed)' ($ss -and $ss.verts -eq 26 -and $ss.faces -eq 24 -and (Closed $ss)) "verts=$($ss.verts) faces=$($ss.faces) open=$($ss.boundaryEdges)"
        # Mirror: 오른쪽 반 → X 로 뒤집어 붙이고 가운데 용접
        M 'new' | Out-Null; M 'add --type cube' | Out-Null; M 'mode --mode edit' | Out-Null
        M 'select.all' | Out-Null; M 'translate --delta 0.5,0,0' | Out-Null
        $mi = M 'mirror --axis x'
        Add-Result model 'mirror X with center weld (12 verts, x -1..1)' ($mi -and $mi.verts -eq 12 -and (Near $mi.min[0] -1) -and (Near $mi.max[0] 1)) "verts=$($mi.verts) min x=$($mi.min[0])"

        # ---- 2 단계: 버텍스 그룹 (돌출 · 나누기 · 거울에서 따라감), 체크포인트, batch (Undo 한 번), 기준 그림 + 실루엣 비교
        M 'new' | Out-Null; M 'add --type cube --size 2' | Out-Null; M 'mode --mode edit --select face' | Out-Null
        M 'select.normal --direction 0,1,0' | Out-Null
        M 'group.assign --name Top' | Out-Null
        M 'extrude --distance 1' | Out-Null
        M 'subdivide' | Out-Null
        $gl = M 'group.list'
        $top = $gl.groups | Where-Object { $_.name -eq 'Top' }
        Add-Result model 'vertex group follows extrude + subdivide (4 → 8 → 13 verts)' ($top -and $top.verts -eq 13) "Top verts=$($top.verts)"
        $gs = M 'group.select --name Top'
        Add-Result model 'group.select selects the group' ($gs -and $gs.selection.verts -eq 13) "selected=$($gs.selection.verts)"
        M 'new' | Out-Null; M 'add --type cube' | Out-Null; M 'mode --mode edit' | Out-Null; M 'select.all' | Out-Null
        M 'translate --delta 0.5,0,0' | Out-Null; M 'group.assign --name Hand.L' | Out-Null
        M 'mirror --axis x' | Out-Null
        $mg = (M 'group.list').groups
        $hr = $mg | Where-Object { $_.name -eq 'Hand.R' }
        Add-Result model 'mirror X: new side goes to Hand.R' ($hr -and $hr.verts -eq 4 -and $hr.max[0] -le 0.0001) "groups=$(($mg | ForEach-Object { "$($_.name):$($_.verts)" }) -join ' ')"
        M "save $dir\groups.nmodel" | Out-Null; M 'new' | Out-Null; M "open $dir\groups.nmodel" | Out-Null
        M 'object.select --name Cube' | Out-Null
        $og = (M 'group.list').groups
        Add-Result model 'groups saved in .nmodel' (@($og).Count -eq 2) "groups=$(($og | ForEach-Object { "$($_.name):$($_.verts)" }) -join ' ')"

        M 'new' | Out-Null; M 'add --type cube' | Out-Null
        M 'checkpoint --save one' | Out-Null
        M 'add --type uvsphere --location 2,0,0' | Out-Null
        $cr = M 'checkpoint --restore one'
        $cu = M 'undo'
        Add-Result model 'checkpoint restore (and undo it)' ($cr -and $cr.objects -eq 1 -and $cu -and $cu.objects -eq 2) "restore → $($cr.objects) objects, undo → $($cu.objects)"

        $bf = Join-Path $dir 'steps.txt'
        @('# 탑', 'add --type cube --size 1 --name Tower', 'mode --mode edit --select face', 'select.normal --direction 0,1,0', 'extrude --distance 0.5', 'nova model inset --thickness 0.1') | Set-Content -Encoding utf8 $bf
        M 'new' | Out-Null
        $bt = M "batch $bf"
        $bu = M 'undo'
        Add-Result model 'batch file (5 ops) = one undo step' ($bt -and $bt.faces -eq 14 -and @($bt.steps).Count -eq 5 -and $bu -and $bu.objects -eq 0) "faces=$($bt.faces) steps=$(@($bt.steps).Count) after undo objects=$($bu.objects)"
        M 'redo' | Out-Null
        @('select.all', 'extrude --distance 0.3', 'loopcut') | Set-Content -Encoding utf8 $bf
        $bad = Invoke-Nova "model batch $bf"
        $after = M 'info'
        Add-Result model 'failing batch rolls back all steps' ($bad -match 'step 3' -and $after.faces -eq 14) "faces=$($after.faces) msg=$(($bad -split "`n")[0])"

        # 기준 그림: 300x400 흰 바탕에 검은 사각형 (2 m 높이 = 200 px/m → 0.6 x 1.6 m, 바닥에 섬)
        Add-Type -AssemblyName System.Drawing
        $png = Join-Path $dir 'ref_front.png'
        $bmp = New-Object System.Drawing.Bitmap 300, 400
        $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::White); $g.FillRectangle([System.Drawing.Brushes]::Black, 90, 80, 120, 320); $g.Dispose()
        $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        M 'new' | Out-Null
        $ra = M "ref.add $png --view front --height 2"
        M 'add --type cube --dimensions 0.6,1.6,0.6 --location 0,0.8,0' | Out-Null
        $c1 = M 'compare'
        Add-Result model 'reference + compare: matching box IoU > 0.95' ($ra -and $c1 -and $c1.iou -gt 0.95) "iou=$($c1.iou) extra=$($c1.extra) missing=$($c1.missing)"
        M 'mode --mode edit' | Out-Null; M 'select.all' | Out-Null; M 'scale --factor 1.5,1,1' | Out-Null
        $c2 = M "compare --out $dir\diff.png"
        $band = @($c2.bands | Where-Object { [math]::Abs($_.meshWidth - 0.9) -lt 0.02 -and [math]::Abs($_.refWidth - 0.6) -lt 0.02 })
        Add-Result model 'compare: 1.5x wider mesh → IoU ~0.67, widths 0.9 vs 0.6, hint, diff PNG' ($c2 -and $c2.iou -lt 0.75 -and $c2.iou -gt 0.6 -and $band.Count -gt 0 -and @($c2.hints).Count -gt 0 -and (Test-Path "$dir\diff.png")) "iou=$($c2.iou) bands ok=$($band.Count) hint=$(@($c2.hints)[0])"

        # ---- 3 단계: 캐릭터 도구
        # 거울 모디파이어: 왼쪽 면을 뺀 반쪽 상자 → 결과는 닫힌 상자, 가운데 점은 옮겨도 X = 0 (Clipping), 내보내기 = 결과
        M 'new' | Out-Null; M 'add --type cube' | Out-Null; M 'mode --mode edit --select face' | Out-Null
        M 'select.normal --direction -1,0,0 --angle 10' | Out-Null; M 'delete --type faces' | Out-Null
        M 'select.all' | Out-Null; M 'translate --delta 0.5,0,0' | Out-Null
        $mm = M 'modifier.mirror --enable true'
        Add-Result model 'mirror modifier: half box (5 faces) → closed 10 faces' ($mm -and $mm.faces -eq 5 -and $mm.evaluated.faces -eq 10 -and $mm.evaluated.boundaryEdges -eq 0) "base $($mm.faces), evaluated $($mm.evaluated.faces) open $($mm.evaluated.boundaryEdges)"
        $mc = M 'translate --delta 0.3,0,0'
        Add-Result model 'mirror clipping keeps center verts on X = 0' ($mc -and [math]::Abs($mc.min[0]) -lt 0.0001 -and [math]::Abs($mc.max[0] - 1.3) -lt 0.001 -and $mc.evaluated.boundaryEdges -eq 0) "x $($mc.min[0])..$($mc.max[0]) open $($mc.evaluated.boundaryEdges)"
        M "export $dir\mirrored.fbx" | Out-Null
        $mi2 = M "import $dir\mirrored.fbx"
        Add-Result model 'export applies the mirror (re-import 10 faces, x -1.3..1.3)' ($mi2 -and $mi2.faces -eq 10 -and [math]::Abs($mi2.min[0] + 1.3) -lt 0.002) "faces=$($mi2.faces) min x=$($mi2.min[0])"
        M 'new' | Out-Null; M 'add --type cube' | Out-Null
        $sp = M 'modifier.subsurf --levels 2'
        $sa = M 'modifier.apply'
        Add-Result model 'subsurf preview 2 (96 faces) → apply' ($sp -and $sp.faces -eq 6 -and $sp.evaluated.faces -eq 96 -and $sa -and $sa.faces -eq 96 -and -not $sa.modifiers) "preview base $($sp.faces) eval $($sp.evaluated.faces), applied $($sa.faces)"

        # 비례 편집: 평면 가운데 점을 반경 0.5 로 들어 올림 → 가까운 점은 조금, 먼 점은 그대로
        M 'new' | Out-Null; M 'add --type plane --size 2 --cutsX 10 --cutsZ 10' | Out-Null; M 'mode --mode edit' | Out-Null
        M 'select.box --min -0.01,-1,-0.01 --max 0.01,1,0.01' | Out-Null
        M 'translate --delta 0,1,0 --proportional 0.5' | Out-Null
        $pv = M 'get --what verts --limit 500'
        $full = @($pv.items | Where-Object { $_[2] -gt 0.999 }).Count
        $part = @($pv.items | Where-Object { $_[2] -gt 0.001 -and $_[2] -lt 0.999 })
        $far = @($pv.items | Where-Object { [math]::Sqrt($_[1] * $_[1] + $_[3] * $_[3]) -gt 0.501 -and $_[2] -gt 0.0001 }).Count
        Add-Result model 'proportional editing (radius 0.5, smooth falloff)' ($full -eq 1 -and $part.Count -ge 8 -and $far -eq 0) "lifted 1: $full, partly: $($part.Count), moved outside radius: $far"

        # UV: Smart UV Project (정육면체 = 6 덩어리), Loop Cut · Subsurf 뒤에도 UV 가 남는다
        M 'new' | Out-Null; M 'add --type cube' | Out-Null; M 'mode --mode edit' | Out-Null
        $uv = M 'uv.smart'
        $lc = M 'loopcut --edge 0'
        $ss2 = M 'subsurf'
        Add-Result model 'Smart UV (6 charts) survives loop cut + subsurf' ($uv -and $uv.changed.charts -eq 6 -and $uv.uvFaces -eq 6 -and $lc.uvFaces -eq 10 -and $ss2.uvFaces -eq $ss2.faces) "charts=$($uv.changed.charts) uvFaces 6→$($lc.uvFaces) (of $($lc.faces))→$($ss2.uvFaces) (of $($ss2.faces))"

        # 머리카락: 다발 (닫힘, 바깥을 봄) · 카드
        M 'new' | Out-Null
        $hs = M 'hair.strand --points [[0,1.5,0.05],[0,1.62,0.2],[0,1.55,0.38]] --center 0,1.25,0 --width 0.1 --thickness 0.04 --sides 4'
        M 'mode --mode edit' | Out-Null
        $hi = M 'info'
        $hf = M 'get --what faces'
        $rn = M 'recalc_normals'
        Add-Result model 'hair.strand: closed clump (33 faces), normals already outward (recalc flips 0)' ($hs -and $hi.faces -eq 33 -and $hi.boundaryEdges -eq 0 -and $rn -and $rn.changed.flipped -eq 0) "faces=$($hi.faces) open=$($hi.boundaryEdges) flipped=$($rn.changed.flipped)"
        M 'mode --mode object' | Out-Null
        M 'hair.card --points [[0.1,1.5,0],[0.2,1.4,0.1],[0.25,1.2,0.12]] --center 0,1.25,0 --width 0.06 --name Card' | Out-Null
        M 'mode --mode edit' | Out-Null
        $cf = M 'get --what faces'
        $cOut = @($cf.items | Where-Object { $c = $_[3]; $n = $_[2]; ($c[0] * $n[0] + ($c[1] - 1.25) * $n[1] + $c[2] * $n[2]) -gt 0 }).Count
        Add-Result model 'hair.card faces away from the head' ($cf -and $cf.total -eq 8 -and $cOut -eq 8) "faces=$($cf.total) outward=$cOut"

        # 재질 색 + 툰 렌더 (외곽선) · UV 바둑판
        M 'select.all' | Out-Null
        $ms = M 'material.set --index 1 --name HairPink --color 0.95,0.45,0.6'
        $ml = M 'material.list'
        $tr = M "render --dir $dir\toon --views front,three-quarter --size 320 --shading toon --wire false"
        $ur = M "render --path $dir\uv.png --views persp --size 320 --shading uv --wire false"
        $mat1 = $ml.materials | Where-Object { $_.index -eq 1 }
        Add-Result model 'material color + toon / UV checker renders' ($mat1 -and $mat1.faces -eq 8 -and [math]::Abs($mat1.color[0] - 0.95) -lt 0.01 -and (Test-Path "$dir\toon\front.png") -and (Test-Path "$dir\uv.png")) "HairPink faces=$($mat1.faces) color=$($mat1.color -join ',')"

        # 렌더: 시점마다 PNG
        M 'new' | Out-Null; M 'add --type uvsphere --radius 0.5 --location 0,0.5,0 --smooth' | Out-Null; M 'add --type cube --size 0.6 --location 1,0.3,0' | Out-Null
        $rn = M "render --dir $dir\render --views front,right,top,persp --size 320 --shading solid"
        $pngs = @('front', 'right', 'top', 'persp' | ForEach-Object { Join-Path "$dir\render" "$_.png" } | Where-Object { (Test-Path $_) -and (Get-Item $_).Length -gt 2000 })
        Add-Result model 'render 4 views to PNG' ($rn -and $pngs.Count -eq 4) "files=$($pngs.Count)"
        # .nmodel 저장 → 새 문서 → 열기
        $sv = M "save $dir\scene.nmodel"
        M 'new' | Out-Null
        $op = M "open $dir\scene.nmodel"
        Add-Result model 'save / open .nmodel' ($sv -and $op -and $op.objects -eq 2 -and $op.faces -eq $rn.faces) "objects=$($op.objects) faces=$($op.faces)/$($rn.faces)"
        $bad = Invoke-Nova 'model loopcut'
        Add-Result model 'bad arguments → error message' ($bad -match 'need --edge') ($bad -replace '\s+', ' ')

        # ---- 4 단계 리깅: 예제 치비 → Humanoid 뼈대 · 자동 가중치 · 머리카락 사슬 → 포즈 렌더 → VRM → 엔진 캐릭터
        $root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
        M 'new' | Out-Null
        Invoke-NovaJson "model batch $root\docs\examples\model_chibi.txt" | Out-Null
        $hu = M 'rig.humanoid'
        $names = @($hu.bones | ForEach-Object { $_.name })
        $lua = $hu.bones | Where-Object { $_.name -eq 'LeftUpperArm' }
        Add-Result model 'rig.humanoid: 21 bones fitted by part names (left arm at -X)' ($hu -and $names.Count -eq 21 -and $hu.parts.ArmL -and $lua.head[0] -lt -0.05) "bones=$($names.Count) parts=$(@($hu.parts.PSObject.Properties).Count) LeftUpperArm x=$($lua.head[0])"
        $wt = M 'rig.weights'
        $arml = $wt.objects | Where-Object { $_.object -eq 'ArmL' }
        Add-Result model 'rig.weights: left arm weighted to LeftUpperArm' ($arml -and $arml.bones.LeftUpperArm -gt 0 -and -not $arml.bones.RightUpperArm) "ArmL bones: $(($arml.bones | ConvertTo-Json -Compress))"
        $ch = M 'rig.chain --objects Bang*,Back*,Ahoge* --parent Head'
        Add-Result model 'rig.chain: one spring chain per hair strand (14) + body colliders' ($ch -and @($ch.chains).Count -eq 14 -and $ch.colliders -ge 5) "chains=$(@($ch.chains).Count) colliders=$($ch.colliders)"
        $ck = M 'rig.check'
        Add-Result model 'rig.check: every vertex weighted, humanoid complete' ($ck -and $ck.ok) "problems=$(($ck.problems | ConvertTo-Json -Compress))"
        M "render --dir $dir\rig_rest --views front --size 256 --shading toon --wire false" | Out-Null
        M 'rig.pose --bone LeftUpperArm --rotation 0,0,-70' | Out-Null
        M "render --dir $dir\rig_posed --views front --size 256 --shading toon --wire false" | Out-Null
        $info = M 'info'
        M 'rig.pose --reset' | Out-Null
        $diff = -1
        if ((Test-Path "$dir\rig_rest\front.png") -and (Test-Path "$dir\rig_posed\front.png"))
        {
            Add-Type -AssemblyName System.Drawing
            $b1 = [System.Drawing.Bitmap]::FromFile("$dir\rig_rest\front.png"); $b2 = [System.Drawing.Bitmap]::FromFile("$dir\rig_posed\front.png")
            $diff = 0
            for ($y = 0; $y -lt 256; $y += 2) { for ($x = 0; $x -lt 256; $x += 2) { if ($b1.GetPixel($x, $y).ToArgb() -ne $b2.GetPixel($x, $y).ToArgb()) { $diff++ } } }
            $b1.Dispose(); $b2.Dispose()
        }
        Add-Result model 'rig.pose bends the skinned arm (render differs, posed flag)' ($info.armature.posed -and $diff -gt 100) "changed px=$diff posed=$($info.armature.posed)"
        # 치마: 원뿔 하나 → 둘레 8 사슬
        M 'new' | Out-Null
        M 'add --type uvsphere --radius 0.25 --location 0,1.2,0 --name Head' | Out-Null
        M 'add --type cylinder --vertices 12 --radius 0.15 --depth 0.5 --location 0,0.75,0 --name Body' | Out-Null
        M 'add --type cylinder --vertices 16 --radius 0.3 --radiusTop 0.16 --depth 0.3 --location 0,0.42,0 --name Skirt' | Out-Null
        M 'rig.humanoid --style adult' | Out-Null
        $sk = M 'rig.chain --objects Skirt --radial 8 --parent Hips'
        Add-Result model 'rig.chain --radial 8: skirt gets 8 chains around' ($sk -and @($sk.chains).Count -eq 8) "chains=$(@($sk.chains).Count)"
        # VRM 1.0 내보내기 → 엔진: Humanoid + Dynamic Bone + lilToon
        M 'new' | Out-Null
        Invoke-NovaJson "model batch $root\docs\examples\model_chibi.txt" | Out-Null
        # 표정: 입 + 셰이프 키 (눈 blink · happy, 입 aa ih ou ee oh happy)
        $fc = Invoke-NovaJson "model batch $root\docs\examples\model_chibi_face.txt"
        $sl = M 'shape.list --object Mouth'
        M "render --path $dir\face_rest.png --views front --size 256 --shading toon --wire false --bones false --zoom 2 --target 0,1.05,0" | Out-Null
        M 'shape.value --object Mouth --name aa --value 1' | Out-Null
        M "render --path $dir\face_aa.png --views front --size 256 --shading toon --wire false --bones false --zoom 2 --target 0,1.05,0" | Out-Null
        M 'shape.value --object Mouth --name aa --value 0' | Out-Null
        $fdiff = -1
        if ((Test-Path "$dir\face_rest.png") -and (Test-Path "$dir\face_aa.png"))
        {
            Add-Type -AssemblyName System.Drawing
            $b1 = [System.Drawing.Bitmap]::FromFile("$dir\face_rest.png"); $b2 = [System.Drawing.Bitmap]::FromFile("$dir\face_aa.png")
            $fdiff = 0
            for ($y = 0; $y -lt 256; $y += 2) { for ($x = 0; $x -lt 256; $x += 2) { if ($b1.GetPixel($x, $y).ToArgb() -ne $b2.GetPixel($x, $y).ToArgb()) { $fdiff++ } } }
            $b1.Dispose(); $b2.Dispose()
        }
        Add-Result model 'shape keys: 6 mouth shapes, shape.value aa opens the mouth in the render' ($fc -and @($sl.shapes).Count -eq 6 -and $fdiff -gt 20) "mouth shapes=$(@($sl.shapes).Count) changed px=$fdiff"
        Invoke-NovaJson "model batch $root\docs\examples\model_chibi_rig.txt" | Out-Null
        $vrmDir = Join-Path $Project 'Assets\NovaTestRig'
        Remove-Item $vrmDir -Recurse -Force -ErrorAction SilentlyContinue
        $ex = M "export --path $vrmDir\Chibi.vrm --title Chibi"
        $vrmJson = $null
        if (Test-Path "$vrmDir\Chibi.vrm")
        {
            $bytes = [IO.File]::ReadAllBytes("$vrmDir\Chibi.vrm")
            $len = [BitConverter]::ToUInt32($bytes, 12)
            $vrmJson = [Text.Encoding]::UTF8.GetString($bytes, 20, $len) | ConvertFrom-Json
        }
        $hb = if ($vrmJson) { @($vrmJson.extensions.VRMC_vrm.humanoid.humanBones.PSObject.Properties).Count } else { 0 }
        $sp = if ($vrmJson) { @($vrmJson.extensions.VRMC_springBone.springs).Count } else { 0 }
        Add-Result model 'export .vrm: skin + VRMC_vrm humanoid + spring bones + MToon' ($ex -and $hb -eq 21 -and $sp -eq 14 -and $vrmJson.skins.Count -eq 1 -and $vrmJson.materials[0].extensions.VRMC_materials_mtoon) "humanBones=$hb springs=$sp skins=$(@($vrmJson.skins).Count)"
        $pre = if ($vrmJson) { @($vrmJson.extensions.VRMC_vrm.expressions.preset.PSObject.Properties | ForEach-Object { $_.Name }) } else { @() }
        $mouthMesh = if ($vrmJson) { $vrmJson.meshes | Where-Object { $_.name -eq 'Mouth' } } else { $null }
        Add-Result model 'export .vrm: morph targets + VRMC_vrm expressions (happy, blink, aa …)' ($pre.Count -eq 9 -and ($pre -contains 'happy') -and ($pre -contains 'blinkLeft') -and @($mouthMesh.extras.targetNames).Count -eq 6 -and @($mouthMesh.primitives[0].targets).Count -eq 6) "presets=$($pre -join ',') mouth targets=$(@($mouthMesh.primitives[0].targets).Count)"
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create character --name RigChibi --model Assets\NovaTestRig\Chibi.vrm' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $rf = Join-Path $dir 'rig_engine.cs'
        'var g = GameObject.Find("RigChibi"); var d = g.GetComponent<DynamicBone>(); var a = g.GetComponent<Animator>(); var h = a.GetBonePosition(HumanBodyBones.Head); return d.chainCount + " " + d.colliderCount + " " + h.y.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $rf
        $er = Invoke-NovaJson "exec --file $rf"
        $ea = if ($er) { "$($er.result)" -split ' ' } else { @() }
        Add-Result model 'engine: rigged VRM → character (Humanoid head, 14 Dynamic Bone chains)' ($ea.Count -eq 3 -and $ea[0] -eq '14' -and [double]$ea[2] -gt 0.7 -and [double]$ea[2] -lt 1.1) "chains $($ea[0]) colliders $($ea[1]) head y $($ea[2])"
        $xf = Join-Path $dir 'rig_expr.cs'
        'var g = GameObject.Find("RigChibi"); var e = g.GetComponent<Expressions>(); e.SetWeight("happy", 1f); SkinnedMeshRenderer mouth = null; foreach (var r in g.GetComponentsInChildren<SkinnedMeshRenderer>()) if (r.gameObject.name.StartsWith("Mouth")) mouth = r; int i = mouth.sharedMesh.GetBlendShapeIndex("happy"); return e.count + " " + mouth.sharedMesh.blendShapeCount + " " + mouth.GetBlendShapeWeight(i).ToString("F0", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $xf
        $xr = Invoke-NovaJson "exec --file $xf"
        $xa = if ($xr) { "$($xr.result)" -split ' ' } else { @() }
        Add-Result model 'engine: VRM expressions → Expressions + BlendShape (happy = 100 on the mouth)' ($xa.Count -eq 3 -and $xa[0] -eq '9' -and $xa[1] -eq '6' -and $xa[2] -eq '100') "expressions $($xa[0]) mouth shapes $($xa[1]) happy weight $($xa[2])"
        # 원본이 바뀌면 다시 가져오기 (Unity 처럼): 같은 경로로 다시 내보낸 뒤 1 초 넘게 기다리면 로그 + 씬이 새 메시로
        M 'object.transform --object Body --scale 1.2,1,1.2' | Out-Null
        M "export --path $vrmDir\Chibi.vrm --title Chibi" | Out-Null
        Wait-Sec 2.5
        $rl = Invoke-Nova 'log --grep "changed on disk" -n 3'
        $still = Invoke-NovaJson "exec --file $rf"
        Add-Result model 'auto reimport: re-exported VRM reloads in the open scene' (($rl -match 'Chibi.vrm changed on disk') -and $still -and "$($still.result)" -match '^14 ') "log=$((($rl | Out-String) -replace '\s+', ' ').Trim()) after=$($still.result)"
        # 가중치 붓: 왼팔 가운데를 LeftLowerArm 100 % 로 → 그 그룹 점이 늘고 합은 1 (rig.check)
        M 'object.select --name ArmL' | Out-Null
        $g0 = M 'group.list'
        $c0 = ($g0.groups | Where-Object { $_.name -eq 'LeftLowerArm' }).verts
        $pt = M 'rig.paint --bone LeftLowerArm --center -0.22,0.66,0 --radius 0.25 --weight 1 --strength 1 --object ArmL'
        $g1 = M 'group.list'
        $c1 = ($g1.groups | Where-Object { $_.name -eq 'LeftLowerArm' }).verts
        $ck2 = M 'rig.check'
        Add-Result model 'rig.paint: brush paints LeftLowerArm on the arm, weights stay normalized' ($pt.changed.verts -gt 0 -and [int]$c1 -gt [int]$c0 -and $ck2.ok) "painted=$($pt.changed.verts) LeftLowerArm verts $c0 → $c1 check=$($ck2.ok)"
        # 스킨 FBX: 본 (LimbNode) + Skin / Cluster + BindPose → 엔진이 Humanoid 캐릭터로 (Unity · Blender 도 같은 형식)
        $fx = M "export --path $vrmDir\ChibiRig.fbx"
        Invoke-Nova 'create character --name FbxChibi --model Assets\NovaTestRig\ChibiRig.fbx' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $ff = Join-Path $dir 'rig_fbx.cs'
        'var g = GameObject.Find("FbxChibi"); var a = g.GetComponent<Animator>(); var h = a.GetBonePosition(HumanBodyBones.Head); var l = a.GetBonePosition(HumanBodyBones.LeftHand); return g.GetComponentsInChildren<SkinnedMeshRenderer>().Length + " " + h.y.ToString("F2", System.Globalization.CultureInfo.InvariantCulture) + " " + l.x.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $ff
        $fr = Invoke-NovaJson "exec --file $ff"
        $fa = if ($fr) { "$($fr.result)" -split ' ' } else { @() }
        Add-Result model 'export .fbx with armature → engine skinned Humanoid character' ($fx -and -not $fx.warning -and $fa.Count -eq 3 -and [int]$fa[0] -ge 10 -and [double]$fa[1] -gt 0.7 -and [double]$fa[1] -lt 1.1 -and [double]$fa[2] -lt 0) "skinned=$($fa[0]) head y=$($fa[1]) left hand x=$($fa[2])"
        # 애니메이션 (5 단계): rig.pose + anim.key → glTF 애니메이션 → 기본 컨트롤러의 Wave (엔진 리소스 Nova_Basic.glb 와 같은 예제)
        $ab = Invoke-NovaJson "model batch $root\docs\examples\anim_basic.txt"
        $al = M 'anim.list'
        M 'anim.select --name Walk' | Out-Null
        M 'anim.time --time 0' | Out-Null
        M "render --path $dir\walk0.png --views right --size 256 --wire false --bones false" | Out-Null
        M 'anim.time --time 0.5' | Out-Null
        M "render --path $dir\walk5.png --views right --size 256 --wire false --bones false" | Out-Null
        $wdiff = -1
        if ((Test-Path "$dir\walk0.png") -and (Test-Path "$dir\walk5.png"))
        {
            Add-Type -AssemblyName System.Drawing
            $b1 = [System.Drawing.Bitmap]::FromFile("$dir\walk0.png"); $b2 = [System.Drawing.Bitmap]::FromFile("$dir\walk5.png")
            $wdiff = 0
            for ($y = 0; $y -lt 256; $y += 2) { for ($x = 0; $x -lt 256; $x += 2) { if ($b1.GetPixel($x, $y).ToArgb() -ne $b2.GetPixel($x, $y).ToArgb()) { $wdiff++ } } }
            $b1.Dispose(); $b2.Dispose()
        }
        Add-Result model 'anim: Idle · Walk · Wave clips keyed from poses, anim.time samples (walk frames differ)' ($ab -and @($al.clips).Count -eq 3 -and ($al.clips | Where-Object { $_.name -eq 'Walk' }).keys -gt 40 -and $wdiff -gt 100) "clips=$(($al.clips | ForEach-Object { $_.name }) -join ',') changed px=$wdiff"
        $ag = M "export --path $vrmDir\Anim.glb"
        $agJson = $null
        if (Test-Path "$vrmDir\Anim.glb") { $bytes = [IO.File]::ReadAllBytes("$vrmDir\Anim.glb"); $len = [BitConverter]::ToUInt32($bytes, 12); $agJson = [Text.Encoding]::UTF8.GetString($bytes, 20, $len) | ConvertFrom-Json }
        $an = if ($agJson) { @($agJson.animations | ForEach-Object { $_.name }) } else { @() }
        Add-Result model 'export .glb: glTF animations (rotation + Hips translation) + VRMC_vrm humanoid for retargeting' ($an.Count -eq 3 -and $agJson.extensions.VRMC_vrm.humanoid -and @($agJson.animations[0].channels).Count -ge 21) "animations=$($an -join ',') channels(Idle)=$(@($agJson.animations[0].channels).Count)"
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create character --name WaveTest' | Out-Null
        Invoke-Nova 'play' | Out-Null
        Wait-Sec 0.5
        $wf = Join-Path $dir 'wave.cs'
        'var a = GameObject.Find("WaveTest").GetComponent<Animator>(); a.Play("Wave"); return "ok";' | Set-Content -Encoding utf8 $wf
        Invoke-Nova "exec --file $wf" | Out-Null
        Wait-Sec 1
        $hf = Join-Path $dir 'wave_read.cs'
        'var a = GameObject.Find("WaveTest").GetComponent<Animator>(); var c = System.Globalization.CultureInfo.InvariantCulture; return a.GetBonePosition(HumanBodyBones.RightHand).y.ToString("F2", c) + " " + a.GetBonePosition(HumanBodyBones.Head).y.ToString("F2", c);' | Set-Content -Encoding utf8 $hf
        $hr = Invoke-NovaJson "exec --file $hf"
        Invoke-Nova 'stop' | Out-Null
        $ha = if ($hr) { "$($hr.result)" -split ' ' } else { @() }
        Add-Result model 'engine: default controller Wave (Nova_Basic.glb) raises the right hand above the head' ($ha.Count -eq 2 -and [double]$ha[0] -gt [double]$ha[1]) "right hand y $($ha[0]) head y $($ha[1])"
        Remove-Item $vrmDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item "$vrmDir.meta" -Force -ErrorAction SilentlyContinue
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Stop-TestEditor $ed
        if ($null -ne $before) { Set-Content -Path $manifest -Value $before -NoNewline -Encoding utf8 }
    }
}

function Suite-Anim2D
{
    # 2D 애니메이터 패키지: 예제 batch (image.make 조각 → 뼈대 → 그림 → walk · idle) → 본 수 · 키 시각의 자세 · 첨부 바꾸기,
    # 키 안 찍은 자세 유지, Undo · batch 되돌리기, PNG 렌더 (투명 배경), 스프라이트 시트 + JSON, .skel2d 저장 · 열기
    Write-Host '[anim2d]'
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $before = if (Test-Path $manifest) { Get-Content $manifest -Raw } else { $null }
    $dir = Join-Path $Out 'anim2d'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\Anim2D'
    $ed = Start-TestEditor
    try
    {
        $a = Invoke-NovaJson 'package add com.nova.animation2d'
        Add-Result anim2d 'package loads (NovaAnimation2D.dll)' ($a -and $a.loaded) "loaded=$($a.loaded)"
        $h = Invoke-NovaJson 'anim2d help'
        Add-Result anim2d 'anim2d help lists ops' ($h -and $h.'bone.add' -and $h.'anim.key' -and $h.export) "ops=$(@($h.PSObject.Properties).Count)"
        function A2([string]$line) { Invoke-NovaJson "anim2d $line" }
        function Near([double]$a, [double]$b, [double]$eps = 0.05) { [math]::Abs($a - $b) -lt $eps }
        function BoneRot([string]$name) { $l = A2 'bone.list'; $b = @($l.bones | Where-Object { $_.name -eq $name })[0]; if ($b) { [double]$b.world.rotation } else { [double]::NaN } }
        function SlotCur([string]$name) { $l = A2 'slot.list'; @($l.slots | Where-Object { $_.name -eq $name })[0].current }

        A2 'new --name walker' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $b = A2 "batch $(Join-Path $Root 'docs\examples\anim2d_walker.txt')"
        $sw.Stop()
        $names = @($b.animations | ForEach-Object { $_.name }) -join ','
        Add-Result anim2d 'example batch: 12 bones, 14 slots, walk + idle' ($b -and $b.bones -eq 12 -and $b.slots -eq 14 -and $names -eq 'walk,idle') "bones=$($b.bones) slots=$($b.slots) anims=$names ($([int]$sw.Elapsed.TotalMilliseconds) ms)"
        $made = @(Get-ChildItem (Join-Path $assetDir 'Walker') -Filter *.png -ErrorAction SilentlyContinue).Count
        Add-Result anim2d 'image.make wrote 15 part PNGs' ($made -eq 15) "png=$made"

        # 키 시각의 자세 (smooth 곡선도 키를 지난다) + 사이 값
        A2 'anim.time --time 0.2' | Out-Null
        $r02 = BoneRot 'leg_front'
        A2 'anim.time --time 0.4' | Out-Null
        $r04 = BoneRot 'leg_front'
        A2 'anim.time --time 0.3' | Out-Null
        $r03 = BoneRot 'leg_front'
        Add-Result anim2d 'walk keys: leg_front world rotation -92 @0.2, -118 @0.4, between @0.3' ((Near $r02 -92) -and (Near $r04 -118) -and $r03 -lt -92 -and $r03 -gt -118) "0.2=$r02 0.3=$r03 0.4=$r04"
        A2 'anim.time --time 0.8' | Out-Null
        Add-Result anim2d 'walk loops (0.8 = 0)' (Near (BoneRot 'leg_front') -62) "0.8=$(BoneRot 'leg_front')"

        # 키 안 찍은 자세는 다른 연산 뒤에도 남고, 시각을 옮기면 키대로
        A2 'anim.time --time 0.1' | Out-Null
        A2 'pose --bone leg_front --rotation -30 --world' | Out-Null
        $i = A2 'info'
        $kept = BoneRot 'leg_front'
        A2 'anim.time --time 0.1' | Out-Null
        $back = BoneRot 'leg_front'
        Add-Result anim2d 'unkeyed pose stays (info, bone.list) until the time changes' ((Near $kept -30) -and $i.unkeyedPose -and -not (Near $back -30)) "kept=$kept flag=$($i.unkeyedPose) after anim.time=$back"

        # idle: 눈 깜빡임 = 첨부 바꾸기 (계단)
        A2 'anim.select --name idle' | Out-Null
        A2 'anim.time --time 1.65' | Out-Null
        $c1 = SlotCur 'eye'
        A2 'anim.time --time 1.8' | Out-Null
        $c2 = SlotCur 'eye'
        Add-Result anim2d 'idle blink: eye attachment closed @1.65, open @1.8' ($c1 -eq 'closed' -and $c2 -eq 'open') "1.65=$c1 1.8=$c2"

        # Undo · batch 되돌리기
        A2 'mode --mode setup' | Out-Null
        $u0 = (A2 'info').bones
        A2 'bone.add --name tail --parent hip --x -10 --y 0 --rotation 200 --length 30' | Out-Null
        $u1 = (A2 'info').bones
        $u2 = (A2 'undo').bones
        Add-Result anim2d 'bone.add → undo' ($u1 -eq $u0 + 1 -and $u2 -eq $u0) "$u0 → $u1 → $u2"
        $bf = Join-Path $dir 'bad.txt'
        "bone.add --name x1`nbone.add --name x2 --parent nobody" | Set-Content -Encoding utf8 $bf
        Invoke-Nova "anim2d batch $bf" | Out-Null
        $u3 = (A2 'info').bones
        Add-Result anim2d 'batch with a bad step rolls back' ($u3 -eq $u0) "bones=$u3"

        # PNG 렌더 (투명 배경: 모서리 알파 0, 가운데에 그림)
        Add-Type -AssemblyName System.Drawing
        $png = Join-Path $dir 'walk_02.png'
        $rr = A2 "render --path $png --anim walk --time 0.2 --size 256 --background none"
        $ok = $false; $detail = 'no file'
        if ($rr -and (Test-Path $png))
        {
            $bm = [System.Drawing.Bitmap]::FromFile($png)
            $corner = $bm.GetPixel(1, 1).A
            $opaque = 0
            for ($y = 0; $y -lt $bm.Height; $y += 4) { for ($x = 0; $x -lt $bm.Width; $x += 4) { if ($bm.GetPixel($x, $y).A -gt 200) { $opaque++ } } }
            $detail = "$($bm.Width)x$($bm.Height) corner alpha=$corner opaque samples=$opaque"
            $ok = $bm.Width -eq 256 -and $corner -eq 0 -and $opaque -gt 200
            $bm.Dispose()
        }
        Add-Result anim2d 'render PNG (transparent background, character drawn)' $ok $detail
        A2 "render --path $(Join-Path $dir 'setup.png') --size 512 --bones --grid" | Out-Null

        # 스프라이트 시트 + JSON (12 fps × 0.8 초 = 10 프레임)
        $sheet = Join-Path $dir 'walk_sheet.png'
        $ex = A2 "export --path $sheet --anim walk --fps 12"
        $sj = Join-Path $dir 'walk_sheet.json'
        $meta = if (Test-Path $sj) { Get-Content $sj -Raw | ConvertFrom-Json } else { $null }
        Add-Result anim2d 'export sprite sheet: 10 frames + JSON' ($ex -and $ex.frames -eq 10 -and $meta -and @($meta.frames).Count -eq 10 -and $meta.image -eq 'walk_sheet.png') "frames=$($ex.frames) json frames=$(@($meta.frames).Count) image=$($meta.image)"

        # 저장 · 열기
        $sk = 'Assets/Anim2D/walker.skel2d'
        A2 "save --path $sk" | Out-Null
        $k0 = (A2 'info').animations
        A2 'new' | Out-Null
        $o = A2 "open --path $sk"
        $same = $o -and $o.bones -eq 12 -and $o.slots -eq 14 -and (@($o.animations | ForEach-Object { "$($_.name):$($_.keys)" }) -join ',') -eq (@($k0 | ForEach-Object { "$($_.name):$($_.keys)" }) -join ',')
        Add-Result anim2d '.skel2d save → open (same bones, slots, keys)' $same "bones=$($o.bones) slots=$($o.slots) anims=$(@($o.animations | ForEach-Object { "$($_.name):$($_.keys)" }) -join ',')"

        # 씬: SpriteRenderer (엔진) + SpriteSkinnedRenderer (패키지) → SpriteBatch. 그림 색이 PNG 와 같은가 (sRGB 그림을 감마로 되돌림)
        Invoke-Nova 'create empty --name A2Walker --position 0,0,0' | Out-Null
        Invoke-Nova ('add-component A2Walker SpriteSkinnedRenderer --values "{\"skeleton\":\"' + $sk + '\",\"animation\":\"walk\",\"previewTime\":0.2}"') | Out-Null
        Invoke-Nova 'create empty --name A2Ball --position 0.7,1.5,-0.2' | Out-Null
        Invoke-Nova 'add-component A2Ball SpriteRenderer --values "{\"sprite\":\"builtin:Circle\",\"color\":[1,0,0,1],\"sortingOrder\":1}"' | Out-Null
        Invoke-Nova 'set A2Ball --scale 0.4,0.4,1' | Out-Null
        Invoke-Nova 'camera --position 0,1.2,-4 --target 0,1.2,0' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $ss = Join-Path $dir 'scene_sprites.png'
        Invoke-Nova "screenshot $ss --view scene" | Out-Null
        $red = 0; $shirt = 0; $hair = 0
        if (Test-Path $ss)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($ss)
            for ($y = 0; $y -lt $bm.Height; $y += 2) { for ($x = 0; $x -lt $bm.Width; $x += 2) {
                $c = $bm.GetPixel($x, $y)
                if ($c.R -gt 180 -and $c.R - $c.G -gt 110 -and $c.R - $c.B -gt 100) { $red++ }
                if ($c.R -lt 100 -and $c.G -gt 100 -and $c.G -lt 175 -and $c.B -gt 190) { $shirt++ }
                if ($c.R -gt 85 -and $c.R -lt 135 -and $c.G -gt 45 -and $c.G -lt 85 -and $c.B -lt 55) { $hair++ }
            } }
            $bm.Dispose()
        }
        Add-Result anim2d 'scene: Sprite Renderer (red circle) + Sprite Skinned Renderer drawn' ($red -gt 100 -and $shirt -gt 100) "red=$red shirt=$shirt"
        Add-Result anim2d 'scene: sprite colors match the PNG (sRGB images, brown hair not black)' ($hair -gt 30) "hair-colored samples=$hair"
        # Play: 시간이 흐르고 C# 로 애니메이션 바꾸기
        Wait-Compile
        $cf = Join-Path $dir 'ssr.cs'
        'var r = GameObject.Find("A2Walker").GetComponent<SpriteSkinnedRenderer>(); return r.animationName + " " + r.time.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $cf
        $pf = Join-Path $dir 'ssr_play.cs'
        'var r = GameObject.Find("A2Walker").GetComponent<SpriteSkinnedRenderer>(); var s = GameObject.Find("A2Ball").GetComponent<SpriteRenderer>(); s.flipX = true; s.sortingOrder = 3; return r.Play("idle") + " " + r.Play("nope") + " " + r.animationName + " " + s.flipX + " " + s.sortingOrder + " " + s.sprite.name;' | Set-Content -Encoding utf8 $pf
        Invoke-Nova 'play' | Out-Null
        Invoke-Nova 'wait 30' | Out-Null
        $t1 = Invoke-NovaJson "exec --file $cf"
        $p1 = Invoke-NovaJson "exec --file $pf"
        Invoke-Nova 'stop' | Out-Null
        $tv = if ($t1) { "$($t1.result)" -split ' ' } else { @() }
        Add-Result anim2d 'play: walk time advances (Start resets the preview time)' ($tv.Count -eq 2 -and $tv[0] -eq 'walk' -and [double]$tv[1] -gt 0 -and [double]$tv[1] -lt 0.8) "$($t1.result)"
        Add-Result anim2d 'C#: SpriteSkinnedRenderer.Play / SpriteRenderer flipX · sortingOrder · sprite' ("$($p1.result)" -eq 'True False idle True 3 Circle') "$($p1.result)"

        $w = Invoke-NovaJson 'anim2d window'
        Add-Result anim2d 'window opens (Window > 2D Animator)' ($w -and $w.bones -eq 12) "bones=$($w.bones)"
        Invoke-Nova 'wait 10' | Out-Null
        $sc = Join-Path $dir 'window.png'
        Invoke-Nova "screenshot $sc --view editor" | Out-Null
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Stop-TestEditor $ed
        if ($null -ne $before) { Set-Content -Path $manifest -Value $before -NoNewline -Encoding utf8 }
        Remove-Item $assetDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item "$assetDir.meta" -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Layers
{
    # Tags and Layers (Unity 번호: 4 Water, 5 UI) · 예전 파일 옮기기 (3→4, 4→5, 한 번만) · Layer Collision Matrix · Raycast layerMask · C#
    Write-Host '[layers]'
    $dir = Join-Path $Out 'layers'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $settings = Join-Path $Project 'ProjectSettings'
    $saved = @{}
    foreach ($f in 'TagManager.json', 'PhysicsSettings.json') { $p = Join-Path $settings $f; $saved[$f] = if (Test-Path $p) { Get-Content $p -Raw } else { $null } }
    $legacy = Join-Path $Project 'Assets\Scenes\LayerLegacy.scene'
    $ed = Start-TestEditor
    try
    {
        $l = Invoke-NovaJson 'layers'
        $names = @($l.layers | ForEach-Object { "$($_.layer):$($_.name)" }) -join ','
        Add-Result layers 'builtin layers use Unity numbers' ($names -like '0:Default,1:TransparentFX,2:Ignore Raycast,4:Water,5:UI*') $names
        Invoke-Nova 'layers --set 8 --name Enemy' | Out-Null
        $l2 = Invoke-NovaJson 'layers --add-tag Boss'
        $bad = Invoke-Nova 'layers --set 4 --name Lava'
        Add-Result layers 'user layer 8 named, tag added, builtin rename refused' ((@($l2.layers | Where-Object { $_.layer -eq 8 -and $_.name -eq 'Enemy' }).Count -eq 1) -and ($l2.tags -contains 'Boss') -and ($bad -match 'Builtin')) "8=$(@($l2.layers | Where-Object { $_.layer -eq 8 }).name) tags=$($l2.tags.Count) builtin: $bad"

        # 예전 형식 씬 (layerFormat 없음): Water 3, UI 4, excludeLayers 비트 3
        @'
{ "rootGameObjects": [
  { "name": "OldWater", "fileID": 7001, "active": true, "tag": "Untagged", "layer": 3, "static": false, "components": [
      { "type": "BoxCollider", "enabled": true, "excludeLayers": 8 } ], "children": [] },
  { "name": "OldUI", "fileID": 7002, "active": true, "tag": "Untagged", "layer": 4, "static": false, "components": [], "children": [] } ] }
'@ | Set-Content -Encoding utf8 $legacy
        Invoke-Nova 'scene open Assets/Scenes/LayerLegacy.scene --force' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $w = Invoke-NovaJson 'get OldWater'
        $u = Invoke-NovaJson 'get OldUI'
        $mask = @($w.components | Where-Object { $_.type -eq 'BoxCollider' })[0].excludeLayers
        Add-Result layers 'old scene migrated (Water 3→4, UI 4→5, collider mask bit 3→4)' ($w.layer -eq 4 -and $u.layer -eq 5 -and $mask -eq 16) "water=$($w.layer) ($($w.layerName)) ui=$($u.layer) mask=$mask"
        Invoke-Nova 'set OldUI --name OldUI2' | Out-Null
        Invoke-Nova 'undo' | Out-Null
        $u2 = Invoke-NovaJson 'get OldUI'
        Invoke-Nova 'scene save' | Out-Null
        Invoke-Nova 'scene open Assets/Scenes/LayerLegacy.scene --force' | Out-Null
        $w3 = Invoke-NovaJson 'get OldWater'
        $fileOk = (Get-Content $legacy -Raw) -match '"layerFormat"'
        Add-Result layers 'migrated once: undo and save + reopen keep 4 / 5' ($u2.layer -eq 5 -and $w3.layer -eq 4 -and $fileOk) "after undo ui=$($u2.layer), reopened water=$($w3.layer), file has layerFormat=$fileOk"

        # Layer Collision Matrix: Enemy 상자가 Default 바닥을 통과
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Floor --position 0,-0.5,0 --scale 10,1,10' | Out-Null
        Invoke-Nova 'create cube --name EnemyBox --position 0,2,0' | Out-Null
        Invoke-Nova 'add-component EnemyBox RigidBody' | Out-Null
        Invoke-Nova 'set EnemyBox --layer Enemy' | Out-Null
        Invoke-Nova 'create cube --name Ghost --position 3,1,0' | Out-Null
        Invoke-Nova 'set Ghost --layer "Ignore Raycast"' | Out-Null
        $ps = Invoke-NovaJson 'physics --ignore Enemy,Default'
        Add-Result layers 'physics --ignore Enemy,Default' (@($ps.ignoredPairs | Where-Object { ($_ -join '/') -eq 'Default/Enemy' }).Count -eq 1) "ignored=$($ps.ignoredPairs | ConvertTo-Json -Compress)"
        $yf = Join-Path $dir 'y.cs'
        'return GameObject.Find("EnemyBox").transform.position.y.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $yf
        function Wait-Sec([double]$sec) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $sec) { Invoke-Nova 'wait 10' | Out-Null } }
        Invoke-Nova 'play' | Out-Null; Wait-Sec 1.5
        $y1 = [double](Invoke-NovaJson "exec --file $yf").result
        Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        Invoke-Nova 'physics --collide Enemy,Default' | Out-Null
        Invoke-Nova 'play' | Out-Null; Wait-Sec 1.5
        $y2 = [double](Invoke-NovaJson "exec --file $yf").result
        # Raycast: layerMask · Ignore Raycast (기본 마스크에서 빠짐) · C# LayerMask · gameObject.layer · IgnoreLayerCollision (실행 중만)
        $cf = Join-Path $dir 'mask.cs'
        @'
var c = System.Globalization.CultureInfo.InvariantCulture;
RaycastHit h;
bool floorOnly = Physics.Raycast(new Vector3(0, 5, 0), Vector3.down, out h, 20f, LayerMask.GetMask("Default"));
string a = floorOnly ? h.collider.gameObject.name : "none";
bool enemy = Physics.Raycast(new Vector3(0, 5, 0), Vector3.down, out h, 20f, 1 << LayerMask.NameToLayer("Enemy"));
string b = enemy ? h.collider.gameObject.name : "none";
bool ghostDefault = Physics.Raycast(new Vector3(3, 5, 0), Vector3.down, out h, 20f);
string g = ghostDefault ? h.collider.gameObject.name : "none";
bool ghostAll = Physics.Raycast(new Vector3(3, 5, 0), Vector3.down, out h, 20f, Physics.AllLayers);
string g2 = ghostAll ? h.collider.gameObject.name : "none";
Physics.IgnoreLayerCollision(8, 4);
return a + " " + b + " " + g + " " + g2 + " " + GameObject.Find("EnemyBox").layer + " " + LayerMask.NameToLayer("Water") + " " + LayerMask.LayerToName(5) + " " + Physics.GetIgnoreLayerCollision(8, 4);
'@ | Set-Content -Encoding utf8 $cf
        $m = Invoke-NovaJson "exec --file $cf"
        Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        Add-Result layers 'matrix: Enemy falls through Default floor, then lands when re-enabled' ($y1 -lt -1 -and $y2 -gt 0.3 -and $y2 -lt 0.7) "ignored y=$y1, colliding y=$y2"
        Add-Result layers 'C#: Raycast layerMask, Ignore Raycast skipped by default, LayerMask, layer, IgnoreLayerCollision' ("$($m.result)" -eq 'Floor EnemyBox Floor Ghost 8 4 UI True') "$($m.result)"
        $after = Invoke-NovaJson 'physics'
        Add-Result layers 'IgnoreLayerCollision is runtime only (settings unchanged after Stop)' (@($after.ignoredPairs).Count -eq 0) "ignored after stop=$($after.ignoredPairs | ConvertTo-Json -Compress)"

        # Culling Mask: 카메라 (Enemy 를 안 그림) · 빛 (Enemy 를 안 비춤 → 어두움). Game 뷰 가운데 빨간 상자의 밝기
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 20,1,20' | Out-Null
        Invoke-Nova 'create cube --name RedBox --scale 3,3,3' | Out-Null
        Invoke-Nova 'set RedBox --layer Enemy' | Out-Null
        $cam = Invoke-NovaJson 'get "Main Camera"'
        $cp = $cam.worldPosition
        Invoke-Nova ('set RedBox --position {0},{1},{2}' -f $cp[0], $cp[1], ($cp[2] + 8)) | Out-Null
        Copy-Item (Join-Path $Project 'Assets\Materials\Red Plastic.mat') (Join-Path $Project 'Assets\LayerTestRed.mat') -Force
        Invoke-Nova 'set RedBox --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/LayerTestRed.mat\"]}"' | Out-Null
        function CenterRed([string]$name)
        {
            $p = Join-Path $dir $name
            Invoke-Nova 'wait 5' | Out-Null
            Invoke-Nova "screenshot $p --view game" | Out-Null
            if (-not (Test-Path $p)) { return @(0, 0) }
            $bm = [System.Drawing.Bitmap]::FromFile($p)
            $sum = 0.0; $n = 0; $red = 0
            for ($y = [int]($bm.Height * 0.4); $y -lt [int]($bm.Height * 0.6); $y += 3) { for ($x = [int]($bm.Width * 0.4); $x -lt [int]($bm.Width * 0.6); $x += 3) { $c = $bm.GetPixel($x, $y); $sum += $c.R; $n++; if ($c.R -gt $c.G + 40 -and $c.R -gt $c.B + 40) { $red++ } } }
            $bm.Dispose()
            return @([math]::Round($sum / [math]::Max(1, $n)), $red)
        }
        $lit = CenterRed 'cull_lit.png'
        Invoke-Nova 'set "Directional Light" --component Light --values "{\"cullingMaskBits\":4294967039}"' | Out-Null
        $dark = CenterRed 'cull_light.png'
        Invoke-Nova 'set "Directional Light" --component Light --values "{\"cullingMaskBits\":4294967295}"' | Out-Null
        Invoke-Nova 'set "Main Camera" --component Camera --values "{\"cullingMaskBits\":4294967039}"' | Out-Null
        $gone = CenterRed 'cull_camera.png'
        $cmf = Join-Path $dir 'cullmask.cs'
        'var l = GameObject.Find("Directional Light").GetComponent<Light>(); l.cullingMask = ~LayerMask.GetMask("Enemy"); return Camera.main.cullingMask + " " + l.cullingMask;' | Set-Content -Encoding utf8 $cmf
        $cm = Invoke-NovaJson "exec --file $cmf"
        Add-Result layers 'Light.cullingMask: Enemy box not lit by the sun (darker)' ($lit[1] -gt 50 -and $dark[0] -lt $lit[0] - 25) "lit R=$($lit[0]) red=$($lit[1]), light masked R=$($dark[0])"
        Add-Result layers 'Camera.cullingMask: Enemy box not drawn' ($lit[1] -gt 50 -and $gone[1] -lt 5) "red samples: drawn=$($lit[1]), camera masked=$($gone[1])"
        Add-Result layers 'C#: Camera.cullingMask / Light.cullingMask' ("$($cm.result)" -eq '-257 -257') "$($cm.result)"

        # Project Settings 창 (Tags and Layers · Physics 의 매트릭스) 캡처
        Invoke-Nova 'window project-settings --category Physics' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        Invoke-Nova "screenshot $(Join-Path $dir 'physics_settings.png') --view editor" | Out-Null
        Invoke-Nova 'window project-settings --category "Tags and Layers"' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        Invoke-Nova "screenshot $(Join-Path $dir 'tags_layers.png') --view editor" | Out-Null
        Invoke-Nova 'window project-settings --close' | Out-Null
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Stop-TestEditor $ed
        foreach ($f in $saved.Keys) { $p = Join-Path $settings $f; if ($null -ne $saved[$f]) { Set-Content -Path $p -Value $saved[$f] -NoNewline -Encoding utf8 } else { Remove-Item $p -Force -ErrorAction SilentlyContinue } }
        Remove-Item $legacy, "$legacy.meta" -Force -ErrorAction SilentlyContinue
        Remove-Item (Join-Path $Project 'Assets\LayerTestRed.mat'), (Join-Path $Project 'Assets\LayerTestRed.mat.meta') -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Sprites
{
    # 스프라이트 시트 자르기 (Sprite Mode = Multiple) · 프레임 애니메이션 (.spriteanim + Sprite Animator) · Sorting Layers
    Write-Host '[sprites]'
    $dir = Join-Path $Out 'sprites'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\SpriteTest'
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    $tm = Join-Path $Project 'ProjectSettings\TagManager.json'
    $tmBefore = if (Test-Path $tm) { Get-Content $tm -Raw } else { $null }
    # 시험 그림: 32 × 32 칸 5 개 (빨강 · 초록 · 파랑 · 노랑 + 빈 칸), 칸마다 가운데 원
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap 160, 32
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $cols = @([System.Drawing.Color]::FromArgb(255, 230, 40, 40), [System.Drawing.Color]::FromArgb(255, 40, 200, 60), [System.Drawing.Color]::FromArgb(255, 40, 80, 230), [System.Drawing.Color]::FromArgb(255, 240, 220, 40))
    for ($i = 0; $i -lt 4; $i++) { $b = New-Object System.Drawing.SolidBrush $cols[$i]; $g.FillEllipse($b, $i * 32 + 4, 4, 24, 24); $b.Dispose() }
    $g.Dispose()
    $sheet = Join-Path $assetDir 'hero.png'
    $bmp.Save($sheet, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $ed = Start-TestEditor
    try
    {
        $s = Invoke-NovaJson 'sprite-slice Assets/SpriteTest/hero.png --mode grid --cell 32,32 --pivot 0.5,0 --ppu 32 --filter point --animation 8'
        Add-Result sprites 'grid slice 32x32: 4 sprites (empty cell skipped) + .spriteanim' ($s -and $s.sprites -eq 4 -and $s.names[3] -eq 'hero_3' -and $s.animation -like '*hero.spriteanim') "sprites=$($s.sprites) names=$($s.names -join ',') anim=$($s.animation)"
        $a = Invoke-NovaJson 'sprite-slice Assets/SpriteTest/hero.png --mode auto --pivot 0.5,0 --ppu 32 --filter point'
        Add-Result sprites 'automatic slice: 4 islands' ($a -and $a.sprites -eq 4) "sprites=$($a.sprites)"
        $meta = Get-Content "$sheet.meta" -Raw | ConvertFrom-Json
        $r2 = @($meta.settings.sprites + $meta.sprites | Where-Object { $_ -and $_.name -eq 'hero_2' })[0]
        $rectOk = $r2 -and [math]::Abs([double]$r2.rect[0] - 68) -le 1 -and [math]::Abs([double]$r2.rect[1] - 4) -le 1 -and [math]::Abs([double]$r2.rect[2] - 24) -le 1
        Add-Result sprites '.meta: Sprite Mode Multiple, auto rect hero_2 ≈ 68,4,24,24 (bottom-left origin)' $rectOk "rect=$($r2.rect -join ',') meta keys=$(@($meta.PSObject.Properties.Name) -join ',')"
        Invoke-Nova 'sprite-slice Assets/SpriteTest/hero.png --mode grid --cell 32,32 --pivot 0.5,0 --ppu 32 --filter point' | Out-Null

        # 씬: 잘라 놓은 스프라이트 하나 (파랑 = hero_2) + Sorting Layer (Foreground 의 빨강이 Default 의 초록 (Order 10) 위)
        Invoke-Nova 'layers --add-sorting-layer Background' | Out-Null
        $sl = Invoke-NovaJson 'layers --add-sorting-layer Foreground'
        Add-Result sprites 'sorting layers: Default, Background, Foreground' ((@($sl.sortingLayers | ForEach-Object { $_.name }) -join ',') -eq 'Default,Background,Foreground') "$(@($sl.sortingLayers | ForEach-Object { $_.name }) -join ',')"
        Invoke-Nova 'layers --move-sorting-layer Background --by -1' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create empty --name Blue --position -2,1,0' | Out-Null
        Invoke-Nova 'add-component Blue SpriteRenderer --values "{\"sprite\":\"Assets/SpriteTest/hero.png#hero_2\"}"' | Out-Null
        $fg = (@($sl.sortingLayers | Where-Object { $_.name -eq 'Foreground' })[0]).id
        Invoke-Nova 'create empty --name RedFront --position 1,1,0' | Out-Null
        Invoke-Nova ('add-component RedFront SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[1,0,0,1],\"sortingLayerID\":' + $fg + '}"') | Out-Null
        Invoke-Nova 'create empty --name GreenBack --position 1.3,1,0' | Out-Null
        Invoke-Nova 'add-component GreenBack SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[0,1,0,1],\"sortingOrder\":10}"' | Out-Null
        Invoke-Nova 'camera --position 0,1,-5 --target 0,1,0' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $shot = Join-Path $dir 'sprites_scene.png'
        Invoke-Nova "screenshot $shot --view scene" | Out-Null
        $blue = 0; $overlapRed = $false; $info = ''
        if (Test-Path $shot)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($shot)
            for ($y = 0; $y -lt $bm.Height; $y += 2) { for ($x = 0; $x -lt $bm.Width; $x += 2) { $c = $bm.GetPixel($x, $y); if ($c.B -gt 180 -and $c.R -lt 110 -and $c.G -lt 150) { $blue++ } } }
            # 겹친 곳 (x = 1.15 근처): 화면 가운데에서 오른쪽
            $cx = [int]($bm.Width * 0.5); $cy = [int]($bm.Height * 0.5)
            $reds = 0; $greens = 0
            for ($x = $cx; $x -lt $bm.Width; $x += 2) { $c = $bm.GetPixel($x, $cy); if ($c.R -gt 180 -and $c.G -lt 90) { $reds++ } elseif ($c.G -gt 180 -and $c.R -lt 90) { $greens++ } }
            $info = "reds=$reds greens=$greens"
            $overlapRed = $reds -gt $greens   # 빨강 1 단위 + 겹침이 빨강이면 초록은 0.3 단위만 보인다
            $bm.Dispose()
        }
        Add-Result sprites 'sub-sprite drawn (blue hero_2, 1 unit at 32 PPU)' ($blue -gt 100) "blue samples=$blue"
        Add-Result sprites 'Foreground sorting layer draws over Default (even with Order 10)' $overlapRed $info

        # 프레임 애니메이션: Sprite Animator 가 Play 중 스프라이트를 바꾼다 + C#
        Invoke-Nova 'create empty --name Hero --position -4,1,0' | Out-Null
        Invoke-Nova 'add-component Hero SpriteRenderer --values "{\"sprite\":\"Assets/SpriteTest/hero.png#hero_0\"}"' | Out-Null
        Invoke-Nova 'add-component Hero SpriteAnimator --values "{\"clips\":[\"Assets/SpriteTest/hero.spriteanim\"]}"' | Out-Null
        $cf = Join-Path $dir 'anim.cs'
        'var a = GameObject.Find("Hero").GetComponent<SpriteAnimator>(); var r = GameObject.Find("Hero").GetComponent<SpriteRenderer>(); return a.currentClip + " " + a.isPlaying + " " + a.frame + " " + r.sprite.name;' | Set-Content -Encoding utf8 $cf
        $lf = Join-Path $dir 'layer.cs'
        'var r = GameObject.Find("GreenBack").GetComponent<SpriteRenderer>(); string before = r.sortingLayerName; r.sortingLayerName = "Background"; return before + " " + r.sortingLayerName + " " + GameObject.Find("Hero").GetComponent<SpriteAnimator>().Play("nope");' | Set-Content -Encoding utf8 $lf
        function Wait-Sec([double]$sec) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $sec) { Invoke-Nova 'wait 10' | Out-Null } }
        Invoke-Nova 'play' | Out-Null
        Wait-Sec 0.2
        $f1 = Invoke-NovaJson "exec --file $cf"
        Wait-Sec 0.3
        $f2 = Invoke-NovaJson "exec --file $cf"
        $l1 = Invoke-NovaJson "exec --file $lf"
        Invoke-Nova 'stop' | Out-Null
        $p1 = "$($f1.result)" -split ' '; $p2 = "$($f2.result)" -split ' '
        Add-Result sprites 'Sprite Animator plays hero.spriteanim (frame and sprite change)' ($p1[0] -eq 'hero' -and $p1[1] -eq 'True' -and $p1[3] -eq "hero_$($p1[2])" -and $p2[2] -ne $p1[2]) "$($f1.result) → $($f2.result)"
        Add-Result sprites 'C#: sortingLayerName get/set, Play(unknown) = false' ("$($l1.result)" -eq 'Default Background False') "$($l1.result)"
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Stop-TestEditor $ed
        if ($null -ne $tmBefore) { Set-Content -Path $tm -Value $tmBefore -NoNewline -Encoding utf8 } else { Remove-Item $tm -Force -ErrorAction SilentlyContinue }
        Remove-Item $assetDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item "$assetDir.meta" -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Physics2D
{
    # 2D 물리 (Box2D): 떨어져 바닥에 서기 · 스프라이트 윤곽 Polygon Collider 2D (오목한 L) · OnCollisionEnter2D (법선) · OnTriggerEnter2D ·
    # Physics2D.Raycast (layerMask) · Physics 2D Layer Collision Matrix · velocity
    Write-Host '[physics2d]'
    $dir = Join-Path $Out 'physics2d'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\Physics2DTest'
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    $settingsFiles = @('TagManager.json', 'Physics2DSettings.json')
    $saved = @{}
    foreach ($f in $settingsFiles) { $p = Join-Path $Project "ProjectSettings\$f"; $saved[$f] = if (Test-Path $p) { Get-Content $p -Raw } else { $null } }
    # 오목한 L 모양 그림 (100 PPU → 1 × 1 단위)
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap 100, 100
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::Transparent)
    $br = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 230, 120, 40))
    $g.FillRectangle($br, 0, 0, 40, 100); $g.FillRectangle($br, 0, 60, 100, 40); $g.Dispose(); $br.Dispose()
    $bmp.Save((Join-Path $assetDir 'L.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    @'
using NovaEngine;
public class Hit2DProbe : MonoBehaviour
{
    public static int collisions, triggers, exits;
    public static string normal = "";
    void OnCollisionEnter2D(Collision2D c) { collisions++; var n = c.GetContact(0).normal; normal = n.x.ToString("F1", System.Globalization.CultureInfo.InvariantCulture) + "," + n.y.ToString("F1", System.Globalization.CultureInfo.InvariantCulture); }
    void OnTriggerEnter2D(Collider2D other) { triggers++; }
    void OnTriggerExit2D(Collider2D other) { exits++; }
}
'@ | Set-Content -Encoding utf8 (Join-Path $assetDir 'Hit2DProbe.cs')
    $ed = Start-TestEditor
    try
    {
        Wait-Compile
        function Wait-Sec([double]$sec) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $sec) { Invoke-Nova 'wait 10' | Out-Null } }
        function Y([string]$name) { $f = Join-Path $dir 'y.cs'; ('return GameObject.Find("' + $name + '").transform.position.y.ToString("F3", System.Globalization.CultureInfo.InvariantCulture);') | Set-Content -Encoding utf8 $f; [double](Invoke-NovaJson "exec --file $f").result }
        Invoke-Nova 'layers --set 8 --name Enemy' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        # 바닥: 정사각형 스프라이트 10 × 1 (윗면 y = -0.5)
        Invoke-Nova 'create empty --name Ground --position 0,-1,0 --scale 10,1,1' | Out-Null
        Invoke-Nova 'add-component Ground SpriteRenderer --values "{\"sprite\":\"builtin:Square\"}"' | Out-Null
        Invoke-Nova 'add-component Ground BoxCollider2D' | Out-Null
        # 상자 (스프라이트 크기에 자동으로 맞는 Box Collider 2D) + 길목의 트리거
        Invoke-Nova 'create empty --name Crate --position 0,3,0' | Out-Null
        Invoke-Nova 'add-component Crate SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[0.9,0.6,0.2,1]}"' | Out-Null
        Invoke-Nova 'add-component Crate BoxCollider2D' | Out-Null
        Invoke-Nova 'add-component Crate Rigidbody2D' | Out-Null
        Invoke-Nova 'add-component Crate Hit2DProbe' | Out-Null
        Invoke-Nova 'create empty --name Zone --position 0,1.5,0' | Out-Null
        Invoke-Nova 'add-component Zone BoxCollider2D --values "{\"isTrigger\":true,\"size\":[2,0.5]}"' | Out-Null
        # 공 (원) · L (스프라이트 윤곽 Polygon) · Enemy 레이어 상자 (바닥과 안 부딪히게)
        Invoke-Nova 'create empty --name Ball --position 2.5,4,0' | Out-Null
        Invoke-Nova 'add-component Ball SpriteRenderer --values "{\"sprite\":\"builtin:Circle\"}"' | Out-Null
        Invoke-Nova 'add-component Ball CircleCollider2D' | Out-Null
        Invoke-Nova 'add-component Ball Rigidbody2D' | Out-Null
        Invoke-Nova 'create empty --name Ell --position -3,2,0' | Out-Null
        Invoke-Nova 'add-component Ell SpriteRenderer --values "{\"sprite\":\"Assets/Physics2DTest/L.png\"}"' | Out-Null
        Invoke-Nova 'add-component Ell PolygonCollider2D' | Out-Null
        Invoke-Nova 'add-component Ell Rigidbody2D --values "{\"freezeRotation\":true}"' | Out-Null
        Invoke-Nova 'create empty --name Ghost2D --position 4.5,2,0' | Out-Null
        Invoke-Nova 'set Ghost2D --layer Enemy' | Out-Null
        Invoke-Nova 'add-component Ghost2D SpriteRenderer --values "{\"sprite\":\"builtin:Square\"}"' | Out-Null
        Invoke-Nova 'add-component Ghost2D BoxCollider2D' | Out-Null
        Invoke-Nova 'add-component Ghost2D Rigidbody2D' | Out-Null
        $p2 = Invoke-NovaJson 'physics --2d --ignore Enemy,Default'
        Add-Result physics2d 'Physics 2D matrix: Enemy / Default ignored' (@($p2.ignoredPairs | Where-Object { ($_ -join '/') -eq 'Default/Enemy' }).Count -eq 1) "ignored=$($p2.ignoredPairs | ConvertTo-Json -Compress)"
        Invoke-Nova 'select Ell' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $ell = Invoke-NovaJson 'get Ell'
        $poly = @($ell.components | Where-Object { $_.type -eq 'PolygonCollider2D' })[0]
        $pts = if ($poly) { @($poly.paths[0]).Count } else { 0 }
        Add-Result physics2d 'Polygon Collider 2D traced the L sprite (concave outline)' ($poly -and @($poly.paths).Count -eq 1 -and $pts -ge 6 -and $pts -le 12) "paths=$(@($poly.paths).Count) points=$pts"
        Invoke-Nova 'camera --position 0,1,-9 --target 0,1,0' | Out-Null

        Invoke-Nova 'play' | Out-Null
        Wait-Sec 2.0
        $crate = Y 'Crate'; $ball = Y 'Ball'; $ellY = Y 'Ell'; $ghost = Y 'Ghost2D'
        Invoke-Nova "screenshot $(Join-Path $dir 'physics2d.png') --view scene" | Out-Null
        Invoke-Nova "screenshot $(Join-Path $dir 'physics2d_game.png') --view game" | Out-Null
        $cf = Join-Path $dir 'probe.cs'
        @'
var c = System.Globalization.CultureInfo.InvariantCulture;
var down = Physics2D.Raycast(new Vector2(-1.5f, 5f), Vector2.down);
var none = Physics2D.Raycast(new Vector2(-1.5f, 5f), Vector2.down, 20f, LayerMask.GetMask("Enemy"));
var rb = GameObject.Find("Ball").GetComponent<Rigidbody2D>();
return Hit2DProbe.collisions + " " + Hit2DProbe.triggers + " " + Hit2DProbe.exits + " " + Hit2DProbe.normal + " " + (down ? down.collider.gameObject.name + ":" + down.distance.ToString("F2", c) : "none") + " " + (none ? "hit" : "none") + " " + rb.bodyType + " " + Physics2D.gravity.y.ToString("F2", c);
'@ | Set-Content -Encoding utf8 $cf
        $pr = Invoke-NovaJson "exec --file $cf"
        $vf = Join-Path $dir 'vel.cs'
        'var rb = GameObject.Find("Crate").GetComponent<Rigidbody2D>(); rb.velocity = new Vector2(4, 0); return rb.position.x.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $vf
        $x0 = [double](Invoke-NovaJson "exec --file $vf").result
        Wait-Sec 0.3
        $xf = Join-Path $dir 'x.cs'
        'return GameObject.Find("Crate").transform.position.x.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $xf
        $x1 = [double](Invoke-NovaJson "exec --file $xf").result
        Invoke-Nova 'stop' | Out-Null
        Add-Result physics2d 'box falls and rests on the ground (y = 0)' ([math]::Abs($crate) -lt 0.05) "crate y=$crate"
        Add-Result physics2d 'circle and L-shaped polygon rest on the ground' ([math]::Abs($ball) -lt 0.06 -and $ellY -gt -0.1 -and $ellY -lt 0.1) "ball y=$ball, L y=$ellY"
        Add-Result physics2d 'Enemy layer box falls through (2D matrix)' ($ghost -lt -3) "ghost y=$ghost"
        $pp = "$($pr.result)" -split ' '
        Add-Result physics2d 'OnCollisionEnter2D once (normal up) + OnTriggerEnter2D / Exit2D' ($pp.Count -ge 4 -and [int]$pp[0] -eq 1 -and [int]$pp[1] -eq 1 -and [int]$pp[2] -eq 1 -and $pp[3] -eq '0.0,1.0') "$($pr.result)"
        Add-Result physics2d 'Physics2D.Raycast hits the ground, layerMask Enemy misses' ($pp.Count -ge 6 -and $pp[4] -eq 'Ground:5.50' -and $pp[5] -eq 'none') "$($pr.result)"
        Add-Result physics2d 'Rigidbody2D.velocity moves the body' ($x1 - $x0 -gt 0.5) "x $x0 → $x1"
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Stop-TestEditor $ed
        foreach ($f in $settingsFiles) { $p = Join-Path $Project "ProjectSettings\$f"; if ($null -ne $saved[$f]) { Set-Content -Path $p -Value $saved[$f] -NoNewline -Encoding utf8 } else { Remove-Item $p -Force -ErrorAction SilentlyContinue } }
        Remove-Item $assetDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item "$assetDir.meta" -Force -ErrorAction SilentlyContinue
    }
}

function Suite-ShaderGraph
{
    # Shader Graph: CLI 로 그래프 → 저장 (셰이더 만들기) → 재질 → 큐브 (Scene 뷰 색), 잘못된 연결 거절 · Undo · 오류, 모든 노드 종류가 컴파일되는지
    Write-Host '[shadergraph]'
    $dir = Join-Path $Out 'shadergraph'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\SGTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.Drawing
    $ed = Start-TestEditor
    try
    {
        function SG([string]$line) { Invoke-NovaJson "shadergraph $line" }
        function Wait-Sec([double]$sec) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $sec) { Invoke-Nova 'wait 10' | Out-Null } }
        # Scene 뷰 가운데 (상자) 색: 평균 R G B, 빨강 · 검정 · 흰색 칸 수
        function Center([string]$name)
        {
            $p = Join-Path $dir $name
            Invoke-Nova 'wait 5' | Out-Null
            Invoke-Nova "screenshot $p --view scene" | Out-Null
            if (-not (Test-Path $p)) { return [pscustomobject]@{ R = 0; G = 0; B = 0; Red = 0; Black = 0; White = 0; Green = 0; Blue = 0; N = 1 } }
            $bm = [System.Drawing.Bitmap]::FromFile($p)
            $r = 0.0; $g = 0.0; $b = 0.0; $n = 0; $red = 0; $black = 0; $white = 0; $green = 0; $blue = 0
            for ($y = [int]($bm.Height * 0.35); $y -lt [int]($bm.Height * 0.65); $y += 3) { for ($x = [int]($bm.Width * 0.35); $x -lt [int]($bm.Width * 0.65); $x += 3) {
                $c = $bm.GetPixel($x, $y); $r += $c.R; $g += $c.G; $b += $c.B; $n++
                if ($c.R -gt $c.G + 60 -and $c.R -gt $c.B + 60) { $red++ }
                if ($c.R -lt 80 -and $c.G -lt 80 -and $c.B -lt 80) { $black++ }
                if ($c.R -gt 180 -and $c.G -gt 180 -and $c.B -gt 180) { $white++ }
                if ($c.G -gt $c.R + 40 -and $c.G -gt $c.B + 20) { $green++ }
                if ($c.B -gt $c.R + 40 -and $c.B -gt $c.G + 10) { $blue++ } } }
            $bm.Dispose()
            $n = [math]::Max(1, $n)
            [pscustomobject]@{ R = [math]::Round($r / $n); G = [math]::Round($g / $n); B = [math]::Round($b / $n); Red = $red; Black = $black; White = $white; Green = $green; Blue = $blue; N = $n }
        }

        $h = SG 'help'
        Add-Result shadergraph 'shadergraph help lists ops' ($h -and $h.'node.add' -and $h.connect -and $h.save -and $h.'property.add') "ops=$(@($h.PSObject.Properties).Count)"
        $types = @(SG 'nodes' | ForEach-Object { $_ })   # (PowerShell 5: JSON 배열은 한 덩어리로 온다)
        $names = @($types | ForEach-Object { $_.type })
        Add-Result shadergraph 'node library: 60+ types (math, UV, procedural, texture, artistic)' ($types.Count -ge 60 -and $names -contains 'Voronoi' -and $names -contains 'Sample Texture 2D' -and $names -contains 'Fresnel Effect' -and $names -contains 'Tiling And Offset') "types=$($types.Count)"

        # ---- Lit: Base Color ← Tint (Color 속성, 빨강)
        $new = SG 'new Assets/SGTest/Tint.shadergraph --timeout 240'
        Add-Result shadergraph 'new graph (empty Lit) builds' ($new -and $new.shader -eq 'Shader Graphs/Tint' -and $new.error -eq '') "shader=$($new.shader) error=$($new.error)"
        $p = SG 'property.add --name Tint --type Color --value 1,0,0,1 --node --x -300 --y 0'
        $c = SG 'connect --from 1 --to Master --in "Base Color"'
        # 잘못된 연결: Texture2D → float, 고리
        SG 'property.add --name Tex --type Texture2D --node --x -300 --y 200' | Out-Null
        $bad1 = Invoke-Nova 'shadergraph connect --from 2 --to Master --in Metallic'
        SG 'node.add --type Add --x -100 --y 300' | Out-Null
        SG 'node.add --type Add --x 50 --y 300' | Out-Null
        SG 'connect --from 3 --to 4 --in A' | Out-Null
        $bad2 = Invoke-Nova 'shadergraph connect --from 4 --to 3 --in A'
        Add-Result shadergraph 'refuses Texture2D into a float input and loops' ($p.ref -eq '_Tint' -and $c -and ($bad1 -match 'Texture') -and ($bad2 -match 'loop')) "ref=$($p.ref) texture: $bad1 / loop: $bad2"
        $n0 = @((SG 'info').nodes).Count
        SG 'node.add --type Sine' | Out-Null
        $n1 = @((SG 'info').nodes).Count
        SG 'undo' | Out-Null
        $n2 = @((SG 'info').nodes).Count
        Add-Result shadergraph 'node.add → undo' ($n1 -eq $n0 + 1 -and $n2 -eq $n0) "$n0 → $n1 → $n2"
        SG 'node.delete --id 3' | Out-Null
        SG 'node.delete --id 4' | Out-Null
        $hl = SG 'compile --hlsl'
        Add-Result shadergraph 'compile: HLSL with the Tint property and PS_Graph' ($hl.ok -and $hl.hlsl -match 'gSG__Tint' -and $hl.hlsl -match 'PS_Graph' -and $hl.hlsl -match 'GraphBatchTech') "lines=$($hl.lines)"
        $s = SG 'save --timeout 240'
        $m = SG 'material'
        Add-Result shadergraph 'save builds the shader, material made next to the graph' ($s.built -and $m.material -eq 'Assets/SGTest/Tint.mat') "built=$($s.built) material=$($m.material)"

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 20,1,20' | Out-Null
        Invoke-Nova 'create cube --name Box --position 0,1.5,0 --scale 3,3,3' | Out-Null
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/Tint.mat\"]}"' | Out-Null
        Invoke-Nova 'camera --position 0,1.5,-6 --target 0,1.5,0' | Out-Null
        $lit = Center 'lit_tint.png'
        Add-Result shadergraph 'Lit graph: red Tint on a cube' ($lit.Red -gt $lit.N * 0.8) "rgb=$($lit.R),$($lit.G),$($lit.B) red=$($lit.Red)/$($lit.N)"

        # 그래프 기본값을 바꿔도 이미 만든 재질은 제 값 (Unity 와 같다), 새 재질은 새 기본값
        SG 'property.set --ref _Tint --value 0,0,1,1' | Out-Null
        SG 'save --timeout 240' | Out-Null
        SG 'material --mat Assets/SGTest/Tint2.mat' | Out-Null
        $j1 = Get-Content (Join-Path $assetDir 'Tint.mat') -Raw | ConvertFrom-Json
        $j2 = Get-Content (Join-Path $assetDir 'Tint2.mat') -Raw | ConvertFrom-Json
        $still = Center 'lit_keep.png'
        Add-Result shadergraph 'material keeps its value when the graph default changes; a new material gets the new default' ((@($j1.Properties._Tint | ForEach-Object { [int]$_ }) -join ',') -eq '1,0,0,1' -and (@($j2.Properties._Tint | ForEach-Object { [int]$_ }) -join ',') -eq '0,0,1,1' -and $still.Red -gt $still.N * 0.8 -and $j2.Shader -eq 'Shader Graphs/Tint') "Tint.mat=$($j1.Properties._Tint -join ',') Tint2.mat=$($j2.Properties._Tint -join ',') shader=$($j2.Shader) red=$($still.Red)"

        # 오류: 없는 속성을 가리키는 Property 노드 → 저장은 되지만 셰이더는 실패 (오류 문장), 고치면 다시
        SG 'node.add --type Property --options "{\"ref\":\"_Nope\"}"' | Out-Null
        $last = @((SG 'info').nodes)[-1].id
        SG "connect --from $last --to Master --in Alpha" | Out-Null
        $err = Invoke-Nova 'shadergraph save --timeout 240'
        SG 'undo' | Out-Null
        SG 'undo' | Out-Null
        $ok = SG 'save --timeout 240'
        Add-Result shadergraph 'bad graph: save reports the error, fixing it builds again' (($err -match '_Nope') -and $ok.built) "error: $(($err -split "`n")[0]) / fixed built=$($ok.built)"

        # ---- Unlit + Checkerboard (UV · 도우미 함수)
        SG 'new Assets/SGTest/Checker.shadergraph --material Unlit --timeout 240' | Out-Null
        $bf = Join-Path $dir 'checker.txt'
        @(
            '# Unlit checker (black / white, 4 x 4)',
            'node.add --type Checkerboard --x -300 --values ''{"Color A":[0,0,0],"Color B":[1,1,1],"Frequency":[4,4]}''',
            'connect --from 1 --to Master --in "Base Color"'
        ) | Set-Content -Encoding utf8 $bf
        $b = SG "batch $bf"
        SG 'save --timeout 240' | Out-Null
        SG 'material' | Out-Null
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/Checker.mat\"]}"' | Out-Null
        $ch = Center 'unlit_checker.png'
        Add-Result shadergraph 'Unlit graph: Checkerboard shows black and white squares' ($b -and $ch.Black -gt $ch.N * 0.2 -and $ch.White -gt $ch.N * 0.2) "black=$($ch.Black) white=$($ch.White) of $($ch.N)"

        # ---- Time: Fraction(Time) → Base Color (밝기가 시간에 따라 바뀐다)
        SG 'new Assets/SGTest/Pulse.shadergraph --material Unlit --timeout 240' | Out-Null
        SG 'node.add --type Time --x -500' | Out-Null
        SG 'node.add --type Fraction --x -250' | Out-Null
        SG 'connect --from 1 --out Time --to 2 --in In' | Out-Null
        SG 'connect --from 2 --to Master --in "Base Color"' | Out-Null
        SG 'save --timeout 240' | Out-Null
        SG 'material' | Out-Null
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/Pulse.mat\"]}"' | Out-Null
        $t1 = Center 'time_1.png'
        Wait-Sec 0.45
        $t2 = Center 'time_2.png'
        Add-Result shadergraph 'Time node animates (gray level changes over time)' ([math]::Abs($t1.R - $t2.R) -gt 15 -and [math]::Abs($t1.R - $t1.G) -le 3) "R $($t1.R) → $($t2.R)"

        # ---- 2단계: Alpha Clipping · Transparent · 엔진 Lit Alpha Clipping · 같은 이름 · 미리보기
        # 빨간 엔진 벽 앞의 상자: 구멍 / 투명한 곳으로 빨강이 보여야 한다 (프리패스가 구멍에 깊이를 남기면 배경색)
        Copy-Item (Join-Path $Project 'Assets\Materials\Red Plastic.mat') (Join-Path $assetDir 'Red.mat') -Force
        Invoke-Nova 'create cube --name RedWall --position 0,1.5,4 --scale 10,8,1' | Out-Null
        Invoke-Nova 'set RedWall --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/Red.mat\"]}"' | Out-Null
        $cf = Join-Path $dir 'clip.txt'
        @(
            'settings --alpha-clip true',
            'property.add --name Tint --type Color --value 0.1,0.8,0.2,1 --node --x -400 --y -100',
            'node.add --type Checkerboard --x -400 --y 100 --values ''{"Color A":[0,0,0],"Color B":[1,1,1],"Frequency":[2,2]}''',
            'connect --from 1 --to Master --in "Base Color"',
            'connect --from 2 --to Master --in Alpha'
        ) | Set-Content -Encoding utf8 $cf
        SG 'new Assets/SGTest/Clip.shadergraph --timeout 240' | Out-Null
        $cb = SG "batch $cf"
        $cs = SG 'save --timeout 240'
        SG 'material' | Out-Null
        $ch2 = SG 'compile --hlsl'
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/Clip.mat\"]}"' | Out-Null
        $cl = Center 'clip.png'
        Add-Result shadergraph 'Alpha Clipping: holes show the wall behind (prepass + shadow techniques), tiles stay' ($cs.built -and $ch2.hlsl -match 'GraphDepthBatchTech' -and $ch2.hlsl -match 'GraphShadowBatchTech' -and $cl.Red -gt $cl.N * 0.15 -and $cl.Green -gt $cl.N * 0.15) "built=$($cs.built) red(holes)=$($cl.Red) green(tiles)=$($cl.Green) of $($cl.N)"

        # 엔진 Lit 재질의 Alpha Clipping (Mesh Renderer): 같은 구멍이 프리패스에서도 (예전에는 배경색 구멍)
        $bmp = New-Object System.Drawing.Bitmap 64, 64
        for ($y = 0; $y -lt 64; $y++) { for ($x = 0; $x -lt 64; $x++) { $a = if ([math]::Floor($x / 16) % 2 -eq 0) { 255 } else { 0 }; $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($a, 255, 255, 255)) } }
        $bmp.Save((Join-Path $assetDir 'stripes.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        $sm = Get-Content (Join-Path $assetDir 'Red.mat') -Raw | ConvertFrom-Json
        $sm.BaseMapPath = 'Assets\SGTest\stripes.png'; $sm.AlphaClipping = 1; $sm.BaseColor = @(0.1, 0.2, 0.9, 1); $sm.ResourcePath = 'Assets\SGTest\Stripes.mat'
        $sm | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir 'Stripes.mat')
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/Stripes.mat\"]}"' | Out-Null
        $st = Center 'engine_clip.png'
        Add-Result shadergraph 'engine Lit Alpha Clipping on a Mesh Renderer: stripes show the wall behind (prepass fix)' ($st.Red -gt $st.N * 0.15 -and $st.Blue -gt $st.N * 0.15) "red(holes)=$($st.Red) blue(stripes)=$($st.Blue) of $($st.N)"

        # Transparent: 파란 유리 (Alpha 0.4) 너머로 빨간 벽 — 섞인 색
        SG 'new Assets/SGTest/Glass.shadergraph --timeout 240' | Out-Null
        SG 'settings --surface Transparent' | Out-Null
        SG 'property.add --name Tint --type Color --value 0.1,0.3,1,1 --node --x -400 --y -100' | Out-Null
        SG 'node.add --type Float --x -400 --y 100 --values "{\"X\":0.4}"' | Out-Null
        SG 'connect --from 1 --to Master --in "Base Color"' | Out-Null
        SG 'connect --from 2 --to Master --in Alpha' | Out-Null
        $gs = SG 'save --timeout 240'
        SG 'material' | Out-Null
        $gi = SG 'info'
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/Glass.mat\"]}"' | Out-Null
        $tr = Center 'transparent.png'
        Add-Result shadergraph 'Transparent: the red wall shows through the blue glass (blended, no prepass)' ($gs.built -and $gi.surface -eq 'Transparent' -and $tr.R -gt 70 -and $tr.B -gt 60) "rgb=$($tr.R),$($tr.G),$($tr.B)"

        # 같은 셰이더 이름 (다른 폴더의 같은 파일 이름): 저장이 이유를 알려 주고, 경로를 바꾸면 된다
        New-Item -ItemType Directory -Force (Join-Path $assetDir 'Sub') | Out-Null
        Copy-Item (Join-Path $assetDir 'Glass.shadergraph') (Join-Path $assetDir 'Sub\Glass.shadergraph') -Force
        SG 'open Assets/SGTest/Sub/Glass.shadergraph' | Out-Null
        $dup = Invoke-Nova 'shadergraph save --timeout 240'
        SG 'settings --path "Shader Graphs/Sub"' | Out-Null
        $dupOk = SG 'save --timeout 240'
        Add-Result shadergraph 'two graphs with one shader name: save explains, a Blackboard path fixes it' (($dup -match 'two shader graphs') -and $dupOk.built -and $dupOk.shader -eq 'Shader Graphs/Sub/Glass') "dup: $(($dup -split "`n" | Select-Object -Last 1)) / after path: $($dupOk.shader) built=$($dupOk.built)"

        # 미리보기 셰이더 (노드 + Main Preview): 창을 열면 백그라운드에서 만든다
        SG 'open Assets/SGTest/Clip.shadergraph' | Out-Null
        Invoke-Nova 'shadergraph window' | Out-Null
        Wait-Sec 1.5
        Invoke-Nova "screenshot $(Join-Path $dir 'preview.png') --view editor" | Out-Null
        $pv = @(Select-String -Path $EditorLog -Pattern '_Preview\.fx' | ForEach-Object { $_.Line })
        Add-Result shadergraph 'preview shader builds when the window opens' (@($pv | Where-Object { $_ -match 'hr=0x00000000|cache hit' }).Count -ge 1) "$(($pv | Select-Object -Last 1))"

        # ---- 3단계: Vertex 단계 · Custom Function · Sub Graph
        function Batch([string]$name, [string[]]$lines) { $f = Join-Path $dir "$name.txt"; $lines | Set-Content -Encoding utf8 $f; SG "batch $f" }
        # 화면 위 · 아래 절반의 초록 칸 수 (정점을 위로 옮기면 위가 많다)
        function GreenHalves([string]$name)
        {
            $p = Join-Path $dir $name
            Invoke-Nova 'wait 5' | Out-Null
            Invoke-Nova "screenshot $p --view scene" | Out-Null
            if (-not (Test-Path $p)) { return @(0, 0) }
            $bm = [System.Drawing.Bitmap]::FromFile($p)
            $up = 0; $down = 0
            for ($y = 0; $y -lt $bm.Height; $y += 4) { for ($x = [int]($bm.Width * 0.25); $x -lt [int]($bm.Width * 0.75); $x += 4) {
                $c = $bm.GetPixel($x, $y)
                if ($c.G -gt $c.R + 50 -and $c.G -gt $c.B + 50) { if ($y -lt $bm.Height / 2) { $up++ } else { $down++ } } } }
            $bm.Dispose()
            return @($up, $down)
        }
        Invoke-Nova 'window scene' | Out-Null   # 미리보기 검사가 Shader Graph 창을 Scene 탭 앞에 열었다 (Scene 뷰가 그려지지 않으면 캡처가 예전 그림)
        Invoke-Nova 'set RedWall --active false' | Out-Null
        SG 'new Assets/SGTest/Lift.shadergraph --material Unlit --timeout 240' | Out-Null
        $lb = Batch 'lift' @(
            'node.add --type Position --options ''{"space":"Object"}''',
            'node.add --type Add --values ''{"B":[0,0.35,0]}''',
            'connect --from 1 --to 2 --in A',
            'connect --from 2 --to Master --in "Vertex Position"',
            'node.add --type Color --options ''{"color":[0,1,0,1]}''',
            'connect --from 3 --to Master --in "Base Color"')
        $ls = SG 'save --timeout 240'
        SG 'material' | Out-Null
        $lh = SG 'compile --hlsl'
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/Lift.mat\"]}"' | Out-Null
        $gh = GreenHalves 'vertex_lift.png'
        Add-Result shadergraph 'Vertex stage: Vertex Position (Object + 0.35 up) lifts the cube; prepass + shadow move too (still drawn)' ($ls.built -and $lh.hlsl -match 'SG_EvaluateVertex' -and $gh[0] -gt 500 -and $gh[0] -gt $gh[1] * 2) "built=$($ls.built) green upper=$($gh[0]) lower=$($gh[1])"

        # 시간으로 움직이는 정점 (예제 물결): 프리패스와 본 패스가 같은 시각을 써야 한다 (다르면 EQUAL 깊이가 어긋나 검게 빈다)
        SG 'new Assets/SGTest/Wave.shadergraph --timeout 240' | Out-Null
        SG "batch $(Join-Path $Root 'docs\examples\shadergraph_wave.txt')" | Out-Null
        $ws = SG 'save --timeout 240'
        SG 'material' | Out-Null
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/Wave.mat\"]}"' | Out-Null
        $wv = Center 'vertex_wave.png'
        Add-Result shadergraph 'Vertex stage + Time (wave example): prepass and main pass use one time per frame (not black)' ($ws.built -and $wv.Black -lt $wv.N * 0.1 -and $wv.B -gt 80) "built=$($ws.built) black=$($wv.Black)/$($wv.N) rgb=$($wv.R),$($wv.G),$($wv.B)"

        SG 'new Assets/SGTest/CF.shadergraph --material Unlit --timeout 240' | Out-Null
        Batch 'cf' @(
            'node.add --type "Custom Function" --values ''{"A":[0,0,1]}'' --options ''{"name":"Flip","body":"Out = A.zyx;"}''',
            'connect --from 1 --out Out --to Master --in "Base Color"') | Out-Null
        $cfs = SG 'save --timeout 240'
        SG 'material' | Out-Null
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/CF.mat\"]}"' | Out-Null
        $cfc = Center 'custom_string.png'
        'void Swap_float(float3 A, out float3 Out) { Out = A.yxz; }' | Set-Content -Encoding ascii (Join-Path $assetDir 'swap.hlsl')
        SG 'new Assets/SGTest/CFFile.shadergraph --material Unlit --timeout 240' | Out-Null
        Batch 'cffile' @(
            'node.add --type "Custom Function" --values ''{"A":[0,1,0]}'' --options ''{"name":"Swap","mode":"File","file":"Assets/SGTest/swap.hlsl"}''',
            'connect --from 1 --out Out --to Master --in "Base Color"') | Out-Null
        $cff = SG 'save --timeout 240'
        SG 'material' | Out-Null
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/CFFile.mat\"]}"' | Out-Null
        $cfd = Center 'custom_file.png'
        Add-Result shadergraph 'Custom Function: String body (A.zyx) and File (.hlsl Swap_float) both give red' ($cfs.built -and $cff.built -and $cfc.Red -gt $cfc.N * 0.8 -and $cfd.Red -gt $cfd.N * 0.8) "string red=$($cfc.Red) file red=$($cfd.Red) of $($cfc.N)"
        $badOpt = Invoke-Nova 'shadergraph node.add --type Color --options notjson'
        Add-Result shadergraph 'node.add refuses --options that is not a JSON object' ($badOpt -match 'JSON object') "$(($badOpt -split "`n")[0])"

        SG 'new Assets/SGTest/Pass.shadersubgraph --timeout 240' | Out-Null
        Batch 'sub' @('property.add --name Col --type Color --value 0,0,1,1 --node', 'connect --from 1 --to Master --in Out') | Out-Null
        $ss = SG 'save --timeout 240'
        SG 'new Assets/SGTest/UseSub.shadergraph --material Unlit --timeout 240' | Out-Null
        Batch 'usesub' @(
            'node.add --type "Sub Graph" --options ''{"asset":"Assets/SGTest/Pass.shadersubgraph"}''',
            'node.add --type Color --options ''{"color":[0,1,0,1]}''',
            'connect --from 2 --to 1 --in Col',
            'connect --from 1 --out Out --to Master --in "Base Color"') | Out-Null
        $us = SG 'save --timeout 240'
        SG 'material' | Out-Null
        Invoke-Nova 'set Box --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTest/UseSub.mat\"]}"' | Out-Null
        $s1 = Center 'subgraph_1.png'
        # Sub Graph 를 고치면 (Out = 1 - Col) 쓰는 그래프가 다시 만들어진다 → 초록 → 자홍
        SG 'open Assets/SGTest/Pass.shadersubgraph' | Out-Null
        Batch 'sub2' @('node.add --type "One Minus"', 'connect --from 1 --to 2 --in In', 'connect --from 2 --to Master --in Out') | Out-Null
        $ss2 = SG 'save --timeout 240'
        $s2 = Center 'subgraph_2.png'
        Add-Result shadergraph 'Sub Graph: input → output works; saving the Sub Graph rebuilds the graph that uses it' ($ss.built -and $us.built -and $ss2.built -and $s1.Green -gt $s1.N * 0.8 -and $s2.R -gt 150 -and $s2.B -gt 150 -and $s2.G -lt 90) "before green=$($s1.Green)/$($s1.N), after rgb=$($s2.R),$($s2.G),$($s2.B)"
        Invoke-Nova 'set RedWall --active true' | Out-Null

        # ---- 모든 노드 종류: 하나씩 + Add 사슬로 Base Color 에 → 한 셰이더로 컴파일
        SG 'new Assets/SGTest/AllNodes.shadergraph --material Lit --timeout 240' | Out-Null
        $lines = New-Object System.Collections.Generic.List[string]
        $id = 0; $prev = 0; $count = 0
        foreach ($t in $types)
        {
            if ($t.type -eq 'Property' -or $t.type -eq 'Sub Graph') { continue }
            $id++; $node = $id
            $lines.Add("node.add --type `"$($t.type)`" --x $(-200 * ($count % 8)) --y $(150 * [math]::Floor($count / 8))")
            if ($prev -gt 0)
            {
                $id++
                $lines.Add("node.add --type Add --x 400 --y $(150 * $count)")
                $lines.Add("connect --from $prev --to $id --in A")
                $lines.Add("connect --from $node --to $id --in B")
                $prev = $id
            }
            else { $prev = $node }
            $count++
        }
        $lines.Add("connect --from $prev --to Master --in `"Base Color`"")
        $af = Join-Path $dir 'allnodes.txt'
        $lines | Set-Content -Encoding utf8 $af
        $ab = SG "batch $af"
        $as = SG 'save --timeout 300'
        Add-Result shadergraph "every node type compiles in one shader ($count types)" ($ab -and $as.built) "steps=$(@($ab.steps).Count) built=$($as.built) error=$($as.error)"

        # 창 캡처 (Showcase): Checker 그래프
        SG 'open Assets/SGTest/Checker.shadergraph' | Out-Null
        Invoke-Nova 'shadergraph window' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null
        Invoke-Nova "screenshot $(Join-Path $dir 'window.png') --view editor" | Out-Null
        Add-Result shadergraph 'Shader Graph window opens (editor capture)' (Test-Path (Join-Path $dir 'window.png')) ''
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Decal
{
    # Decal Projector: 바닥에 투영 (상자 안만), Opacity, Base Map 알파, Shader Graph 데칼, 저장 → 다시 열기
    Write-Host '[decal]'
    $dir = Join-Path $Out 'decal'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\DecalTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    # 빨간 Lit 재질 · 줄무늬 알파 재질
    Copy-Item (Join-Path $Project 'Assets\Materials\Red Plastic.mat') (Join-Path $assetDir 'RedDecal.mat') -Force
    $bmp = New-Object System.Drawing.Bitmap 64, 64
    for ($y = 0; $y -lt 64; $y++) { for ($x = 0; $x -lt 64; $x++) { $a = if ([math]::Floor($x / 16) % 2 -eq 0) { 255 } else { 0 }; $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($a, 255, 255, 255)) } }
    $bmp.Save((Join-Path $assetDir 'stripes.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $sm = Get-Content (Join-Path $assetDir 'RedDecal.mat') -Raw | ConvertFrom-Json
    $sm.BaseMapPath = 'Assets\DecalTest\stripes.png'; $sm.ResourcePath = 'Assets\DecalTest\Stripes.mat'
    $sm | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir 'Stripes.mat')
    $ed = Start-TestEditor
    try
    {
        function SetDecal([string]$values) { Invoke-Nova ('set Decal --component DecalProjector --values "' + $values.Replace('"', '\"') + '"') }
        # 화면의 가운데 (데칼 안) 와 바깥 띠의 빨강 · 검정 · 흰색 비율
        function Shot([string]$name)
        {
            $p = Join-Path $dir $name
            Invoke-Nova 'wait 5' | Out-Null
            Invoke-Nova "screenshot $p --view scene" | Out-Null
            if (-not (Test-Path $p)) { return $null }
            $bm = [System.Drawing.Bitmap]::FromFile($p)
            $in = 0; $inRed = 0; $inBlack = 0; $inWhite = 0; $outN = 0; $outRed = 0
            for ($y = 0; $y -lt $bm.Height; $y += 3) { for ($x = 0; $x -lt $bm.Width; $x += 3) {
                $c = $bm.GetPixel($x, $y)
                $red = $c.R -gt $c.G + 60 -and $c.R -gt $c.B + 60
                $fx = [math]::Abs($x / $bm.Width - 0.5); $fy = [math]::Abs($y / $bm.Height - 0.5)
                if ($fx -lt 0.08 -and $fy -lt 0.08) { $in++; if ($red) { $inRed++ }; if ($c.R -lt 110 -and $c.G -lt 110 -and $c.B -lt 110) { $inBlack++ }; if ($c.R -gt 170 -and $c.G -gt 170 -and $c.B -gt 170) { $inWhite++ } }
                elseif ($fx -gt 0.35 -or $fy -gt 0.35) { $outN++; if ($red) { $outRed++ } } } }
            $bm.Dispose()
            [pscustomobject]@{ In = [math]::Max(1, $in); InRed = $inRed; InBlack = $inBlack; InWhite = $inWhite; Out = [math]::Max(1, $outN); OutRed = $outRed }
        }

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 30,1,30' | Out-Null
        # 아래 (-Y) 로 투영 (+Z 를 X 축 90 도로 돌림), 2 × 2 상자
        Invoke-Nova 'create empty --name Decal --position 0,0.5,0 --rotation 90,0,0' | Out-Null
        Invoke-Nova 'add-component Decal DecalProjector' | Out-Null
        SetDecal '{"material":"Assets/DecalTest/RedDecal.mat","size":[2,2,2],"pivot":[0,0,1]}' | Out-Null
        Invoke-Nova 'camera --position 0,3,-0.01 --target 0,0,0' | Out-Null
        $s1 = Shot 'decal_red.png'
        Add-Result decal 'Decal Projector paints the floor inside its box only (red center, grey around)' ($s1 -and $s1.InRed -gt $s1.In * 0.9 -and $s1.OutRed -lt $s1.Out * 0.05) "inside red=$($s1.InRed)/$($s1.In) outside red=$($s1.OutRed)/$($s1.Out)"

        SetDecal '{"fadeFactor":0}' | Out-Null
        $s2 = Shot 'decal_opacity0.png'
        SetDecal '{"fadeFactor":1}' | Out-Null
        Add-Result decal 'Opacity 0 hides the decal' ($s2 -and $s2.InRed -lt $s2.In * 0.05) "inside red=$($s2.InRed)/$($s2.In)"

        SetDecal '{"material":"Assets/DecalTest/Stripes.mat"}' | Out-Null
        $s3 = Shot 'decal_stripes.png'
        Add-Result decal 'Base Map alpha = coverage (stripes)' ($s3 -and $s3.InRed -gt $s3.In * 0.25 -and $s3.InRed -lt $s3.In * 0.75) "inside red=$($s3.InRed)/$($s3.In)"

        # Shader Graph 데칼: Base Color = Checkerboard (투영 UV)
        Invoke-Nova 'shadergraph new Assets/DecalTest/CheckerDecal.shadergraph --material Decal --timeout 240' | Out-Null
        $bf = Join-Path $dir 'checker.txt'
        @('node.add --type Checkerboard --values ''{"Color A":[0,0,0],"Color B":[1,1,1],"Frequency":[2,2]}''', 'connect --from 1 --to Master --in "Base Color"') | Set-Content -Encoding utf8 $bf
        Invoke-Nova "shadergraph batch $bf" | Out-Null
        $gs = Invoke-NovaJson 'shadergraph save --timeout 240'
        $gm = Invoke-NovaJson 'shadergraph material'
        $hl = Invoke-NovaJson 'shadergraph compile --hlsl'
        SetDecal '{"material":"Assets/DecalTest/CheckerDecal.mat"}' | Out-Null
        $s4 = Shot 'decal_graph.png'
        Add-Result decal 'Shader Graph decal (Material = Decal): checkerboard on the floor' ($gs.built -and $hl.hlsl -match 'GraphDecalTech' -and $s4 -and $s4.InBlack -gt $s4.In * 0.15 -and $s4.InWhite -gt $s4.In * 0.15) "built=$($gs.built) black=$($s4.InBlack) white=$($s4.InWhite) of $($s4.In)"

        # 저장 → 다시 열기: 설정 그대로
        SetDecal '{"material":"Assets/DecalTest/RedDecal.mat","uvScale":[2,3],"angleFade":[10,80]}' | Out-Null
        Invoke-Nova 'scene save --as Assets/DecalTest/DecalScene.scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'scene open Assets/DecalTest/DecalScene.scene --force' | Out-Null
        $g = Invoke-NovaJson 'get Decal'
        $c = @($g.components | Where-Object { $_.type -eq 'DecalProjector' })[0]
        Add-Result decal 'saved and reopened: material, size, UV scale, angle fade kept' ($c -and $c.material -eq 'Assets/DecalTest/RedDecal.mat' -and (@($c.size | ForEach-Object { [double]$_ }) -join ',') -eq '2,2,2' -and (@($c.uvScale | ForEach-Object { [double]$_ }) -join ',') -eq '2,3' -and (@($c.angleFade | ForEach-Object { [double]$_ }) -join ',') -eq '10,80') "material=$($c.material) size=$($c.size -join ',') uv=$($c.uvScale -join ',') angle=$($c.angleFade -join ',')"
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-ReflectionProbe
{
    # Reflection Probe: 거울 구가 카메라 뒤의 빨간 벽을 비추는지 (하늘 대신), 굽기 · 다시 열기 · 상자 밖 · Intensity · Custom · Box Projection · 실시간
    Write-Host '[reflectionprobe]'
    $dir = Join-Path $Out 'reflectionprobe'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\ProbeTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $base = Get-Content (Join-Path $Project 'Assets\Materials\Red Plastic.mat') -Raw | ConvertFrom-Json
    function MakeMat([string]$name, $color, [double]$metallic, [double]$smooth, $emission)
    {
        $m = $base.PSObject.Copy(); $m.BaseColor = $color; $m.Metallic = $metallic; $m.Smoothness = $smooth; $m.ResourcePath = "Assets\ProbeTest\$name.mat"
        if ($emission) { $m.Emission = $true; $m.EmissionColor = $emission }
        $m | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir "$name.mat")
    }
    MakeMat 'Chrome' @(1, 1, 1, 1) 1.0 1.0 $null
    MakeMat 'RedWall' @(1, 0, 0, 1) 0.0 0.25 @(1, 0, 0)
    MakeMat 'GreenWall' @(0, 1, 0, 1) 0.0 0.25 @(0, 1, 0)
    $ed = Start-TestEditor
    try
    {
        function SetProbe([string]$values) { Invoke-Nova ('set Probe --component ReflectionProbe --values "' + $values.Replace('"', '\"') + '"') }
        function SetWall([string]$mat) { Invoke-Nova ('set Wall --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/ProbeTest/' + $mat + '.mat\"]}"') | Out-Null }
        function View { Invoke-Nova 'camera --position 0,1,-3 --target 0,1,0' | Out-Null }
        # 구 가운데 (카메라 뒤를 비추는 곳) 의 평균 색 + 구 상자 안의 빨강 · 초록 점 수
        function Shot([string]$name)
        {
            $p = Join-Path $dir $name
            Invoke-Nova 'wait 5' | Out-Null
            Invoke-Nova "screenshot $p --view scene" | Out-Null
            if (-not (Test-Path $p)) { return $null }
            $bm = [System.Drawing.Bitmap]::FromFile($p)
            $n = 0; $r = 0; $g = 0; $b = 0; $red = 0; $green = 0
            for ($y = [int]($bm.Height * 0.35); $y -lt [int]($bm.Height * 0.65); $y += 2) { for ($x = [int]($bm.Width * 0.42); $x -lt [int]($bm.Width * 0.58); $x += 2) {
                $c = $bm.GetPixel($x, $y)
                if ($c.R -gt $c.G + 80 -and $c.R -gt $c.B + 80) { $red++ }
                if ($c.G -gt $c.R + 80 -and $c.G -gt $c.B + 60) { $green++ }
                $fx = $x / $bm.Width; $fy = $y / $bm.Height
                if ($fx -gt 0.48 -and $fx -lt 0.52 -and $fy -gt 0.46 -and $fy -lt 0.50) { $n++; $r += $c.R; $g += $c.G; $b += $c.B } } }
            $bm.Dispose()
            $n = [math]::Max(1, $n)
            [pscustomobject]@{ R = [int]($r / $n); G = [int]($g / $n); B = [int]($b / $n); Red = $red; Green = $green }
        }
        function IsRed($s) { $s -and $s.R -gt 150 -and $s.R -gt $s.G + 80 -and $s.R -gt $s.B + 80 }
        function IsGreen($s) { $s -and $s.G -gt 150 -and $s.G -gt $s.R + 80 }
        function Txt($s) { if ($s) { "rgb=$($s.R),$($s.G),$($s.B) red=$($s.Red) green=$($s.Green)" } else { 'no capture' } }

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 30,1,30' | Out-Null
        Invoke-Nova 'create sphere --name Mirror --position 0,1,0' | Out-Null
        Invoke-Nova 'set Mirror --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/ProbeTest/Chrome.mat\"]}"' | Out-Null
        Invoke-Nova 'create cube --name Wall --position 0,1.5,-6 --scale 20,8,1' | Out-Null
        SetWall 'RedWall'
        View
        $s0 = Shot 'probe_none.png'
        Add-Result reflectionprobe 'without a probe the mirror sphere reflects the sky (not the red wall behind the camera)' ($s0 -and -not (IsRed $s0)) (Txt $s0)

        Invoke-Nova 'create empty --name Probe --position 0,1,0' | Out-Null
        Invoke-Nova 'add-component Probe ReflectionProbe' | Out-Null
        SetProbe '{"size":[30,20,30]}' | Out-Null
        Invoke-Nova 'scene save --as Assets/ProbeTest/ProbeScene.scene' | Out-Null
        $bake = Invoke-NovaJson 'probe bake'
        $file = Join-Path $Project 'Assets\ProbeTest\ProbeScene\ReflectionProbe-0.dds'
        View
        $s1 = Shot 'probe_baked.png'
        Add-Result reflectionprobe 'Bake: Assets/ProbeTest/ProbeScene/ReflectionProbe-0.dds, the sphere reflects the red wall' ((Test-Path $file) -and $bake.baked[0].bakedTexture -eq 'Assets/ProbeTest/ProbeScene/ReflectionProbe-0.dds' -and (IsRed $s1)) "file=$(Test-Path $file) baked=$($bake.baked[0].bakedTexture) $(Txt $s1)"

        Invoke-Nova 'scene save' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'scene open Assets/ProbeTest/ProbeScene.scene --force' | Out-Null
        View
        $g = Invoke-NovaJson 'get Probe'
        $c = @($g.components | Where-Object { $_.type -eq 'ReflectionProbe' })[0]
        $s2 = Shot 'probe_reopened.png'
        Add-Result reflectionprobe 'saved and reopened: baked texture kept and loaded (still red), box size kept' ($c -and $c.bakedTexture -eq 'Assets/ProbeTest/ProbeScene/ReflectionProbe-0.dds' -and (@($c.size | ForEach-Object { [double]$_ }) -join ',') -eq '30,20,30' -and (IsRed $s2)) "baked=$($c.bakedTexture) size=$($c.size -join ',') $(Txt $s2)"

        SetProbe '{"center":[0,0,60]}' | Out-Null
        $s3 = Shot 'probe_outside.png'
        SetProbe '{"center":[0,0,0]}' | Out-Null
        Add-Result reflectionprobe 'outside the probe box (Box Offset moved away) the sphere falls back to the sky' ($s3 -and -not (IsRed $s3)) (Txt $s3)

        SetProbe '{"intensity":0}' | Out-Null
        $s4 = Shot 'probe_intensity0.png'
        SetProbe '{"intensity":1}' | Out-Null
        Add-Result reflectionprobe 'Intensity 0 = dark reflection inside the box (no sky either)' ($s4 -and $s0 -and $s4.R -lt 120 -and $s4.B -lt $s0.B - 50 -and -not (IsRed $s4)) "$(Txt $s4) (sky $($s0.B))"

        SetProbe '{"mode":"Custom","customCubemap":"Assets/ProbeTest/ProbeScene/ReflectionProbe-0.dds"}' | Out-Null
        SetWall 'GreenWall'
        $s6 = Shot 'probe_custom.png'
        Add-Result reflectionprobe 'Custom: the chosen cubemap (the baked red one) even after the wall turned green' ((IsRed $s6)) (Txt $s6)

        SetProbe '{"mode":"Realtime","refreshMode":"OnAwake"}' | Out-Null
        $s7 = Shot 'probe_realtime_awake.png'
        SetWall 'RedWall'
        $s8 = Shot 'probe_realtime_awake_after.png'
        Invoke-Nova 'probe render' | Out-Null
        $s9 = Shot 'probe_realtime_rendered.png'
        Add-Result reflectionprobe 'Realtime On Awake: captures the green wall once, keeps it after the wall turns red until probe render' ((IsGreen $s7) -and (IsGreen $s8) -and (IsRed $s9)) "awake: $(Txt $s7) | after: $(Txt $s8) | render: $(Txt $s9)"

        SetProbe '{"refreshMode":"EveryFrame","timeSlicing":"IndividualFaces"}' | Out-Null
        SetWall 'GreenWall'
        Invoke-Nova 'wait 15' | Out-Null
        $s10 = Shot 'probe_everyframe.png'
        Add-Result reflectionprobe 'Realtime Every Frame (Individual Faces): follows the wall turning green by itself' ((IsGreen $s10)) (Txt $s10)

        # Box Projection: 찍는 점을 구에서 4 떨어뜨리고 (상자는 그대로 — Box Offset) 벽이 작아 보이던 것을 상자 벽에 맞춰 바로잡는다
        SetProbe '{"timeSlicing":"AllFacesAtOnce","center":[0,0,-4],"size":[12,8,12]}' | Out-Null
        Invoke-Nova 'set Probe --position 0,1,4' | Out-Null
        SetWall 'RedWall'
        $s11 = Shot 'probe_far_plain.png'
        SetProbe '{"boxProjection":true}' | Out-Null
        $s12 = Shot 'probe_far_boxprojection.png'
        Add-Result reflectionprobe 'Box Projection: capture point 4 m away, the wall reflection is corrected to its real size (bigger red patch)' ($s11 -and $s12 -and (IsRed $s12) -and $s12.Red -gt $s11.Red * 1.2) "plain red=$($s11.Red) boxProjection red=$($s12.Red)"
        $info = Invoke-NovaJson 'probe info'
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Audio
{
    Write-Host '[audio]'
    $mixer = Join-Path $Project 'Assets\NovaTestMixer.mixer'
    '{ "groups": [ { "id": 1, "name": "Master", "parent": -1 }, { "id": 2, "name": "Music", "parent": 0, "effects": [ { "id": 4, "type": "Lowpass" } ] }, { "id": 3, "name": "SFX", "parent": 0, "effects": [ { "id": 5, "type": "Reverb" } ] } ], "snapshots": [ { "name": "Normal", "values": { "g2/vol": 0 } }, { "name": "Quiet", "values": { "g2/vol": -40 } } ], "exposed": [ { "name": "MusicVol", "key": "g2/vol" } ], "nextId": 6 }' | Set-Content -Encoding utf8 $mixer
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'scene new --force' | Out-Null
        foreach ($x in @(@('AMusic', 'BGM_Loop.wav', 'Music'), @('ASfx', 'Coin.wav', 'SFX')))
        {
            Invoke-Nova "create empty --name $($x[0])" | Out-Null
            $values = (@{ clip = "Resources\Packages\Audio\SFX\$($x[1])"; loop = $true; outputMixer = 'Assets\NovaTestMixer.mixer'; outputGroup = $x[2] } | ConvertTo-Json -Compress).Replace('"', '\"')
            Invoke-Nova "add-component $($x[0]) AudioSource --values `"$values`"" | Out-Null
        }
        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $lf = Join-Path $Out 'mixer_levels.cs'
        'var m = NovaEngine.Audio.AudioMixer.Load("Assets/NovaTestMixer.mixer"); float v; m.GetFloat("MusicVol", out v); return m.GetGroupLevel("Music").ToString("F1") + " " + m.GetGroupLevel("SFX").ToString("F1") + " " + v.ToString("F1");' | Set-Content -Encoding utf8 $lf
        $level = { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 0.8) { Invoke-Nova 'wait 5' | Out-Null }; $r = Invoke-NovaJson "exec --file $lf"; if ($r) { "$($r.result)" -split ' ' } else { @() } }
        $a = & $level
        $avail = (Invoke-NovaJson 'log -n 400 --grep "XAudio2 ready"' | Out-String) -match 'ready'
        if (-not $avail -and $a.Count -eq 3 -and [double]$a[0] -le -79)
        {
            Add-Result audio 'mixer groups (no audio device)' $true 'XAudio2 not available - skipped'
        }
        else
        {
            Add-Result audio 'audio source plays through its mixer group' ($a.Count -eq 3 -and [double]$a[0] -gt -30 -and [double]$a[1] -gt -40) "Music $($a[0]) dB, SFX $($a[1]) dB (expect sound in both)"
            $sf = Join-Path $Out 'mixer_set.cs'
            'return NovaEngine.Audio.AudioMixer.Load("Assets/NovaTestMixer.mixer").SetFloat("MusicVol", -80f);' | Set-Content -Encoding utf8 $sf
            Invoke-Nova "exec --file $sf" | Out-Null
            $b = & $level
            Add-Result audio 'exposed parameter mutes only that group' ($b.Count -eq 3 -and [double]$b[0] -le -79 -and [double]$b[1] -gt -40) "Music $($b[0]) dB, SFX $($b[1]) dB"
            $tf = Join-Path $Out 'mixer_snap.cs'
            'var m = NovaEngine.Audio.AudioMixer.Load("Assets/NovaTestMixer.mixer"); m.ClearFloat("MusicVol"); m.FindSnapshot("Quiet").TransitionTo(0.3f); return "ok";' | Set-Content -Encoding utf8 $tf
            Invoke-Nova "exec --file $tf" | Out-Null
            $c = & $level
            Add-Result audio 'snapshot transition lowers the group' ($c.Count -eq 3 -and [math]::Abs([double]$c[2] + 40) -lt 0.1 -and [double]$c[0] -lt [double]$a[0] - 20) "MusicVol $($c[2]) (expect -40), Music $($c[0]) dB"
        }
        Invoke-Nova 'stop' | Out-Null

        # OGG · MP3 (테스트 프로젝트에 Assets/TestAssets/Audio 의 ring.ogg · hey.mp3 · long.mp3(> 20 초) 가 있을 때만)
        $audioDir = Join-Path $Project 'Assets\TestAssets\Audio'
        if ((Test-Path (Join-Path $audioDir 'ring.ogg')) -and (Test-Path (Join-Path $audioDir 'hey.mp3')) -and (Test-Path (Join-Path $audioDir 'long.mp3')))
        {
            Invoke-Nova 'scene new --force' | Out-Null
            # Load Type 기본은 Decompress On Load (Unity) → 긴 곡은 Import Settings 로 Streaming
            '{ "importer": "AudioImporter", "loadType": "Streaming" }' | Set-Content -Encoding utf8 (Join-Path $audioDir 'long.mp3.meta')
            $k = 0
            foreach ($f in @('ring.ogg', 'hey.mp3', 'long.mp3'))
            {
                Invoke-Nova "create empty --name ACodec$k" | Out-Null
                $values = (@{ clip = "Assets\TestAssets\Audio\$f"; playOnAwake = $false } | ConvertTo-Json -Compress).Replace('"', '\"')
                Invoke-Nova "add-component ACodec$k AudioSource --values `"$values`"" | Out-Null
                $k++
            }
            $cf = Join-Path $Out 'codec_len.cs'
            'string r = ""; for (int i = 0; i < 3; i++) r += GameObject.Find("ACodec" + i).GetComponent<AudioSource>().clip.length.ToString("F2") + " "; return r.Trim();' | Set-Content -Encoding utf8 $cf
            $len = Invoke-NovaJson "exec --file $cf"
            $lv = if ($len) { "$($len.result)" -split ' ' } else { @() }
            $streamLine = (Invoke-Nova 'log -n 5000 --grep "long.mp3"') -join ' '
            Add-Result audio 'ogg and mp3 decode, long mp3 streams' ($lv.Count -eq 3 -and [double]$lv[0] -gt 1 -and [double]$lv[1] -gt 2 -and [double]$lv[2] -gt 20 -and $streamLine -match 'streaming') "lengths $($lv -join ' / ') s, streaming $([bool]($streamLine -match 'streaming'))"
        }
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item -LiteralPath $mixer -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $Project 'Assets\TestAssets\Audio\long.mp3.meta') -ErrorAction SilentlyContinue
    }
}

# ------------------------------------------------------------------ 자동 저장 + 충돌 복구: 변경 → autosave now → 테스트 에디터 강제 종료 → 다시 열어 recover
function Suite-Recovery
{
    Write-Host '[recovery]'
    $folder = Join-Path $Project 'Library\AutoSave'
    $ed = Start-TestEditor
    Invoke-Nova 'scene new --force' | Out-Null
    Invoke-Nova 'create cube --name RecoverCube --position 1,2,3' | Out-Null
    $n = Invoke-NovaJson 'autosave now'
    $files = @(Get-ChildItem $folder -Filter "autosave_$($ed.Pid).scene" -ErrorAction SilentlyContinue)
    Add-Result recovery 'autosave writes Library/AutoSave' ($n -and $files.Count -eq 1) "files=$($files.Count)"
    Stop-Process -Id $ed.Pid -Force   # 충돌 흉내 (이 테스트가 띄운 에디터만)
    Stop-Job $ed.Job -ErrorAction SilentlyContinue; Remove-Job $ed.Job -Force -ErrorAction SilentlyContinue
    Start-Sleep 1
    $ed = Start-TestEditor
    try
    {
        $st = Invoke-NovaJson 'autosave status'
        Add-Result recovery 'next start offers recovery' ($st -and $st.pendingRecovery -match 'Untitled') "pending=$($st.pendingRecovery)"
        Invoke-Nova 'autosave recover' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $c = Invoke-NovaJson 'get RecoverCube'
        $ok = $c -and (($c.position -join ',') -eq '1.0,2.0,3.0') -and (Info).dirty
        Add-Result recovery 'recover restores the scene (dirty)' $ok "position=$($c.position -join ',')"
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
    $left = @(Get-ChildItem $folder -ErrorAction SilentlyContinue)
    Add-Result recovery 'clean quit removes session files' ($left.Count -eq 0) "left=$($left.Count)"
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
            Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null; Invoke-Nova 'window scene' | Out-Null;   # Play 가 Game 탭을 앞으로 한 뒤에
    Invoke-Nova 'wait 240' | Out-Null
            Invoke-Nova "screenshot $offPng --view scene" | Out-Null; Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 20' | Out-Null
            foreach ($t in 'Campfire', 'Campfire/Smoke', 'Campfire/Embers')
            {
                Invoke-Nova ('set "{0}" ParticleSystem.renderer={{\"softParticles\":true,\"softDistance\":0.6,\"lit\":true}}' -f $t) | Out-Null
            }
            Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null; Invoke-Nova 'window scene' | Out-Null;   # Play 가 Game 탭을 앞으로 한 뒤에
    Invoke-Nova 'wait 240' | Out-Null
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
                'animation' { Suite-Animation }
                'import' { Suite-Import }
                'ui' { Suite-UI }
                'packages' { Suite-Packages }
                'model' { Suite-Model }
                'anim2d' { Suite-Anim2D }
                'layers' { Suite-Layers }
                'sprites' { Suite-Sprites }
                'physics2d' { Suite-Physics2D }
                'shadergraph' { Suite-ShaderGraph }
                'decal' { Suite-Decal }
                'reflectionprobe' { Suite-ReflectionProbe }
                'audio' { Suite-Audio }
                'recovery' { Suite-Recovery }
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
