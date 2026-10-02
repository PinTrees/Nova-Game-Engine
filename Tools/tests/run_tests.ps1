# NOVA 자동 회귀 검사 — 실행 중인 에디터를 NOVA CLI 로 다뤄 확인한다 (테스트 프로젝트에서만, 저장하지 않음).
#
#   powershell -ExecutionPolicy Bypass -File Tools\tests\run_tests.ps1                 # quick (약 4~6 분)
#   ... -Suite full          + 성능(DX11 대 OpenGL), 파티클 Soft · Lit
#   ... -Interactive         + 실제 키 입력 검사 (에디터를 앞으로 띄운다 — 그동안 키보드·마우스를 쓰지 말 것)
#   ... -Only cli,render     골라서 (cli, physics, animation, import, ui, packages, model, recovery, render, gfx, perf, particles, keys)
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

$suites = if ($Only.Count) { $Only } else { @('cli', 'physics', 'animation', 'import', 'ui', 'packages', 'model', 'audio', 'recovery', 'render', 'gfx') + $(if ($Suite -eq 'full') { @('perf', 'particles') } else { @() }) + $(if ($Interactive) { @('keys') } else { @() }) }
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
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Stop-TestEditor $ed
        if ($null -ne $before) { Set-Content -Path $manifest -Value $before -NoNewline -Encoding utf8 }
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
