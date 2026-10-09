# NOVA 자동 회귀 검사 — 실행 중인 에디터를 NOVA CLI 로 다뤄 확인한다 (테스트 프로젝트에서만, 저장하지 않음).
#
#   powershell -ExecutionPolicy Bypass -File Tools\tests\run_tests.ps1                 # quick (약 4~6 분)
#   ... -Suite full          + 성능(DX11 대 OpenGL), 파티클 Soft · Lit
#   ... -Interactive         + 실제 키 입력 검사 (에디터를 앞으로 띄운다 — 그동안 키보드·마우스를 쓰지 말 것)
#   ... -Only cli,render     골라서 (cli, physics, animation, import, ui, packages, model, anim2d, layers, sprites, physics2d, shadergraph, decal, reflectionprobe, probevolume, depthoffield, lodgroup, ssr, modelplace, antialiasing, web, scenes, tween, light2d, nav2d, ragdoll, wheel, daynight, cloth, clothskin, starter, behaviour, recovery, render, gfx, vulkan, perf, particles, vfx, vfxgl, vfxvk, weather, tessellation, tessellationgl, tessellationvk, keys)
#   ... -Project <폴더>      테스트 프로젝트 (기본 = 환경 변수 NOVA_TEST_PROJECT, 없으면 E:\NovaTest\ScriptTest)
#
# 결과: 표(PASS/FAIL) + <Out>\results.json, 캡처·차이 그림은 <Out>\ (기본 TestResults\<시각>). 실패가 있으면 종료 코드 1.
#   기본 TestResults\<시각> 폴더는 최근 -KeepResults 개 (기본 10) 만 남기고 오래된 것부터 지운다 (0 = 지우지 않음, 이름을 정한 -Out 폴더는 건드리지 않음)
# 테스트 프로젝트에 필요한 것: Assets/Scenes 의 Materials, Particles, Forest, Trees, Shadows, Culling, SampleScene (+ 지형 이름 Terrain).
param(
    [ValidateSet('quick', 'full')][string]$Suite = 'quick',
    [string[]]$Only = @(),
    [switch]$Interactive,
    [string]$Project = '',
    [string]$Out = '',
    [int]$KeepResults = 10
)
. (Join-Path $PSScriptRoot 'common.ps1')
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })   # -File 로 부르면 a,b 가 글자 하나로 온다
if (-not $Project) { $Project = if ($env:NOVA_TEST_PROJECT) { $env:NOVA_TEST_PROJECT } else { 'E:\NovaTest\ScriptTest' } }
$script:Project = $Project
if (-not (Test-Path (Join-Path $Project 'Assets'))) { throw "test project not found: $Project (use -Project or NOVA_TEST_PROJECT)" }
if (-not (Test-Path $Nova)) { throw "nova.exe not found — build first (build.bat)" }
if (-not $Out)
{
    $Out = Join-Path $Root ('TestResults\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    # 결과가 쌓이지 않게: 이번 것을 포함해 최근 KeepResults 개만 (yyyyMMdd-HHmmss 이름만 대상)
    if ($KeepResults -gt 0 -and (Test-Path (Join-Path $Root 'TestResults')))
    {
        Get-ChildItem (Join-Path $Root 'TestResults') -Directory | Where-Object { $_.Name -match '^\d{8}-\d{6}$' } |
            Sort-Object Name -Descending | Select-Object -Skip ([Math]::Max(0, $KeepResults - 1)) |
            ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }
    }
}
New-Item -ItemType Directory -Force $Out | Out-Null

$suites = if ($Only.Count) { $Only } else { @('cli', 'physics', 'animation', 'import', 'ui', 'packages', 'model', 'anim2d', 'tilemap', 'layers', 'sprites', 'physics2d', 'shadergraph', 'decal', 'reflectionprobe', 'probevolume', 'depthoffield', 'lodgroup', 'occlusion', 'occlusiongl', 'occlusionvk', 'linetrail', 'material', 'vfx', 'vfxgl', 'vfxvk', 'weather', 'tessellation', 'ssr', 'ssao', 'motionvectors', 'cinemachine', 'renderingdebug', 'forwardplus', 'rendergraph', 'modelplace', 'antialiasing', 'audio', 'web', 'scenes', 'tween', 'light2d', 'nav2d', 'ragdoll', 'wheel', 'daynight', 'cloth', 'clothskin', 'starter', 'behaviour', 'recovery', 'render', 'gfx', 'vulkan', 'd3d12', 'vfx12', 'virtualtexture', 'deferred', 'jobs', 'physicsasync', 'renderthread', 'memory', 'transform', 'streaming') + $(if ($Suite -eq 'full') { @('perf', 'particles') } else { @() }) + $(if ($Interactive) { @('keys') } else { @() }) }
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

        # 활성 카메라 (Camera.main = DisplayManager::GetActiveCamera — 등록된 카메라에서 고른다): Priority 가 높은 것, 꺼진 오브젝트는 빼고, 실행 중 붙인 카메라도
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create camera --name CamB' | Out-Null
        Invoke-Nova 'set CamB --component Camera --values "{\"priority\":5}"' | Out-Null
        $cm1 = (Invoke-NovaJson 'exec Camera.main.gameObject.name').result
        $f = Join-Path $Out 'cli_cam.cs'
        'GameObject.Find("CamB").SetActive(false); var g = new GameObject("CamC"); g.AddComponent<Camera>(); return Camera.main.gameObject.name;' | Set-Content -Encoding utf8 $f
        $cm2 = (Invoke-NovaJson "exec --file `"$f`"").result
        Invoke-Nova 'wait 2' | Out-Null
        Invoke-Nova 'set CamC --component Camera --values "{\"priority\":9}"' | Out-Null
        $cm3 = (Invoke-NovaJson 'exec Camera.main.gameObject.name').result
        Add-Result cli 'active camera: highest priority, inactive objects skipped, a camera added at runtime is found' ("$cm1" -eq 'CamB' -and "$cm2" -eq 'Main Camera' -and "$cm3" -eq 'CamC') "priority 5: $cm1; CamB off: $cm2; runtime CamC priority 9: $cm3"
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
        # FBX 와 VRM 이 같은 모양 (같은 자리의 RigChibi): 앞에서 찍은 실루엣이 겹친다 (배경만 찍은 그림과 다른 화소 = 캐릭터)
        #  (예전: 메시 "Head" 와 본 "Head" 가 같은 이름이라 본 자리에 메시 노드를 잡아 머리가 가슴으로 내려갔다 → 이제 메시는 Head_Mesh),
        #  재질 = FBX Diffuse 색 (ChibiRig_FBX.Materials — 예전: 모두 기본 은색)
        Invoke-Nova 'set "Main Camera" --position 0,0.8,2.6 --rotation 4,180,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        function Snap([string]$name) { Invoke-Nova 'wait 6' | Out-Null; $png = Join-Path $dir "$name.png"; Invoke-Nova "screenshot `"$png`" --view game" | Out-Null; return $png }
        # 캐릭터 하나만 보이게: 자식 Skinned Mesh Renderer 를 켜고 끈다 (부모 SetActive 는 아직 자식 그리기를 숨기지 않는다)
        function Show([bool]$fbx, [bool]$vrm)
        {
            $f = Join-Path $dir 'rig_show.cs'
            ('foreach (var r in GameObject.Find("FbxChibi").GetComponentsInChildren<SkinnedMeshRenderer>()) r.enabled = ' + "$fbx".ToLower() + '; foreach (var r in GameObject.Find("RigChibi").GetComponentsInChildren<SkinnedMeshRenderer>()) r.enabled = ' + "$vrm".ToLower() + '; return 1;') | Set-Content -Encoding utf8 $f
            Invoke-NovaJson "exec --file $f" | Out-Null
        }
        Show $true $false
        $sFbx = Snap 'fbx_alone'
        Show $false $true
        $sVrm = Snap 'vrm_alone'
        Show $false $false
        $sBg = Snap 'no_character'
        Show $true $true
        Invoke-Nova 'window scene' | Out-Null
        Add-Type -AssemblyName System.Drawing
        # 윤곽 비교: 줄마다 캐릭터의 왼쪽 · 오른쪽 끝 (안쪽 색은 재질 · 하늘 반사로 배경과 비슷할 수 있다) + 맨 위 · 맨 아래 줄
        $edge = 99.0; $topDiff = 99; $bottomDiff = 99
        if ((Test-Path $sFbx) -and (Test-Path $sVrm) -and (Test-Path $sBg))
        {
            $bf = [System.Drawing.Bitmap]::FromFile($sFbx); $bv = [System.Drawing.Bitmap]::FromFile($sVrm); $bb = [System.Drawing.Bitmap]::FromFile($sBg)
            function Span($bmp, $bg, [int]$y)
            {
                $lo = -1; $hi = -1
                for ($x = 0; $x -lt $bg.Width; $x += 2)
                {
                    $g = $bg.GetPixel($x, $y); $c = $bmp.GetPixel($x, $y)
                    if (([math]::Abs($c.R - $g.R) + [math]::Abs($c.G - $g.G) + [math]::Abs($c.B - $g.B)) -gt 30) { if ($lo -lt 0) { $lo = $x }; $hi = $x }
                }
                return @($lo, $hi)
            }
            $sum = 0.0; $rows = 0; $fTop = -1; $fBot = -1; $vTop = -1; $vBot = -1
            for ($y = 0; $y -lt $bb.Height; $y += 2)
            {
                $fs2 = Span $bf $bb $y; $vs2 = Span $bv $bb $y
                if ($fs2[0] -ge 0) { if ($fTop -lt 0) { $fTop = $y }; $fBot = $y }
                if ($vs2[0] -ge 0) { if ($vTop -lt 0) { $vTop = $y }; $vBot = $y }
                if ($fs2[0] -lt 0 -and $vs2[0] -lt 0) { continue }
                $rows++
                if ($fs2[0] -lt 0 -or $vs2[0] -lt 0) { $sum += 100 } else { $sum += [math]::Abs($fs2[0] - $vs2[0]) + [math]::Abs($fs2[1] - $vs2[1]) }
            }
            $bf.Dispose(); $bv.Dispose(); $bb.Dispose()
            if ($rows -gt 0) { $edge = $sum / $rows; $topDiff = [math]::Abs($fTop - $vTop); $bottomDiff = [math]::Abs($fBot - $vBot) }
        }
        $fc = Join-Path $dir 'rig_fbx_mat.cs'
        'string names = ""; string dress = "none"; foreach (var r in GameObject.Find("FbxChibi").GetComponentsInChildren<SkinnedMeshRenderer>()) { if (r.gameObject.name.StartsWith("Head")) names += r.gameObject.name; if (r.gameObject.name == "Body") foreach (var m in r.sharedMaterials) if (m != null && m.name.StartsWith("Dress")) dress = m.color.r.ToString("F2", System.Globalization.CultureInfo.InvariantCulture) + "," + m.color.g.ToString("F2", System.Globalization.CultureInfo.InvariantCulture) + "," + m.color.b.ToString("F2", System.Globalization.CultureInfo.InvariantCulture); } return names + " " + dress;' | Set-Content -Encoding utf8 $fc
        $fs = Invoke-NovaJson "exec --file $fc"
        $fsa = if ($fs) { "$($fs.result)" -split ' ' } else { @() }
        $matDir = Join-Path $vrmDir 'ChibiRig_FBX.Materials'
        $okShape = $edge -lt 4 -and $topDiff -le 4 -and $bottomDiff -le 4 -and $fsa.Count -eq 2 -and $fsa[0] -eq 'Head_Mesh' -and $fsa[1] -eq '0.35,0.38,0.62' -and (Test-Path (Join-Path $matDir 'Dress.mat'))
        Add-Result model 'FBX character = VRM shape (same outline, Head_Mesh) + FBX Diffuse colors extracted' $okShape ("outline: edge difference {0:F1} px per row (< 4), top {1} px, bottom {2} px (<= 4); head renderer {3}, Dress color {4} (0.35,0.38,0.62), {5} .mat" -f $edge, $topDiff, $bottomDiff, $fsa[0], $fsa[1], @(Get-ChildItem $matDir -Filter *.mat -ErrorAction SilentlyContinue).Count)
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
        Add-Result model 'anim: Idle · Walk · Wave clips keyed from poses, anim.time samples (walk frames differ)' ($ab -and @($al.clips).Count -eq 5 -and ($al.clips | Where-Object { $_.name -eq 'Walk' }).keys -gt 40 -and $wdiff -gt 100) "clips=$(($al.clips | ForEach-Object { $_.name }) -join ',') changed px=$wdiff"
        $ag = M "export --path $vrmDir\Anim.glb"
        $agJson = $null
        if (Test-Path "$vrmDir\Anim.glb") { $bytes = [IO.File]::ReadAllBytes("$vrmDir\Anim.glb"); $len = [BitConverter]::ToUInt32($bytes, 12); $agJson = [Text.Encoding]::UTF8.GetString($bytes, 20, $len) | ConvertFrom-Json }
        $an = if ($agJson) { @($agJson.animations | ForEach-Object { $_.name }) } else { @() }
        Add-Result model 'export .glb: glTF animations (rotation + Hips translation) + VRMC_vrm humanoid for retargeting' ($an.Count -eq 5 -and $agJson.extensions.VRMC_vrm.humanoid -and @($agJson.animations[0].channels).Count -ge 21) "animations=$($an -join ',') channels(Idle)=$(@($agJson.animations[0].channels).Count)"
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
        if ($manifestBefore) { [IO.File]::WriteAllBytes($manifest, $manifestBefore) }
    }
}

function Suite-Tilemap
{
    # 2D Tilemap 패키지: 타일셋 자르기 → .tile, Grid + Tilemap, 상자 채우기 · 흘려 채우기 · 회전, 맞닿은 칸 콜라이더 합치기,
    # 화면 (점 필터 타일 색), Play 에서 상자가 타일 바닥에 선다, Tile Palette (붓 · 고르기 · 지우개), Undo, 씬 저장 · 열기, C# API
    Write-Host '[tilemap]'
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $before = if (Test-Path $manifest) { Get-Content $manifest -Raw } else { $null }
    $dir = Join-Path $Out 'tilemap'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\TilemapTest'
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    # 16 px 타일 4 개 (풀 · 흙 · 돌 · 물), 64 × 16
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap 64, 16
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $cols = @(@(70, 170, 60), @(140, 90, 45), @(128, 128, 128), @(40, 90, 210))
    for ($i = 0; $i -lt 4; $i++) { $br = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, $cols[$i][0], $cols[$i][1], $cols[$i][2])); $g.FillRectangle($br, $i * 16, 0, 16, 16); $br.Dispose() }
    $g.Dispose()
    $bmp.Save((Join-Path $assetDir 'Tiles.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $ed = Start-TestEditor
    try
    {
        $a = Invoke-NovaJson 'package add com.nova.tilemap'
        Add-Result tilemap 'package loads (NovaTilemap.dll)' ($a -and $a.loaded) "loaded=$($a.loaded)"
        function TM([string]$line) { Invoke-NovaJson "tilemap $line" }
        $h = TM 'help'
        Add-Result tilemap 'tilemap help lists ops' ($h -and $h.box -and $h.'tile.fromtexture' -and $h.paint) "ops=$(@($h.PSObject.Properties).Count)"

        # 타일셋 자르기 (16 px, PPU 16 → 칸 하나 = 1 단위, 점 필터) → 팔레트 폴더에 .tile 4 개
        $s = Invoke-NovaJson 'sprite-slice Assets/TilemapTest/Tiles.png --cell 16,16 --ppu 16 --filter point'
        $pal = 'Assets\TilemapTest\Palette'
        $ft = TM "tile.fromtexture --texture Assets/TilemapTest/Tiles.png --folder $pal --collider grid"
        $names = @($ft.tiles | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) }) -join ','
        Add-Result tilemap 'sliced tileset → one .tile per sprite' ($s.sprites -eq 4 -and $ft.count -eq 4 -and $names -eq 'Tiles_0,Tiles_1,Tiles_2,Tiles_3') "sprites=$($s.sprites) tiles=$names"
        $grass = "$pal\Tiles_0.tile"; $dirt = "$pal\Tiles_1.tile"; $stone = "$pal\Tiles_2.tile"; $water = "$pal\Tiles_3.tile"
        $ti = TM "tile.info --path $grass"
        Add-Result tilemap '.tile: sprite of the slice, 1 x 1 units, collider grid' ($ti.hasSprite -and [math]::Abs($ti.size[0] - 1) -lt 0.001 -and $ti.colliderType -eq 'grid') "sprite=$($ti.sprite) size=$($ti.size -join 'x') collider=$($ti.colliderType)"

        Invoke-Nova 'scene new --force' | Out-Null
        $c = TM 'create --name Ground --collider'
        Add-Result tilemap 'create: Grid + child Tilemap (+ renderer, collider)' ($c -and $c.grid -eq 'Grid' -and $c.count -eq 0 -and $null -ne $c.shapes) "grid=$($c.grid) count=$($c.count) shapes=$($c.shapes)"
        TM "box --tilemap Ground --from -8,-3 --to 7,-3 --tile $grass" | Out-Null
        TM "box --tilemap Ground --from -8,-5 --to 7,-4 --tile $dirt" | Out-Null
        $i1 = TM 'info --tilemap Ground'
        Add-Result tilemap 'box fill 16 x 3 → 48 tiles, touching cells merge into ONE collider box' ($i1.count -eq 48 -and $i1.shapes -eq 1 -and $i1.quads -eq 48) "count=$($i1.count) shapes=$($i1.shapes) quads=$($i1.quads) bounds=$($i1.bounds.min -join ',')..$($i1.bounds.max -join ',')"
        TM "box --tilemap Ground --from 2,0 --to 5,0 --tile $stone" | Out-Null
        TM "set --tilemap Ground --x 6 --y 1 --tile $stone --rotation 90 --flipx" | Out-Null
        $gt = TM 'get --tilemap Ground --x 6 --y 1'
        $i2 = TM 'info --tilemap Ground'
        Add-Result tilemap 'platform + rotated / flipped tile (separate boxes: 3)' ($gt.rotation -eq 90 -and $gt.flipx -and $i2.shapes -eq 3) "rotation=$($gt.rotation) flipx=$($gt.flipx) shapes=$($i2.shapes)"

        # 흘려 채우기: 테두리 안의 빈 칸 9 개만 (범위 = 타일이 있는 칸 상자)
        TM 'create --name Deco --grid Grid' | Out-Null
        TM "box --tilemap Deco --from -7,2 --to -3,6 --tile $stone" | Out-Null
        TM 'box --tilemap Deco --from -6,3 --to -4,5' | Out-Null
        $f1 = TM "fill --tilemap Deco --x -5 --y 4 --tile $water"
        $f2 = TM "fill --tilemap Deco --x -7 --y 2 --tile $grass"
        $di = TM 'info --tilemap Deco'
        Add-Result tilemap 'flood fill: 9 empty cells inside a stone ring, then the 16-tile ring' ($f1.changed -eq 9 -and $f2.changed -eq 16 -and $di.count -eq 25) "inside=$($f1.changed) ring=$($f2.changed) count=$($di.count)"

        # 화면: 점 필터 타일 색 (풀 · 흙 · 돌 · 물)
        Invoke-Nova 'camera --position -0.5,0,-14 --target -0.5,0,0' | Out-Null
        Invoke-Nova 'wait 15' | Out-Null
        $ss = Join-Path $dir 'scene.png'
        Invoke-Nova "screenshot $ss --view scene" | Out-Null
        $n = @{ grass = 0; dirt = 0; stone = 0; water = 0 }
        if (Test-Path $ss)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($ss)
            for ($y = 0; $y -lt $bm.Height; $y += 3) { for ($x = 0; $x -lt $bm.Width; $x += 3) {
                $p = $bm.GetPixel($x, $y)
                foreach ($k in 0..3) { if ([math]::Abs($p.R - $cols[$k][0]) -lt 25 -and [math]::Abs($p.G - $cols[$k][1]) -lt 25 -and [math]::Abs($p.B - $cols[$k][2]) -lt 25) { $n[@('grass', 'dirt', 'stone', 'water')[$k]]++ } }
            } }
            $bm.Dispose()
        }
        Add-Result tilemap 'scene view: tile colors drawn (grass, dirt, stone, water)' ($n.grass -gt 300 -and $n.dirt -gt 600 -and $n.stone -gt 100 -and $n.water -gt 50) "grass=$($n.grass) dirt=$($n.dirt) stone=$($n.stone) water=$($n.water)"

        # Tile Palette: 붓 · 고르기 · 지우개 (Scene 뷰 클릭과 같은 함수)
        $w = TM 'window'
        TM "palette --folder $pal" | Out-Null
        TM "select --tile $water" | Out-Null
        TM 'tool --tool brush' | Out-Null
        $p1 = TM 'paint --tilemap Ground --x 10 --y -3'
        $p2 = TM 'paint --tilemap Ground --x 0 --y -3 --tool picker'
        $p3 = TM 'paint --tilemap Ground --x 10 --y -3 --tool eraser'
        Add-Result tilemap 'palette: brush paints, picker takes the tile (→ brush), eraser removes' ($w.window -and $p1.changed -eq 1 -and $p2.selected -like '*Tiles_0.tile' -and $p2.tool -eq 'brush' -and $p3.changed -eq 1 -and $p3.count -eq 53) "brush=$($p1.changed) picked=$([IO.Path]::GetFileName($p2.selected)) tool=$($p2.tool) erase=$($p3.changed) count=$($p3.count)"
        Invoke-Nova 'wait 5' | Out-Null
        Invoke-Nova "screenshot $(Join-Path $dir 'palette.png') --view editor" | Out-Null

        # Undo
        $u0 = (TM 'info --tilemap Ground').count
        TM "set --tilemap Ground --x -8 --y 3 --tile $water" | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $u1 = (TM 'info --tilemap Ground').count
        Invoke-Nova 'undo' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $u2 = (TM 'info --tilemap Ground').count
        Add-Result tilemap 'set tile → undo' ($u1 -eq $u0 + 1 -and $u2 -eq $u0) "$u0 → $u1 → $u2"

        # 저장 · 열기
        $scene = 'Assets/TilemapTest/Tilemap.scene'
        Invoke-Nova "scene save --as $scene" | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova "scene open $scene" | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $r = TM 'info --tilemap Ground'
        $rg = TM 'get --tilemap Ground --x 6 --y 1'
        Add-Result tilemap 'scene save → open (tiles, rotation, collider boxes)' ($r.count -eq $u0 -and $r.shapes -eq 3 -and $rg.rotation -eq 90 -and $rg.flipx) "count=$($r.count) shapes=$($r.shapes) rotation=$($rg.rotation)"

        # Play: 상자가 떨어져 풀 위 (윗면 y = -2) 에 선다 + C# API
        Invoke-Nova 'create empty --name Crate --position -4,2,0' | Out-Null
        Invoke-Nova 'add-component Crate SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[0.9,0.2,0.2,1]}"' | Out-Null
        Invoke-Nova 'add-component Crate BoxCollider2D' | Out-Null
        Invoke-Nova 'add-component Crate Rigidbody2D' | Out-Null
        Wait-Compile
        $yf = Join-Path $dir 'y.cs'
        'return GameObject.Find("Crate").transform.position.y.ToString("F3", System.Globalization.CultureInfo.InvariantCulture);' | Set-Content -Encoding utf8 $yf
        $cf = Join-Path $dir 'api.cs'
        @'
var t = GameObject.Find("Ground").GetComponent<NovaEngine.Tilemaps.Tilemap>();
var grid = GameObject.Find("Grid").GetComponent<Grid>();
var before = t.GetUsedTilesCount();
t.SetTile(new Vector3Int(-8, 3, 0), new NovaEngine.Tilemaps.Tile("Assets/TilemapTest/Palette/Tiles_3.tile"));
var b = t.cellBounds;
var cell = t.WorldToCell(new Vector3(2.5f, 0.5f, 0));
var got = t.GetTile(cell);
var center = t.GetCellCenterWorld(new Vector3Int(2, 0, 0));
return before + " " + t.GetUsedTilesCount() + " " + b.xMin + "," + b.yMin + "," + b.size.x + "," + b.size.y + " " + cell + " " + (got == null ? "null" : got.name) + " " + t.HasTile(new Vector3Int(-8, 3, 0)) + " " + center.x.ToString("F1", System.Globalization.CultureInfo.InvariantCulture) + " " + grid.cellSize.x.ToString("F1", System.Globalization.CultureInfo.InvariantCulture);
'@ | Set-Content -Encoding utf8 $cf
        Invoke-Nova 'play' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 3) { Invoke-Nova 'wait 10' | Out-Null }
        $cy = [double](Invoke-NovaJson "exec --file $yf").result
        $api = Invoke-NovaJson "exec --file $cf"
        $ci = TM 'info --tilemap Ground'
        Invoke-Nova 'stop' | Out-Null
        Add-Result tilemap 'play: a falling crate rests on the tile ground (center y = -1.5)' ([math]::Abs($cy + 1.5) -lt 0.05) "y=$cy"
        Add-Result tilemap 'C#: SetTile / GetTile / cellBounds / WorldToCell / GetCellCenterWorld / Grid.cellSize' ("$($api.result)" -eq '53 54 -8,-5,16,9 (2, 0, 0) Tiles_2 True 2.5 1.0' -and $ci.shapes -eq 4) "$($api.result) shapes=$($ci.shapes)"
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

function Suite-Web
{
    # 웹 빌드 (WebGPU + WebAssembly): 창 없는 Chrome (Tools/web — CLI 만, 화면 · 마우스를 쓰지 않는다)
    #  1) C# 검사 장면 (강체 낙하 · 긴 mp3 · 검사 스크립트) 내보내기 → .NET 판 플레이어 · 쓰는 BCL 만
    #  2) 브라우저에서 실행: WebGPU 오류 0 · C# (Start · Update · WebGLPlayer) · 물리 (상자가 바닥에 선다) · 소리 (출력 진폭)
    #  3) 재질 장면 그림 = PC DX11 (android reference 와 같은 그리기 순서), UI (Screen Space - Overlay 가운데 · 왼쪽 아래) 위치 = DX11
    #  4) nova web build --run: Build Settings 씬 → 에디터의 미리 보기 서버 (wasm MIME · 격리 머리 · 폴더 밖 404) → 브라우저에서 돈다
    Write-Host '[web]'
    $dir = Join-Path $Out 'web'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $bash = 'C:\Program Files\Git\bin\bash.exe'
    $runScene = Join-Path $Root 'Tools\web\run_scene.sh'
    function RunScene([string]$folder, [string]$shot, [int]$frames, [int]$port)
    {
        $o = & $bash $runScene ($folder -replace '\\', '/') ($shot -replace '\\', '/') $frames $port 2>$null | Out-String
        try { return $o | ConvertFrom-Json } catch { return $null }
    }
    $editorSettings = Join-Path $Project 'Assets\EditorSettings.json'
    $settingsBefore = if (Test-Path $editorSettings) { [IO.File]::ReadAllBytes($editorSettings) } else { $null }   # 씬 저장이 LastOpenedScenePath 를 바꾼다
    $probeDir = Join-Path $Project 'Assets\WebProbe'
    New-Item -ItemType Directory -Force $probeDir | Out-Null
    $probeFile = Join-Path $probeDir 'WebProbe.cs'
    $probeBefore = if (Test-Path $probeFile) { Get-Content $probeFile -Raw } else { '' }
    $ed = Start-TestEditor
    try
    {
        $h = Invoke-NovaJson 'web help'
        Add-Result web 'web players built' ($h -and $h.player -and $h.dotnetPlayer) "engine: $($h.player); C#: $($h.dotnetPlayer)"
        if (-not ($h -and $h.player -and $h.dotnetPlayer)) { return }   # Web/build.sh Release · Web/build.sh Release host

        # ---- 1) C# 검사 장면
        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        @'
using System.Linq;
using NovaEngine;

// Web probe (Tools/tests/run_tests.ps1 -Only web): C# on the .NET WebAssembly runtime, physics on the box, platform
public class WebProbe : MonoBehaviour
{
    int frames;

    void Start()
    {
        int sum = Enumerable.Range(1, 10).Select(i => i * i).Sum();
        Debug.Log($"WebProbe start sum={sum} platform={Application.platform} mobile={Application.isMobilePlatform}");
    }

    void Update()
    {
        if (++frames == 150)
            Debug.Log($"WebProbe frames={frames} y={transform.position.y:F2} time={Time.time:F2}");
    }
}
'@ | Set-Content -Encoding utf8 $probeFile
        $probeChanged = (Get-Content $probeFile -Raw) -ne $probeBefore
        $sw2 = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw2.Elapsed.TotalSeconds -lt 90 -and (($inf -and $inf.compiling) -or ($probeChanged -and $now -eq $dllBefore)))
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create plane --name Ground' | Out-Null
        Invoke-Nova 'create cube --name Box --position 0,4,0 --rotation 20,30,10' | Out-Null
        Invoke-Nova 'add-component Box RigidBody' | Out-Null
        Invoke-Nova 'add-component Box WebProbe' | Out-Null
        Invoke-Nova 'create audio-source --name Music' | Out-Null
        Invoke-Nova 'set Music --component AudioSource --values "{\"clip\":\"Assets/TestAssets/Audio/long.mp3\",\"loop\":true,\"playOnAwake\":true,\"volume\":0.8}"' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,2,-7 --rotation 10,0,0' | Out-Null
        Invoke-Nova 'scene save --as Assets/Scenes/WebProbe.scene' | Out-Null
        $probeOut = Join-Path $dir 'probe'
        $ex = Invoke-NovaJson "web export --out `"$probeOut`" --scenes Assets/Scenes/WebProbe.scene"
        Add-Result web 'export with C# (.NET player, used BCL only)' ($ex -and $ex.runtime -eq 'dotnet' -and $ex.csharp.frameworkAssembliesRemoved -gt 50 -and $ex.playerBytes -lt 26MB) `
            $(if ($ex) { "runtime $($ex.runtime), player $([math]::Round($ex.playerBytes / 1MB, 1)) MB ($($ex.csharp.frameworkAssembliesRemoved) BCL assemblies left out), data $([math]::Round($ex.dataBytes / 1MB, 1)) MB, $($ex.seconds) s" } else { 'no result' })

        # ---- 2) 브라우저
        $r = RunScene $probeOut (Join-Path $dir 'probe.png') 170 8631
        Add-Result web 'runs in the browser (WebGPU)' ($r -and $r.phase -eq 'running' -and $r.gpuErrors -eq 0 -and $r.fps -ge 20 -and @($r.consoleErrors).Count -eq 0 -and @($r.exceptions).Count -eq 0) `
            $(if ($r) { "phase $($r.phase), $($r.fps) fps, draws $($r.stats.draws), WebGPU errors $($r.gpuErrors), console errors $(@($r.consoleErrors).Count) $(@($r.consoleErrors) -join ' | ')" } else { 'no result (Chrome?)' })
        $log = if ($r) { @($r.scriptLog) -join ' ' } else { '' }
        Add-Result web 'C# scripts run (Start, LINQ, WebGLPlayer)' ($log -match 'WebProbe start sum=385 platform=WebGLPlayer mobile=False') ((@($r.scriptLog) | Where-Object { $_ -match 'WebProbe start' }) -join ' ')
        $y = if ($log -match 'WebProbe frames=150 y=([\-0-9.]+)') { [double]$Matches[1] } else { $null }
        Add-Result web 'physics: the box falls and rests on the ground' ($null -ne $y -and [math]::Abs($y - 0.5) -lt 0.08) "y $y after 150 frames (expect 0.5)"
        Add-Result web 'audio: long mp3 streams to Web Audio' ($r -and $r.audio -and $r.audio.blocks -gt 20 -and $r.audio.peak -gt 0.01) $(if ($r -and $r.audio) { "state $($r.audio.state), blocks $($r.audio.blocks), peak $([math]::Round($r.audio.peak, 3))" } else { 'no audio' })

        # ---- 3) 그림 = DX11 (재질 장면 — 움직이는 것 없음)
        $matOut = Join-Path $dir 'materials'
        $mx = Invoke-NovaJson "web export --out `"$matOut`" --scenes Assets/Scenes/Materials.scene"
        $m = if ($mx) { RunScene $matOut (Join-Path $dir 'materials_web.png') 60 8632 } else { $null }
        if ($m -and $m.size)
        {
            Invoke-Nova 'scene open Assets/Scenes/Materials.scene --force' | Out-Null
            Invoke-Nova 'wait 10' | Out-Null   # 조명 · 볼륨이 다음 프레임에 등록된다 (바로 그리면 해가 빠진 그림)
            $ref = Join-Path $dir 'materials_DirectX11.png'
            Invoke-NovaJson "android reference --out `"$ref`" --width $($m.size[0]) --height $($m.size[1]) --frames 60" | Out-Null
            $c = if (Test-Path $ref) { [NovaImageCompare]::Compare($ref, (Join-Path $dir 'materials_web.png'), (Join-Path $dir 'materials_diff.png')) } else { $null }
            Add-Result web 'WebGPU image = DX11 (Materials)' ($c -and $c[2] -lt 1.0) $(if ($c) { 'max {0}, mean {1:N3}, >8: {2:N2}% ({3} x {4})' -f $c[0], $c[1], $c[2], $m.size[0], $m.size[1] } else { 'no image' })
        }
        else { Add-Result web 'WebGPU image = DX11 (Materials)' $false 'web run failed' }

        # ---- 3b) UI 위치: 가운데 (초록) · 왼쪽 아래 (빨강) 그림 — 웹 캔버스 크기 = DX11 기준 그림 크기 (기준 그림도 Game 뷰가 아닌 그 크기로 레이아웃)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create ui:Image --name Center' | Out-Null
        Invoke-Nova 'create ui:Image --name Corner' | Out-Null
        Invoke-Nova 'set Canvas/Corner --component RectTransform --values "{\"anchorMin\":[0,0],\"anchorMax\":[0,0],\"pivot\":[0,0],\"anchoredPosition\":[0,0],\"sizeDelta\":[60,60]}"' | Out-Null
        Invoke-Nova 'set Canvas/Corner --component UIImage --values "{\"color\":[1,0,0,1]}"' | Out-Null
        Invoke-Nova 'set Canvas/Center --component UIImage --values "{\"color\":[0,1,0,1]}"' | Out-Null
        Invoke-Nova 'scene save --as Assets/Scenes/WebUiProbe.scene' | Out-Null
        $uiOut = Join-Path $dir 'ui'
        $ux = Invoke-NovaJson "web export --out `"$uiOut`" --scenes Assets/Scenes/WebUiProbe.scene"
        $u = if ($ux) { RunScene $uiOut (Join-Path $dir 'ui_web.png') 30 8634 } else { $null }
        if ($u -and $u.size)
        {
            Invoke-Nova 'wait 10' | Out-Null
            $uref = Join-Path $dir 'ui_DirectX11.png'
            Invoke-NovaJson "android reference --out `"$uref`" --width $($u.size[0]) --height $($u.size[1]) --frames 10" | Out-Null
            $c = if (Test-Path $uref) { [NovaImageCompare]::Compare($uref, (Join-Path $dir 'ui_web.png'), (Join-Path $dir 'ui_diff.png')) } else { $null }
            Add-Result web 'UI layout = DX11 (centre, bottom-left)' ($c -and $c[2] -lt 0.5) $(if ($c) { 'max {0}, mean {1:N3}, >8: {2:N2}% ({3} x {4})' -f $c[0], $c[1], $c[2], $u.size[0], $u.size[1] } else { 'no image' })
        }
        else { Add-Result web 'UI layout = DX11 (centre, bottom-left)' $false 'web run failed' }

        # ---- 3c) 2D 빛 (Light 2D · Shadow Caster 2D) = DX11
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'delete "Global Volume"' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,0,-10 --component Camera --values "{\"cameraType\":1,\"orthoSize\":5}"' | Out-Null
        Invoke-Nova 'create empty --name Ground --position 0,0,1 --scale 30,14,1' | Out-Null
        Invoke-Nova 'add-component Ground SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[0.9,0.9,0.95,1]}"' | Out-Null
        Invoke-Nova 'create global-light-2d --name Ambient' | Out-Null
        Invoke-Nova 'set Ambient --component Light2D --values "{\"intensity\":0.25,\"color\":[0.6,0.7,1]}"' | Out-Null
        Invoke-Nova 'create spot-light-2d --name Spot --position -1,0.5,0' | Out-Null
        Invoke-Nova 'set Spot --component Light2D --values "{\"outerRadius\":6,\"intensity\":1.2,\"shadows\":true,\"shadowStrength\":0.9}"' | Out-Null
        Invoke-Nova 'create empty --name Crate --position 2,0,0' | Out-Null
        Invoke-Nova 'add-component Crate SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[0.8,0.4,0.3,1],\"sortingOrder\":1}"' | Out-Null
        Invoke-Nova 'add-component Crate ShadowCaster2D' | Out-Null
        Invoke-Nova 'scene save --as Assets/Scenes/WebLight2D.scene' | Out-Null
        $l2Out = Join-Path $dir 'light2d'
        $lx = Invoke-NovaJson "web export --out `"$l2Out`" --scenes Assets/Scenes/WebLight2D.scene"
        $l2 = if ($lx) { RunScene $l2Out (Join-Path $dir 'light2d_web.png') 30 8635 } else { $null }
        if ($l2 -and $l2.size)
        {
            Invoke-Nova 'wait 10' | Out-Null
            $lref = Join-Path $dir 'light2d_DirectX11.png'
            Invoke-NovaJson "android reference --out `"$lref`" --width $($l2.size[0]) --height $($l2.size[1]) --frames 10" | Out-Null
            $c = if (Test-Path $lref) { [NovaImageCompare]::Compare($lref, (Join-Path $dir 'light2d_web.png'), (Join-Path $dir 'light2d_diff.png')) } else { $null }
            Add-Result web '2D lights + shadows = DX11' ($c -and $c[2] -lt 0.5) $(if ($c) { 'max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'no image' })
        }
        else { Add-Result web '2D lights + shadows = DX11' $false 'web run failed' }

        # ---- 3d) 내비게이션: 3D · 2D NavMesh (엔진에 함께 넣은 com.nova.ai.navigation, C# DllImport NovaNavigation) — 구운 .navmesh 를 읽어 길 · 걷기
        $navScene = New-NavPlayerScene $dir
        if ($navScene)
        {
            $navOut = Join-Path $dir 'nav'
            $nx = Invoke-NovaJson "web export --out `"$navOut`" --scenes $navScene"
            $nr = if ($nx) { RunScene $navOut (Join-Path $dir 'nav_web.png') 600 8636 } else { $null }
            $nl = if ($nr) { @($nr.scriptLog) | ForEach-Object { "$_" } } else { @() }
            $nav = Test-NavPlayerLog $nl 'WebGLPlayer'
            Add-Result web 'navigation paths in the browser (3D wall, 2D Box Collider 2D)' $nav.PathOk $(if ($nav.Start) { $nav.Start } else { "no NavProbe log (phase $($nr.phase), errors $(@($nr.consoleErrors) -join ' | '))" })
            Add-Result web 'navigation agents walk there in the browser' $nav.WalkOk $(if ($nav.Done) { $nav.Done } else { 'no "NavProbe done"' })
        }
        else { Add-Result web 'navigation test scene (bake)' $false 'bake failed' }

        # ---- 3e) 기능: 스킨 천 (치마 · 망토), Starter Assets 차 · 래그돌 표적, 낮 · 밤 (밤 → NightLight) — 안드로이드 (android_player.ps1 -Check features) 와 같은 장면 · 기준
        $featScene = New-PlayerFeatureScene $dir
        if ($featScene)
        {
            $featOut = Join-Path $dir 'features'
            $fx = Invoke-NovaJson "web export --out `"$featOut`" --scenes $featScene"
            $fr = if ($fx) { RunScene $featOut (Join-Path $dir 'features_web.png') 900 8637 } else { $null }
            $fl = if ($fr) { @($fr.scriptLog) | ForEach-Object { "$_" } } else { @() }
            $feat = Test-PlayerFeatureLog $fl 'WebGLPlayer'
            Add-Result web 'features in the browser: scripts run (cloth simulating, car, shot hits, day night)' $feat.StartOk $(if ($feat.Start) { $feat.Start } else { "no FeatureProbe log (phase $($fr.phase), errors $(@($fr.consoleErrors) -join ' | '))" })
            Add-Result web 'features in the browser: skinned cloth, car drives, ragdoll falls, night street light' ($feat.Cloth -and $feat.Car -and $feat.Ragdoll -and $feat.Night) $(if ($feat.Done) { "cloth $($feat.Cloth) car $($feat.Car) ragdoll $($feat.Ragdoll) night $($feat.Night): $($feat.Done)" } else { 'no "FeatureProbe done"' })
        }
        else { Add-Result web 'features test scene' $false 'scene setup failed' }

        # ---- 4) Build Settings 빌드 + 미리 보기 서버
        $buildOut = Join-Path $dir 'build'
        $b = Invoke-NovaJson "web build --out `"$buildOut`" --run --port 8633"
        $url = if ($b) { $b.url } else { $null }
        $head = $null; $outside = 0
        if ($url)
        {
            try { $head = Invoke-WebRequest -UseBasicParsing -Uri "$($url)_framework/dotnet.native.wasm" -Method Head -TimeoutSec 10 } catch {}
            try { $outside = (Invoke-WebRequest -UseBasicParsing -Uri "$($url)%2e%2e/%2e%2e/Windows/win.ini" -TimeoutSec 10).StatusCode } catch { $outside = [int]$_.Exception.Response.StatusCode }
        }
        Add-Result web 'nova web build --run serves the game' ($head -and $head.Headers['Content-Type'] -eq 'application/wasm' -and $head.Headers['Cross-Origin-Embedder-Policy'] -eq 'require-corp' -and $outside -eq 404) `
            "url $url, wasm $($head.Headers['Content-Type']), COEP $($head.Headers['Cross-Origin-Embedder-Policy']), outside the folder $outside, scenes $(@($b.scenes) -join ',')"
        $page = $null
        if ($url)
        {
            $o = & node (Join-Path $Root 'Tools\web\headless.mjs') $url --wait 60 --size 1280x720 --until "window.novaState && (novaState.phase=='error' || (novaState.phase=='running' && nova.frames() > 30))" `
                --eval "JSON.stringify({phase: novaState.phase, frames: nova.frames(), gpu: novaState.gpuErrors, title: document.title})" 2>$null | Out-String
            try { $page = ($o | ConvertFrom-Json).eval | ConvertFrom-Json } catch {}
        }
        Add-Result web 'built game runs from the editor server' ($page -and $page.phase -eq 'running' -and $page.gpu -eq 0) $(if ($page) { "phase $($page.phase), frames $($page.frames), title '$($page.title)'" } else { 'no result' })
        Invoke-Nova 'web stop-server' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        if ($settingsBefore) { [IO.File]::WriteAllBytes($editorSettings, $settingsBefore) }
        Remove-NavPlayerScene
        Remove-PlayerFeatureScene
    }
}

function Suite-Scenes
{
    # 여러 씬 (SceneManager): LoadSceneAsync Additive (allowSceneActivation 0.9 대기 · completed), 같은 씬 두 번, UnloadSceneAsync,
    # SetActiveScene (새 오브젝트가 그 씬으로), DontDestroyOnLoad (Single 로 바꿔도 남고 Awake · Start 다시 없음), 알림 (sceneLoaded · Unloaded · activeSceneChanged),
    # PlayerPrefs (종류 · 한글 · 다음 Play 에도 남음), nova build-scenes
    Write-Host '[scenes]'
    $buildSettings = Join-Path $Project 'ProjectSettings\EditorBuildSettings.json'
    $editorSettings = Join-Path $Project 'Assets\EditorSettings.json'
    $buildBefore = if (Test-Path $buildSettings) { [IO.File]::ReadAllBytes($buildSettings) } else { $null }
    $settingsBefore = if (Test-Path $editorSettings) { [IO.File]::ReadAllBytes($editorSettings) } else { $null }
    $probeDir = Join-Path $Project 'Assets\SceneProbe'
    New-Item -ItemType Directory -Force $probeDir | Out-Null
    $probeFile = Join-Path $probeDir 'SceneProbe.cs'
    $probeBefore = if (Test-Path $probeFile) { Get-Content $probeFile -Raw } else { '' }
    $ed = Start-TestEditor
    try
    {
        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        Copy-Item (Join-Path $PSScriptRoot 'scene_probe.cs') $probeFile -Force
        $probeChanged = (Get-Content $probeFile -Raw) -ne $probeBefore
        $sw2 = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw2.Elapsed.TotalSeconds -lt 90 -and (($inf -and $inf.compiling) -or ($probeChanged -and $now -eq $dllBefore)))

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create empty --name Boot' | Out-Null
        Invoke-Nova 'add-component Boot SceneProbe' | Out-Null
        Invoke-Nova 'scene save --as Assets/Scenes/SceneProbeA.scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name BObject --position 2,0.5,0' | Out-Null
        Invoke-Nova 'create sphere --name BMarker --position -2,0.5,0' | Out-Null
        Invoke-Nova 'scene save --as Assets/Scenes/SceneProbeB.scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name CObject' | Out-Null
        Invoke-Nova 'scene save --as Assets/Scenes/SceneProbeC.scene' | Out-Null
        $bs = Invoke-NovaJson 'build-scenes set --scenes Assets/Scenes/SceneProbeA.scene,Assets/Scenes/SceneProbeB.scene,Assets/Scenes/SceneProbeC.scene'
        Add-Result scenes 'nova build-scenes set' ($bs -and @($bs.scenes).Count -eq 3 -and $bs.scenes[1].path -match 'SceneProbeB') "$(@($bs.scenes | ForEach-Object { "$($_.index):$($_.path)" }) -join ' ')"
        Invoke-Nova 'exec "PlayerPrefs.DeleteAll(); PlayerPrefs.Save(); return PlayerPrefs.HasKey(\"probe.runs\");"' | Out-Null

        function RunProbe
        {
            Invoke-Nova 'scene open Assets/Scenes/SceneProbeA.scene --force' | Out-Null
            Invoke-Nova 'play' | Out-Null
            Invoke-Nova 'wait 240' | Out-Null
            $lines = @((Invoke-Nova 'log -n 1500 --grep "Log: SceneProbe"') -split "\r?\n") | Where-Object { $_ } | ForEach-Object { ($_ -replace '^.*Log: ', '') -replace '\s+\(E:.*$', '' }
            Invoke-Nova 'stop' | Out-Null
            Invoke-Nova 'wait 10' | Out-Null
            return $lines
        }
        function Line($lines, [string]$start) { [string]($lines | Where-Object { "$_" -like "$start*" } | Select-Object -Last 1) }

        $l = RunProbe
        $w = Line $l 'SceneProbe waiting'
        Add-Result scenes 'LoadSceneAsync waits at 0.9 (allowSceneActivation = false)' ("$w" -match 'progress=0\.9 done=False count=1') "$w"
        $a = Line $l 'SceneProbe added'
        Add-Result scenes 'additive scene: count, name, roots, completed, object scene' ("$a" -match 'count=2 b=SceneProbeB loaded=True roots=5 completed=True objScene=SceneProbeB active=SceneProbeA') "$a"
        $m = Line $l 'SceneProbe made'
        Add-Result scenes 'SetActiveScene: new objects go to the active scene' ("$m" -match 'scene=SceneProbeB active=SceneProbeB') "$m"
        $t = Line $l 'SceneProbe twice'
        Add-Result scenes 'same scene added twice (own handle, own objects)' ("$t" -match 'count=3 handlesDiffer=True roots2=5') "$t"
        $u = Line $l 'SceneProbe unloadedB'
        Add-Result scenes 'UnloadSceneAsync destroys only that scene' ("$u" -match 'count=2 bLoaded=False madeAlive=False b2Roots=5 active=SceneProbeA') "$u"
        $s = Line $l 'SceneProbe single'
        Add-Result scenes 'Single load keeps DontDestroyOnLoad (no second Awake / Start)' ("$s" -match 'count=1 active=SceneProbeC mine=DontDestroyOnLoad awakes=1 starts=1 cObject=True bObject=False') "$s"
        $ev = @($l | Where-Object { $_ -match '^SceneProbe (loaded|unloaded|active) ' })
        $evText = $ev -join ' | '
        Add-Result scenes 'sceneLoaded / sceneUnloaded / activeSceneChanged' ($evText -match 'loaded SceneProbeB Additive roots=5' -and $evText -match 'unloaded SceneProbeB' -and $evText -match 'active SceneProbeB -> SceneProbeA' -and $evText -match 'loaded SceneProbeC Single' -and $evText -match 'active SceneProbeA -> SceneProbeC') "$($ev.Count) events"
        $p1 = Line $l 'SceneProbe prefs'
        Add-Result scenes 'PlayerPrefs int / float / Korean string / wrong type = default' ("$p1" -match 'runs=1 f=1\.5 s=가나다 wrongType=-7 has=True path=True') "$p1"
        $l2 = RunProbe
        $p2 = Line $l2 'SceneProbe prefs'
        Add-Result scenes 'PlayerPrefs survive the next Play' ("$p2" -match 'runs=2') "$p2"
        $h = @(Invoke-Nova 'hierarchy') -join ' '
        Add-Result scenes 'Stop restores the starting scene' ($h -match 'Boot' -and $h -notmatch 'CObject' -and $h -notmatch 'BObject') "$h"
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        if ($buildBefore) { [IO.File]::WriteAllBytes($buildSettings, $buildBefore) }
        if ($settingsBefore) { [IO.File]::WriteAllBytes($editorSettings, $settingsBefore) }
    }
}

function Suite-Tween
{
    # com.nova.tween (트윈 패키지): 검사 스크립트 (Tools/tests/tween_probe.cs) 로 곡선 값 · 이동 + 끝 콜백 · 시퀀스 (Append · Join · 간격 · 콜백 차례) ·
    # Yoyo · From · Relative + Incremental · 튀기기 · 흔들기 · 뛰기 · DOVirtual · DelayedCall · async · 대상 지움 · Kill(대상) · 거꾸로 · 빛 · 카메라 · TweenAnimation 컴포넌트
    Write-Host '[tween]'
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $manifestBefore = if (Test-Path $manifest) { [IO.File]::ReadAllBytes($manifest) } else { $null }
    $probeDir = Join-Path $Project 'Assets\TweenProbe'
    New-Item -ItemType Directory -Force $probeDir | Out-Null
    $probeFile = Join-Path $probeDir 'TweenProbe.cs'
    $ed = Start-TestEditor
    try
    {
        $a = Invoke-NovaJson 'package add com.nova.tween'
        Add-Result tween 'package loads (C# only)' ($a -and $a.loaded) "loaded=$($a.loaded)"
        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        Copy-Item (Join-Path $PSScriptRoot 'tween_probe.cs') $probeFile -Force
        $sw2 = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw2.Elapsed.TotalSeconds -lt 90 -and (($inf -and $inf.compiling) -or $now -eq $dllBefore))
        $errs = @((Invoke-Nova 'log -n 300 --grep "TweenProbe.cs("') -split "\r?\n" | Where-Object { $_ -match 'error CS' -and $_ -notmatch "'Tweening'" })
        Add-Result tween 'probe compiles against the package' ($errs.Count -eq 0) "$($errs -join ' | ')"

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create empty --name Probe' | Out-Null
        Invoke-Nova 'add-component Probe TweenProbe' | Out-Null
        Invoke-Nova 'create cube --name Animated' | Out-Null
        Invoke-Nova 'add-component Animated TweenAnimation --values "{\"endValue\":[0,3,0],\"duration\":0.5}"' | Out-Null
        Invoke-Nova 'play' | Out-Null
        $sw3 = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 30' | Out-Null; $doneLine = (Invoke-Nova 'log -n 60 --grep "TweenProbe done"') -join '' }
        while ($sw3.Elapsed.TotalSeconds -lt 30 -and $doneLine -notmatch 'TweenProbe done')
        $l = @((Invoke-Nova 'log -n 2000 --grep "Log: TweenProbe"') -split "\r?\n") | Where-Object { $_ } | ForEach-Object { ($_ -replace '^.*Log: ', '') -replace '\s+\(E:.*$', '' }
        Invoke-Nova 'stop' | Out-Null
        function Line([string]$start) { [string]($l | Where-Object { "$_" -like "$start*" } | Select-Object -Last 1) }

        $x = Line 'TweenProbe ease'
        Add-Result tween 'eases (OutBounce, InOutQuad, OutBack, InCubic)' ("$x" -match 'outBounce=0\.7656 inOutQuad=0\.1250 outBack1=1\.0000 inCubic=0\.1250 linear=0\.30') "$x"
        $x = Line 'TweenProbe move'
        Add-Result tween 'DOMoveX + OnComplete + WaitForCompletion' ("$x" -match 'x=5\.00 completes=1 .* active=False') "$x"
        $x = Line 'TweenProbe sequence'
        Add-Result tween 'Sequence: Append, Join, interval, callback order' ("$x" -match 'pos=0\.00,2\.00,0\.00 scale=2\.00 rotY=90\.00 order=yscr duration=0\.70') "$x"
        $x = Line 'TweenProbe yoyo'
        Add-Result tween 'Yoyo loops come back, OnStepComplete per loop' ("$x" -match 'z=0\.00 steps=2 mid=True') "$x"
        $x = Line 'TweenProbe from'
        Add-Result tween 'From + Relative Incremental loops' ("$x" -match 'start=0\.00 end=1\.00 incremental x=3\.00') "$x"
        $x = Line 'TweenProbe punch'
        Add-Result tween 'punch / shake return, jump lands (and goes up)' ("$x" -match 'punch end=1\.00,1\.00,1\.00 shake end=0\.00,0\.00,0\.00 moved=True jump end=4\.00,0\.00,0\.00 peak=True') "$x"
        $x = Line 'TweenProbe virtual'
        Add-Result tween 'DOVirtual.Float, DelayedCall, AsyncWaitForCompletion' ("$x" -match 'virtual=10\.00 delayed=True task=True') "$x"
        $x = Line 'TweenProbe destroyedTarget'
        Add-Result tween 'destroyed target, Kill(target), PlayBackwards' ("$x" -match 'destroyedTarget=True killByTarget=1 backwards x=0\.00') "$x"
        $x = Line 'TweenProbe light'
        Add-Result tween 'Light.DOIntensity, Camera.DOFieldOfView, TweenAnimation' ("$x" -match 'light=3\.00 fovFrom=60\.00 fov=30\.00 animated=0\.00,3\.00,0\.00') "$x"
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        if ($manifestBefore) { [IO.File]::WriteAllBytes($manifest, $manifestBefore) }
        Remove-Item -Recurse -Force $probeDir -ErrorAction SilentlyContinue
    }
}

function Suite-Light2D
{
    # 2D 빛 (Light 2D · Shadow Caster 2D · 스프라이트 노멀 맵): 직교 카메라로 화면 점 = 월드 점, 흰 바탕의 밝기로 확인 (후처리 없이)
    #  없음 = 그대로, Global 0.5 = 절반, Spot (곧게 줄어듦) = 거리별 값, 반지름 밖 = 바탕, 그림자 (상자 뒤 = 바탕, 반대쪽 = 그대로), 자기 그림자,
    #  원뿔 (위로 90 도), 노멀 맵 (가장자리가 빛 쪽으로 밝아진다), C# Light2D API, CLI global-light-2d · spot-light-2d
    Write-Host '[light2d]'
    $dir = Join-Path $Out 'light2d'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $texDir = Join-Path $Project 'Assets\Light2DTest'
    New-Item -ItemType Directory -Force $texDir | Out-Null
    # 노멀 맵 시험 그림: 왼쪽 반 = 왼쪽을 보는 면, 오른쪽 반 = 오른쪽을 보는 면 (64 x 64)
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap 64, 64
    for ($y = 0; $y -lt 64; $y++) { for ($x = 0; $x -lt 64; $x++) { $bmp.SetPixel($x, $y, $(if ($x -lt 32) { [System.Drawing.Color]::FromArgb(255, 37, 128, 218) } else { [System.Drawing.Color]::FromArgb(255, 218, 128, 218) })) } }
    $bmp.Save((Join-Path $texDir 'split_n.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'delete "Global Volume"' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,0,-10 --component Camera --values "{\"cameraType\":1,\"orthoSize\":5}"' | Out-Null
        Invoke-Nova 'create empty --name Ground --position 0,0,1 --scale 30,14,1' | Out-Null
        Invoke-Nova 'add-component Ground SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[1,1,1,1]}"' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        function Shot([string]$name) { Invoke-Nova 'wait 5' | Out-Null; $p = Join-Path $dir "$name.png"; Invoke-Nova "screenshot `"$p`" --view game" | Out-Null; return $p }
        Add-Type -AssemblyName System.Drawing
        function Px([string]$png, [double]$wx, [double]$wy, [int]$ch = 0)
        {
            $b = New-Object System.Drawing.Bitmap $png
            try { $ppu = $b.Height / 10.0; $c = $b.GetPixel([int]($b.Width / 2 + $wx * $ppu), [int]($b.Height / 2 - $wy * $ppu)); return @($c.R, $c.G, $c.B)[$ch] } finally { $b.Dispose() }
        }
        function Near([int]$v, [int]$want, [int]$tol = 4) { [math]::Abs($v - $want) -le $tol }

        $unlit = Px (Shot 'unlit') 0 0
        Add-Result light2d 'no Light 2D = unlit sprites' (Near $unlit 255) "centre $unlit (expect 255)"
        $c = Invoke-NovaJson 'create global-light-2d --name Ambient'
        Invoke-Nova 'set Ambient --component Light2D --values "{\"intensity\":0.5}"' | Out-Null
        $g = Px (Shot 'global') 0 0
        Add-Result light2d 'Global Light 2D intensity 0.5 = half' ($c -and (Near $g 127)) "centre $g (expect 127)"

        Invoke-Nova 'set Ambient --component Light2D --values "{\"intensity\":0.2}"' | Out-Null
        Invoke-Nova 'create spot-light-2d --name Spot' | Out-Null
        Invoke-Nova 'set Spot --component Light2D --values "{\"outerRadius\":4,\"innerRadius\":0,\"falloff\":0,\"intensity\":1,\"shadows\":true,\"shadowStrength\":1}"' | Out-Null
        $s = Shot 'spot'
        $v2 = Px $s 2 0.3; $v33 = Px $s 3.3 0; $v5 = Px $s 5 0
        Add-Result light2d 'Spot Light 2D falloff (linear) and radius' ((Near $v2 178) -and (Near $v33 95) -and (Near $v5 51)) "d=2: $v2 (178), d=3.3: $v33 (95), outside: $v5 (51 = ambient)"

        Invoke-Nova 'create empty --name Box --position 2,0,0' | Out-Null
        Invoke-Nova 'add-component Box SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[1,0,0,1],\"sortingOrder\":1}"' | Out-Null
        Invoke-Nova 'add-component Box ShadowCaster2D' | Out-Null
        $sh = Shot 'shadow'
        $behind = Px $sh 3.3 0; $mirror = Px $sh -3.3 0; $front = Px $sh 1.6 0
        Add-Result light2d 'Shadow Caster 2D: shadow behind, caster itself lit' ((Near $behind 51) -and (Near $mirror 95) -and (Near $front 203)) "behind $behind (51), mirror $mirror (95), box front $front (203)"
        Invoke-Nova 'set Box --component ShadowCaster2D --values "{\"selfShadows\":true}"' | Out-Null
        $self = Px (Shot 'selfshadow') 1.6 0
        Add-Result light2d 'Self Shadows darkens the caster' (Near $self 51) "box front $self (expect 51)"
        Invoke-Nova 'delete Box' | Out-Null

        Invoke-Nova 'set Spot --component Light2D --values "{\"innerAngle\":80,\"outerAngle\":90}"' | Out-Null
        $cone = Shot 'cone'
        $up = Px $cone 0 2; $down = Px $cone 0 -2
        Add-Result light2d 'spot angle (cone up)' ((Near $up 178) -and (Near $down 51)) "up $up (178), down $down (51)"

        # 노멀 맵: 왼쪽 반은 왼쪽 (-X) 을, 오른쪽 반은 오른쪽을 본다 → 빛이 왼쪽에 있으면 왼쪽 반이 더 밝다
        Invoke-Nova 'set Spot --position -3,0,0 --component Light2D --values "{\"innerAngle\":360,\"outerAngle\":360,\"outerRadius\":8,\"normalMapDistance\":1,\"shadows\":false}"' | Out-Null
        Invoke-Nova 'create empty --name Panel --position 1,0,0 --scale 2,2,1' | Out-Null
        Invoke-Nova 'add-component Panel SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[1,1,1,1],\"sortingOrder\":2,\"normalMap\":\"Assets/Light2DTest/split_n.png\"}"' | Out-Null
        $nm = Shot 'normalmap'
        $left = Px $nm 0.5 0; $right = Px $nm 1.5 0
        Add-Result light2d 'normal map faces turn toward the light' ($left -gt $right + 40) "facing light $left, facing away $right"

        $cs = Join-Path $dir 'light2d_api.cs'
        'var l = GameObject.Find("Spot").GetComponent<NovaEngine.Rendering.Universal.Light2D>(); l.intensity = 0.5f; l.color = new Color(1f, 0f, 0f, 1f); return l.lightType + " " + l.pointLightOuterRadius.ToString("F1") + " " + l.intensity.ToString("F1") + " " + l.shadowsEnabled;' | Set-Content -Encoding utf8 $cs
        $api = Invoke-NovaJson "exec --file `"$cs`""
        $j = Invoke-NovaJson 'get Spot --component Light2D'
        Add-Result light2d 'C# Light2D API (lightType, radius, intensity, color)' ("$($api.result)" -eq 'Point 8.0 0.5 False' -and $j.intensity -eq 0.5 -and $j.color[1] -eq 0) "$($api.result); json intensity $($j.intensity), color $($j.color -join ',')"
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item -Recurse -Force $texDir -ErrorAction SilentlyContinue
        Remove-Item -Force "$texDir.meta" -ErrorAction SilentlyContinue
    }
}

function Suite-Nav2D
{
    # 2D 내비게이션 (NavMesh Surface 의 Plane = 2D (XY)): 바닥 스프라이트 범위 + 정적 2D 콜라이더가 벽
    #  Box Collider 2D 벽을 돌아가는 길 (z = 0), SamplePosition (벽 안 → 벽 밖), 에이전트가 XY 로 걸어 도착 (z · 회전 그대로),
    #  움직이는 Rigidbody 2D 는 굽지 않는다, Edge Collider 2D 선도 벽, 굽기 파일을 다시 읽어도 2D
    Write-Host '[nav2d]'
    $dir = Join-Path $Out 'nav2d'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $before = if (Test-Path $manifest) { Get-Content $manifest -Raw } else { $null }
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'package add com.nova.ai.navigation' | Out-Null
        Wait-Compile
        foreach ($l in @('scene new --force', 'delete "Global Volume"',
                         'set "Main Camera" --position 0,0,-10 --component Camera --values "{\"cameraType\":1,\"orthoSize\":7}"',
                         'create empty --name NGround --position 0,0,1 --scale 20,14,1',
                         'add-component NGround SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[0.85,0.85,0.8,1]}"',
                         'create empty --name NWall --position 0,0,0 --scale 0.5,8,1',
                         'add-component NWall SpriteRenderer --values "{\"sprite\":\"builtin:Square\",\"color\":[0.3,0.3,0.35,1],\"sortingOrder\":1}"',
                         'add-component NWall BoxCollider2D',
                         'create empty --name NSurface', 'add-component NSurface NavMeshSurface --values "{\"plane\":1}"',
                         'create empty --name NNpc --position -5,0,-0.5',
                         'add-component NNpc SpriteRenderer --values "{\"sprite\":\"builtin:Circle\",\"color\":[0.9,0.3,0.2,1],\"sortingOrder\":2}"',
                         'add-component NNpc NavMeshAgent --values "{\"radius\":0.3,\"speed\":4,\"acceleration\":20}"')) { Invoke-Nova $l | Out-Null }

        $pathCs = Join-Path $dir 'nav2d_path.cs'
        'GameObject.Find("NSurface").GetComponent<NovaEngine.AI.NavMeshSurface>().BuildNavMesh(); var p = new NovaEngine.AI.NavMeshPath(); NovaEngine.AI.NavMesh.CalculatePath(new Vector3(-5,0,0), new Vector3(5,0,0), NovaEngine.AI.NavMesh.AllAreas, p); float my = 0, mz = 0; foreach (var c in p.corners) { my = Mathf.Max(my, Mathf.Abs(c.y)); mz = Mathf.Max(mz, Mathf.Abs(c.z)); } return p.corners.Length + " " + my.ToString("F2") + " " + mz.ToString("F2");' | Set-Content -Encoding utf8 $pathCs
        $r = Invoke-NovaJson "exec --file `"$pathCs`""
        $v = if ($r) { "$($r.result)" -split ' ' } else { @() }
        Add-Result nav2d '2D bake: path goes around the Box Collider 2D wall (XY)' ($v.Count -eq 3 -and [int]$v[0] -ge 3 -and [double]$v[1] -gt 4.0 -and [double]$v[2] -lt 0.01) "corners $($v[0]), max |y| $($v[1]), max |z| $($v[2]) (expect ≥ 3, > 4, 0)"

        $sampleCs = Join-Path $dir 'nav2d_sample.cs'
        'NovaEngine.AI.NavMeshHit h; bool b = NovaEngine.AI.NavMesh.SamplePosition(new Vector3(0.1f, 1, 0), out h, 3f, NovaEngine.AI.NavMesh.AllAreas); return b + " " + Mathf.Abs(h.position.x).ToString("F2") + " " + h.position.y.ToString("F2");' | Set-Content -Encoding utf8 $sampleCs
        $s = Invoke-NovaJson "exec --file `"$sampleCs`""
        $sv = if ($s) { "$($s.result)" -split ' ' } else { @() }
        Add-Result nav2d 'SamplePosition inside the wall → nearest walkable point' ($sv.Count -eq 3 -and $sv[0] -eq 'True' -and [double]$sv[1] -ge 0.5 -and [math]::Abs([double]$sv[2] - 1) -lt 0.2) "hit $($sv[0]), |x| $($sv[1]) (≥ 0.25 + radius), y $($sv[2]) (≈ 1)"

        # 기즈모 사진: Scene 뷰를 XY 평면 정면으로 (표면을 고르면 파란 내비 메시)
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'camera --position 0,0,-17 --target 0,0,0' | Out-Null
        Invoke-Nova 'select NSurface' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        Invoke-Nova "screenshot `"$(Join-Path $dir 'nav2d_scene.png')`" --view editor" | Out-Null

        Invoke-Nova 'play' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $goCs = Join-Path $dir 'nav2d_go.cs'
        'return GameObject.Find("NNpc").GetComponent<NovaEngine.AI.NavMeshAgent>().SetDestination(new Vector3(5, 0, 0));' | Set-Content -Encoding utf8 $goCs
        Invoke-Nova "exec --file `"$goCs`"" | Out-Null
        $midCs = Join-Path $dir 'nav2d_mid.cs'
        'var a = GameObject.Find("NNpc").GetComponent<NovaEngine.AI.NavMeshAgent>(); var v = a.velocity; return a.hasPath + " " + Mathf.Abs(v.z).ToString("F2") + " " + (new Vector2(v.x, v.y)).magnitude.ToString("F2");' | Set-Content -Encoding utf8 $midCs
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 0.6) { Invoke-Nova 'wait 5' | Out-Null }
        $mid = Invoke-NovaJson "exec --file `"$midCs`""
        # 걷는 중: 에이전트를 고르면 내비 메시 + 남은 길 (노랑) — 편집기 창 전체로 (기즈모는 창의 겹침 그림)
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'select NNpc' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        Invoke-Nova "screenshot `"$(Join-Path $dir 'nav2d_walk.png')`" --view editor" | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 6) { Invoke-Nova 'wait 20' | Out-Null }
        $np = Invoke-NovaJson 'get NNpc'
        $p = $np.position; $rot = $np.rotation
        $mv = if ($mid) { "$($mid.result)" -split ' ' } else { @() }
        $rotOk = $rot -and ([math]::Abs([double]$rot[0]) + [math]::Abs([double]$rot[1]) + [math]::Abs([double]$rot[2])) -lt 0.01
        Add-Result nav2d 'agent walks on XY to the destination (z, rotation kept)' ([math]::Abs([double]$p[0] - 5) -lt 0.2 -and [math]::Abs([double]$p[1]) -lt 0.2 -and [math]::Abs([double]$p[2] + 0.5) -lt 0.001 -and $rotOk) ("NPC {0:F2}, {1:F2}, {2:F2} (expect 5, 0, -0.5), rotation {3}" -f [double]$p[0], [double]$p[1], [double]$p[2], ($rot -join ','))
        Add-Result nav2d 'velocity is in XY while walking' ($mv.Count -eq 3 -and $mv[0] -eq 'True' -and [double]$mv[1] -lt 0.001 -and [double]$mv[2] -gt 1.0) "hasPath $($mv[0]), |vz| $($mv[1]), |vxy| $($mv[2])"
        Invoke-Nova 'stop' | Out-Null

        # 움직이는 Rigidbody 2D (Dynamic) 는 굽지 않는다 → 곧은 길
        Invoke-Nova 'add-component NWall Rigidbody2D --values "{\"bodyType\":0,\"gravityScale\":0}"' | Out-Null
        $r = Invoke-NovaJson "exec --file `"$pathCs`""
        $v = if ($r) { "$($r.result)" -split ' ' } else { @() }
        Add-Result nav2d 'dynamic Rigidbody 2D colliders are not baked' ($v.Count -eq 3 -and [int]$v[0] -eq 2) "corners $($v[0]) (expect 2)"
        Invoke-Nova 'remove-component NWall Rigidbody2D' | Out-Null

        # Edge Collider 2D 선도 벽 (상자 벽 대신) — 칸 가운데 사이 (x = 0.13) 의 얇은 선도 빠지지 않는다
        Invoke-Nova 'remove-component NWall BoxCollider2D' | Out-Null
        Invoke-Nova 'create empty --name NEdge' | Out-Null
        Invoke-Nova 'add-component NEdge EdgeCollider2D --values "{\"points\":[[0.13,-4],[0.13,4]]}"' | Out-Null
        $r = Invoke-NovaJson "exec --file `"$pathCs`""
        $v = if ($r) { "$($r.result)" -split ' ' } else { @() }
        Add-Result nav2d 'Edge Collider 2D line is a wall' ($v.Count -eq 3 -and [int]$v[0] -ge 3 -and [double]$v[1] -gt 4.0) "corners $($v[0]), max |y| $($v[1]) (expect ≥ 3, > 4)"

        # 굽기 파일을 다시 읽어도 2D (파일 머리의 플래그): 씬 저장 → 다시 열기 → 굽지 않고 길 찾기
        $scenePath = 'Assets/Nav2DTest.scene'
        Invoke-Nova "scene save --as `"$scenePath`"" | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova "scene open `"$scenePath`"" | Out-Null
        $reCs = Join-Path $dir 'nav2d_reload.cs'
        'var p = new NovaEngine.AI.NavMeshPath(); NovaEngine.AI.NavMesh.CalculatePath(new Vector3(-5,0,0), new Vector3(5,0,0), NovaEngine.AI.NavMesh.AllAreas, p); float my = 0; foreach (var c in p.corners) my = Mathf.Max(my, Mathf.Abs(c.y)); return p.corners.Length + " " + my.ToString("F2");' | Set-Content -Encoding utf8 $reCs
        $r = Invoke-NovaJson "exec --file `"$reCs`""
        $v = if ($r) { "$($r.result)" -split ' ' } else { @() }
        Add-Result nav2d 'saved 2D navmesh reloads as 2D' ($v.Count -eq 2 -and [int]$v[0] -ge 3 -and [double]$v[1] -gt 4.0) "corners $($v[0]), max |y| $($v[1]) (expect ≥ 3, > 4)"
        # Tilemap Collider 2D 로 둘러싼 방: 안은 걸을 수 있고 (곧은 길), 밖으로 나가는 길은 없다
        Invoke-Nova 'package add com.nova.tilemap' | Out-Null
        $tileDir = Join-Path $Project 'Assets\Nav2DTiles'
        New-Item -ItemType Directory -Force $tileDir | Out-Null
        Add-Type -AssemblyName System.Drawing
        $bmp = New-Object System.Drawing.Bitmap 16, 16
        for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, 90, 90, 100)) } }
        $bmp.Save((Join-Path $tileDir 'Wall.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        Invoke-Nova 'sprite-slice Assets/Nav2DTiles/Wall.png --mode grid --cell 16,16 --ppu 16 --filter point' | Out-Null
        $ft = Invoke-NovaJson 'tilemap tile.fromtexture --texture Assets/Nav2DTiles/Wall.png --folder Assets\Nav2DTiles\Palette --collider grid'
        $wallTile = @($ft.tiles)[0]
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'tilemap create --name Walls --collider' | Out-Null
        Invoke-Nova "tilemap box --tilemap Walls --from -6,-4 --to 5,3 --tile `"$wallTile`"" | Out-Null
        Invoke-Nova 'tilemap box --tilemap Walls --from -5,-3 --to 4,2' | Out-Null
        Invoke-Nova 'create empty --name NSurface' | Out-Null
        Invoke-Nova 'add-component NSurface NavMeshSurface --values "{\"plane\":1}"' | Out-Null
        $roomCs = Join-Path $dir 'nav2d_room.cs'
        'GameObject.Find("NSurface").GetComponent<NovaEngine.AI.NavMeshSurface>().BuildNavMesh(); var p = new NovaEngine.AI.NavMeshPath(); NovaEngine.AI.NavMesh.CalculatePath(new Vector3(-3,0,0), new Vector3(3,0,0), NovaEngine.AI.NavMesh.AllAreas, p); var q = new NovaEngine.AI.NavMeshPath(); bool o = NovaEngine.AI.NavMesh.CalculatePath(new Vector3(0,0,0), new Vector3(-6.8f,0,0), NovaEngine.AI.NavMesh.AllAreas, q); string last = o && q.corners.Length > 0 ? q.corners[q.corners.Length - 1].x.ToString("F2") : "none"; return p.corners.Length + " " + last;' | Set-Content -Encoding utf8 $roomCs
        $r = Invoke-NovaJson "exec --file `"$roomCs`""
        $v = if ($r) { "$($r.result)" -split ' ' } else { @() }
        $outOk = $v.Count -eq 2 -and ($v[1] -eq 'none' -or [double]$v[1] -gt -5)
        Add-Result nav2d 'Tilemap Collider 2D room: inside walkable, no way out' ($v.Count -eq 2 -and [int]$v[0] -eq 2 -and $outOk) "inside corners $($v[0]) (expect 2), path out ends at x $($v[1]) (expect none or > -5)"
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'package remove com.nova.tilemap' | Out-Null
        Invoke-Nova 'package remove com.nova.ai.navigation' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        if ($before) { [IO.File]::WriteAllText($manifest, $before) }
        foreach ($f in @('Assets\Nav2DTest.scene', 'Assets\Nav2DTest.scene.meta', 'Assets\NavMesh-NSurface.navmesh', 'Assets\NavMesh-NSurface.navmesh.meta', 'Assets\Nav2DTiles.meta'))
        {
            Remove-Item (Join-Path $Project $f) -Force -ErrorAction SilentlyContinue
        }
        Remove-Item -Recurse -Force (Join-Path $Project 'Assets\Nav2DTiles') -ErrorAction SilentlyContinue
    }
}

function Suite-Ragdoll
{
    # 래그돌 · 3D 관절: 검사 스크립트 (Tools/tests/joints3d_probe.cs) — Character Joint 흔들기 1 · 2 · 비틀기 한계 · 끊어짐,
    #  Configurable Joint 선 한계 · X 드라이브 · Slerp 드라이브, 겹친 다이내믹 바디 (부모 먼저 쓰기).
    #  Ragdoll Wizard (기본 캐릭터): 바디 11 개 · 관절 연결 · 질량, Play → 쓰러져 바닥 위에, 무릎은 뒤로만, 꺼짐 = 애니메이션 따라감 (키네마틱) → C# 으로 켜면 쓰러짐
    Write-Host '[ragdoll]'
    $dir = Join-Path $Out 'ragdoll'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $manifestBefore = if (Test-Path $manifest) { [IO.File]::ReadAllBytes($manifest) } else { $null }
    $probeDir = Join-Path $Project 'Assets\JointsProbe'
    New-Item -ItemType Directory -Force $probeDir | Out-Null
    $probeFile = Join-Path $probeDir 'Joints3DProbe.cs'
    $ed = Start-TestEditor
    try
    {
        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        Copy-Item (Join-Path $PSScriptRoot 'joints3d_probe.cs') $probeFile -Force
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw.Elapsed.TotalSeconds -lt 90 -and (($inf -and $inf.compiling) -or $now -eq $dllBefore))
        $errs = @((Invoke-Nova 'log -n 300 --grep "Joints3DProbe.cs("') -split "\r?\n" | Where-Object { $_ -match 'error CS' })
        Add-Result ragdoll 'probe compiles (CharacterJoint, ConfigurableJoint C# API)' ($errs.Count -eq 0) "$($errs -join ' | ')"

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create empty --name Probe' | Out-Null
        Invoke-Nova 'add-component Probe Joints3DProbe' | Out-Null
        Invoke-Nova 'play' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 30' | Out-Null; $doneLine = (Invoke-Nova 'log -n 60 --grep "Joints3DProbe done"') -join '' }
        while ($sw.Elapsed.TotalSeconds -lt 30 -and $doneLine -notmatch 'Joints3DProbe done')
        $l = @((Invoke-Nova 'log -n 400 --grep "Log: Joints3DProbe"') -split "\r?\n") | Where-Object { $_ } | ForEach-Object { ($_ -replace '^.*Log: ', '') -replace '\s+\(E:.*$', '' }
        Invoke-Nova 'stop' | Out-Null
        function Line([string]$start) { [string]($l | Where-Object { "$_" -like "$start*" } | Select-Object -Last 1) }
        function Num([string]$line, [string]$key) { if ($line -match "$key=(-?[\d.]+)") { [double]$Matches[1] } else { [double]::NaN } }

        $x = Line 'Joints3DProbe swing'
        $s1 = Num $x 'swing1'; $s2 = Num $x 'swing2'; $fr = Num $x 'free'
        Add-Result ragdoll 'Character Joint swing 1 / swing 2 limits (free = 60)' ([math]::Abs($s1 - 30) -lt 3 -and [math]::Abs($s2 - 15) -lt 3 -and [math]::Abs($fr - 60) -lt 3) "$x (expect 30, 15, 60)"
        $x = Line 'Joints3DProbe twist'
        Add-Result ragdoll 'Character Joint twist limit, break force + OnJointBreak' ([math]::Abs((Num $x 'twist') - 10) -lt 3 -and "$x" -match 'break=1 jointGone=True') "$x (expect twist 10)"
        $x = Line 'Joints3DProbe configurable'
        $ly = Num $x 'linearY'; $lx = Num $x 'linearX'; $dx = Num $x 'driveX'; $rx = Num $x 'rotX'
        Add-Result ragdoll 'Configurable Joint: linear limit, X drive (target inverted like Unity), slerp drive' ([math]::Abs($ly - 5.5) -lt 0.05 -and [math]::Abs($lx - 10) -lt 0.01 -and [math]::Abs($dx - 11) -lt 0.05 -and [math]::Abs($rx + 30) -lt 3) "$x (expect 5.5, 10, 11, -30)"
        $x = Line 'Joints3DProbe nested'
        Add-Result ragdoll 'nested dynamic bodies keep their joint gap while falling' ([math]::Abs((Num $x 'gap') - 1.5) -lt 0.05 -and "$x" -match 'parentFell=True') "$x (expect 1.5)"

        # Ragdoll Wizard: 기본 캐릭터 + 바닥
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 20,1,20' | Out-Null
        Invoke-Nova 'create character --name RCh' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $c = Invoke-NovaJson 'ragdoll create RCh'
        $joints = @($c.parts | ForEach-Object { "$($_.bone -replace '^.*:', '')<$($_.joint)" }) -join ' '
        $knee = $c.parts | Where-Object { $_.body -like '*Left Knee' }
        Add-Result ragdoll 'Ragdoll Wizard: 11 bodies, 20 kg, joints chained (knee → hips → pelvis)' ($c -and $c.parts.Count -eq 11 -and [math]::Abs($c.mass - 20) -lt 0.01 -and $knee.joint -eq 'Left Hips') "bodies $($c.parts.Count), mass $($c.mass); $joints"
        Invoke-Nova 'set "Main Camera" --position 1.9,1.3,1.9 --rotation 20,-135,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        Invoke-Nova "screenshot `"$(Join-Path $dir 'ragdoll_stand.png')`" --view game" | Out-Null

        Invoke-Nova 'play' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 3.5) { Invoke-Nova 'wait 20' | Out-Null }
        $i = Invoke-NovaJson 'ragdoll info RCh'
        Invoke-Nova "screenshot `"$(Join-Path $dir 'ragdoll_fallen.png')`" --view game" | Out-Null
        $pelvis = $i.parts | Where-Object { $_.body -like '*Pelvis' }
        $minY = ($i.parts | ForEach-Object { [double]$_.position[1] } | Measure-Object -Minimum).Minimum
        $dyn = @($i.parts | Where-Object { -not $_.kinematic }).Count
        Add-Result ragdoll 'Play: the ragdoll falls and rests on the ground' ($i.active -and $dyn -eq 11 -and [double]$pelvis.position[1] -lt 0.5 -and $minY -gt -0.05) ("pelvis y {0:F2} (< 0.5), lowest body y {1:F2} (> -0.05), dynamic {2}" -f [double]$pelvis.position[1], $minY, $dyn)
        $kneeCs = Join-Path $dir 'ragdoll_knee.cs'
        'float K(string a, string b) { var q = Quaternion.Inverse(GameObject.Find(a).transform.rotation) * GameObject.Find(b).transform.rotation; return Mathf.DeltaAngle(0, q.eulerAngles.x); } return K("Left Hips", "Left Knee").ToString("F1") + " " + K("Right Hips", "Right Knee").ToString("F1");' | Set-Content -Encoding utf8 $kneeCs
        $k = Invoke-NovaJson "exec --file `"$kneeCs`""
        $kv = if ($k) { "$($k.result)" -split ' ' } else { @() }
        Add-Result ragdoll 'knees bend only backwards (-80 .. 0)' ($kv.Count -eq 2 -and [double]$kv[0] -le 2 -and [double]$kv[0] -ge -83 -and [double]$kv[1] -le 2 -and [double]$kv[1] -ge -83) "left $($kv[0]), right $($kv[1])"
        Invoke-Nova 'stop' | Out-Null

        # 꺼짐: 바디가 애니메이션 자세를 따라간다 (키네마틱, 서 있다) → C# 으로 켜면 쓰러진다
        Invoke-Nova 'ragdoll active RCh --value false' | Out-Null
        Invoke-Nova 'play' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 1.5) { Invoke-Nova 'wait 20' | Out-Null }
        $i = Invoke-NovaJson 'ragdoll info RCh'
        $pelvis = $i.parts | Where-Object { $_.body -like '*Pelvis' }
        $kin = @($i.parts | Where-Object { $_.kinematic }).Count
        $standY = [double]$pelvis.position[1]
        $onCs = Join-Path $dir 'ragdoll_on.cs'
        'var r = GameObject.Find("RCh").GetComponent<Ragdoll>(); bool before = r.active; r.active = true; return before + " " + r.bodyCount;' | Set-Content -Encoding utf8 $onCs
        $on = Invoke-NovaJson "exec --file `"$onCs`""
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 3) { Invoke-Nova 'wait 20' | Out-Null }
        $i2 = Invoke-NovaJson 'ragdoll info RCh'
        $fallY = [double](($i2.parts | Where-Object { $_.body -like '*Pelvis' }).position[1])
        $kin2 = @($i2.parts | Where-Object { $_.kinematic }).Count
        $rdLog = ((Invoke-Nova 'log -n 40 --grep "Ragdoll"') -split "?
" | Where-Object { $_ -match 'bodies' } | Select-Object -Last 2) -join ' / '
        Add-Result ragdoll 'inactive = kinematic bodies follow the animation; C# active = true → falls' (-not $i.active -and $kin -eq 11 -and $standY -gt 0.7 -and "$($on.result)" -eq 'False 11' -and $fallY -lt 0.5) ("kinematic {0}, standing pelvis y {1:F2} (> 0.7), C# '{2}', after y {3:F2} (< 0.5), after: active {4} kinematic {5}; {6}" -f $kin, $standY, $on.result, $fallY, $i2.active, $kin2, $rdLog)
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        if ($manifestBefore) { [IO.File]::WriteAllBytes($manifest, $manifestBefore) }
        Remove-Item -Recurse -Force $probeDir -ErrorAction SilentlyContinue
        Remove-Item -Force "$probeDir.meta" -ErrorAction SilentlyContinue
    }
}

function Suite-Wheel
{
    # 차량 (Wheel Collider): 검사 스크립트 (Tools/tests/wheel_probe.cs) 가 C# 으로 차 (1500 kg, 바퀴 4) 를 만든다 —
    #  쉬기 (차 높이 = 반지름 + 쉬는 서스펜션, 매달린 질량 375, 하중 ≈ 375 g, 바닥 = Ground), 뒷바퀴 모터 (앞으로, 곧게, rpm = 구르는 속도),
    #  브레이크 (멈춤), 앞바퀴 조향 25 도 (오른쪽으로 돈다, 바퀴 그림 요 = 차 + 25)
    Write-Host '[wheel]'
    $dir = Join-Path $Out 'wheel'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $probeDir = Join-Path $Project 'Assets\WheelProbe'
    New-Item -ItemType Directory -Force $probeDir | Out-Null
    $probeFile = Join-Path $probeDir 'WheelProbe.cs'
    $ed = Start-TestEditor
    try
    {
        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        Copy-Item (Join-Path $PSScriptRoot 'wheel_probe.cs') $probeFile -Force
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw.Elapsed.TotalSeconds -lt 90 -and (($inf -and $inf.compiling) -or $now -eq $dllBefore))
        $errs = @((Invoke-Nova 'log -n 300 --grep "WheelProbe.cs("') -split "\r?\n" | Where-Object { $_ -match 'error CS' })
        Add-Result wheel 'probe compiles (WheelCollider C# API)' ($errs.Count -eq 0) "$($errs -join ' | ')"

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 200,1,200' | Out-Null
        Invoke-Nova 'create empty --name Probe' | Out-Null
        Invoke-Nova 'add-component Probe WheelProbe' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'play' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 30' | Out-Null; $doneLine = (Invoke-Nova 'log -n 60 --grep "WheelProbe done"') -join '' }
        while ($sw.Elapsed.TotalSeconds -lt 60 -and $doneLine -notmatch 'WheelProbe done')
        Invoke-Nova "screenshot `"$(Join-Path $dir 'wheel_turn.png')`" --view game" | Out-Null
        $l = @((Invoke-Nova 'log -n 400 --grep "Log: WheelProbe"') -split "\r?\n") | Where-Object { $_ } | ForEach-Object { ($_ -replace '^.*Log: ', '') -replace '\s+\(E:.*$', '' }
        Invoke-Nova 'stop' | Out-Null
        function Line([string]$start) { [string]($l | Where-Object { "$_" -like "$start*" } | Select-Object -Last 1) }
        function Num([string]$line, [string]$key) { if ($line -match "$key=(-?[\d.]+)") { [double]$Matches[1] } else { [double]::NaN } }

        $x = Line 'WheelProbe rest'
        Add-Result wheel 'rest: suspension at target, sprung mass, load, ground hit' ([math]::Abs((Num $x 'carY') - 0.55) -lt 0.03 -and (Num $x 'grounded') -eq 4 -and (Num $x 'sprung') -eq 375 -and [math]::Abs((Num $x 'load') - 3679) -lt 370 -and "$x" -match 'ground=Ground' -and [math]::Abs((Num $x 'wheelY') - 0.4) -lt 0.03) "$x (expect carY 0.55, 4, 375, ~3679 N, wheelY 0.40)"
        $x = Line 'WheelProbe drive'
        $rpm = Num $x 'rpm'; $exp = Num $x 'expectRpm'
        Add-Result wheel 'motor torque drives forward, straight, wheels roll' ((Num $x 'speed') -gt 3 -and (Num $x 'side') -lt 0.3 -and $exp -gt 0 -and [math]::Abs($rpm - $exp) -lt 0.15 * $exp) "$x (expect speed > 3, side < 0.3, rpm ≈ expect)"
        $x = Line 'WheelProbe brake'
        Add-Result wheel 'brake torque stops the car (and it stays still)' ((Num $x 'speed') -lt 0.02 -and (Num $x 'av') -lt 0.02) "$x (expect speed, av < 0.02)"
        $x = Line 'WheelProbe steer'
        Add-Result wheel 'steer angle turns right, wheel pose follows the steering' ((Num $x 'heading') -gt 15 -and (Num $x 'dx') -gt 0.5 -and [math]::Abs((Num $x 'wheelYaw') - 25) -lt 2) "$x (expect heading > 15, dx > 0.5, wheelYaw 25)"
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item -Recurse -Force $probeDir -ErrorAction SilentlyContinue
        Remove-Item -Force "$probeDir.meta" -ErrorAction SilentlyContinue
    }
}

function Suite-DayNight
{
    # 낮 · 밤 순환 (com.nova.daynight): 시각 → 단계 이름, 해 (정오) · 달 (자정) 방향의 Directional Light,
    #  하늘: 낮 밝기, 노을 · 새벽 지평이 붉다, 밤은 어둡다, 은하수 시각에 별이 많다, Play 중 7 단계가 차례로, C# DayNight API,
    #  거리: 구운 반사 프로브가 시각마다 다시 찍힌다 (밤 반사가 두 번 어두워지지 않는다), NightLight 가로등이 밤에만 켜진다,
    #  Adaptive Probe Volume 이 모은 하늘이 시각을 따라 어두워진다
    Write-Host '[daynight]'
    $dir = Join-Path $Out 'daynight'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $before = if (Test-Path $manifest) { Get-Content $manifest -Raw } else { $null }
    $ed = Start-TestEditor
    try
    {
        $a = Invoke-NovaJson 'package add com.nova.daynight'
        Add-Result daynight 'package loads (NovaDayNight.dll)' ($a -and $a.loaded) "loaded=$($a.loaded)"
        Wait-Compile
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create plane --name Ground --scale 20,1,20' | Out-Null
        Invoke-Nova 'create empty --name TimeOfDay' | Out-Null
        Invoke-Nova 'add-component TimeOfDay DayNightCycle' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.6,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null

        # 단계 이름
        $names = foreach ($h in @(5, 7, 12, 17, 18.5, 20, 0)) { (Invoke-NovaJson "daynight set --time $h").phase }
        $nameStr = $names -join ','
        Add-Result daynight 'phase by hour (5 7 12 17 18.5 20 0)' ($nameStr -eq 'Dawn,Morning,Day,Evening,Sunset,Night,MilkyWay') $nameStr

        # 빛 방향: 정오 = 해가 위에서 (빛이 아래로), 자정 = 해는 아래, 달이 위에서
        $noon = Invoke-NovaJson 'daynight set --time 12'
        $mid = Invoke-NovaJson 'daynight set --time 0'
        Add-Result daynight 'Directional Light = sun at noon, moon at midnight' ([double]$noon.sunElevation -gt 55 -and [double]$noon.lightForward[1] -lt -0.8 -and [double]$mid.sunElevation -lt -55 -and [double]$mid.lightForward[1] -lt -0.8 -and $mid.moon -gt 0.9) ("noon sun {0:F0} deg, light y {1:F2}; midnight sun {2:F0} deg, light y {3:F2}, moon {4}" -f [double]$noon.sunElevation, [double]$noon.lightForward[1], [double]$mid.sunElevation, [double]$mid.lightForward[1], $mid.moon)

        Add-Type -AssemblyName System.Drawing
        function Shot([string]$name, [double]$time, [string]$rot)
        {
            Invoke-Nova "daynight set --time $time" | Out-Null
            Invoke-Nova "set `"Main Camera`" --rotation $rot" | Out-Null
            Invoke-Nova 'wait 6' | Out-Null
            $p = Join-Path $dir "$name.png"
            Invoke-Nova "screenshot `"$p`" --view game" | Out-Null
            return $p
        }
        # 화면 띠의 평균 (r, g, b) · 밝은 점 수 — y0..y1 은 화면 높이의 비율
        function Stats([string]$png, [double]$y0, [double]$y1)
        {
            $b = New-Object System.Drawing.Bitmap $png
            try
            {
                $r = 0.0; $g = 0.0; $bl = 0.0; $n = 0; $bright = 0
                for ($y = [int]($b.Height * $y0); $y -lt [int]($b.Height * $y1); $y += 2)
                {
                    for ($x = 0; $x -lt $b.Width; $x += 2)
                    {
                        $c = $b.GetPixel($x, $y); $r += $c.R; $g += $c.G; $bl += $c.B; $n++
                        if (($c.R + $c.G + $c.B) / 3 -gt 120) { $bright++ }
                    }
                }
                return [pscustomobject]@{ R = $r / $n; G = $g / $n; B = $bl / $n; L = ($r + $g + $bl) / (3 * $n); Bright = $bright }
            }
            finally { $b.Dispose() }
        }

        $day = Stats (Shot 'day' 13 '-15,180,0') 0.0 0.35
        $sunset = Stats (Shot 'sunset' 18.9 '-4,-90,0') 0.38 0.5
        $dawn = Stats (Shot 'dawn' 5.6 '-4,90,0') 0.38 0.5
        $night = Stats (Shot 'night' 21.25 '-15,180,0') 0.0 0.35
        $mw = Stats (Shot 'milkyway' 1.75 '-55,180,0') 0.0 0.6
        Add-Result daynight 'day sky is bright, night sky is dark' ($day.L -gt 90 -and $night.L -lt $day.L * 0.2) ("day {0:F0}, night {1:F0}" -f $day.L, $night.L)
        Add-Result daynight 'sunset (west) and dawn (east) horizons glow red' ($sunset.R -gt $sunset.B * 1.3 -and $dawn.R -gt $dawn.B * 1.2) ("sunset r {0:F0} b {1:F0}; dawn r {2:F0} b {3:F0}" -f $sunset.R, $sunset.B, $dawn.R, $dawn.B)
        Add-Result daynight 'Milky Way night: dark sky full of stars' ($mw.L -lt 60 -and $mw.Bright -gt 40) ("mean {0:F0}, bright points {1}" -f $mw.L, $mw.Bright)

        # Play: 6 초에 하루 → 단계가 차례로 바뀐다
        Invoke-Nova 'daynight set --time 4.6 --minutes 0.1' | Out-Null
        Invoke-Nova 'play' | Out-Null
        $seen = New-Object System.Collections.Generic.List[string]
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 9)
        {
            $s = Invoke-NovaJson 'daynight status'
            if ($s -and ($seen.Count -eq 0 -or $seen[$seen.Count - 1] -ne $s.phase)) { $seen.Add($s.phase) }
            Invoke-Nova 'wait 3' | Out-Null
        }
        $order = @('Dawn', 'Morning', 'Day', 'Evening', 'Sunset', 'Night', 'MilkyWay')
        $ok = $seen.Count -ge 7
        for ($i = 1; $i -lt $seen.Count -and $ok; $i++) { $ok = ($order.IndexOf($seen[$i]) -eq ($order.IndexOf($seen[$i - 1]) + 1) % 7) }
        Add-Result daynight 'Play: phases follow each other (dawn → … → milky way → dawn)' $ok ($seen -join ' > ')
        $csf = Join-Path $dir 'daynight_api.cs'
        'DayNight.paused = true; DayNight.SetPhase(DayPhase.Sunset); return DayNight.phase + " " + DayNight.timeOfDay.ToString("F2") + " " + DayNight.isNight + " " + (DayNight.sunElevation < 0);' | Set-Content -Encoding utf8 $csf
        $api = Invoke-NovaJson "exec --file `"$csf`""
        Add-Result daynight 'C# DayNight API (SetPhase, phase, timeOfDay, isNight)' ("$($api.result)" -eq 'Sunset 18.75 False True') "$($api.result)"
        function Wait-Sec([double]$s) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Invoke-Nova 'wait 10' | Out-Null } }
        function Exec([string]$name, [string]$code) { $f = Join-Path $dir "$name.cs"; $code | Set-Content -Encoding utf8 $f; $r = Invoke-NovaJson "exec --file `"$f`""; if ($r) { "$($r.result)" } else { '' } }
        Invoke-Nova 'stop' | Out-Null

        # ---- 거리: 가로등 (NightLight) + 거울 구 + 구운 반사 프로브 — 밤에 가로등이 켜지고, 프로브가 그 시각으로 다시 찍힌다
        $tdir = Join-Path $Project 'Assets\DayNightTest'
        New-Item -ItemType Directory -Force $tdir | Out-Null
        function TestMat([string]$name, [double[]]$c, [double]$metal, [double]$smooth, [double[]]$emis = $null)
        {
            $m = [ordered]@{ Shader = 'Universal Render Pipeline/Lit'; ResourcePath = "Assets\DayNightTest\$name.mat"; BaseMapPath = ''; NormalMapPath = ''; MetallicMapPath = ''; OcclusionMapPath = ''; EmissionMapPath = ''
                BaseColor = @($c[0], $c[1], $c[2], 1); Metallic = $metal; Smoothness = $smooth; SmoothnessSource = 0; NormalScale = 1.0; OcclusionStrength = 1.0; Tiling = @(1, 1); Offset = @(0, 0)
                AlphaClipping = 0; Cutoff = 0.5; ReceiveShadows = 1; SpecularHighlights = 1; EnvironmentReflections = 1; Emission = [bool]$emis; EmissionColor = $(if ($emis) { @($emis[0], $emis[1], $emis[2]) } else { @(0, 0, 0) })
                EmissionIntensity = 1.0; Priority = 0; UseShadowMap = 1 }
            $m | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $tdir "$name.mat")
        }
        TestMat 'Mirror' @(0.95, 0.95, 0.95) 1.0 1.0
        TestMat 'Bulb' @(1, 0.9, 0.7) 0.0 0.5 @(4.0, 3.0, 1.8)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create plane --name Ground --scale 4,1,4' | Out-Null
        Invoke-Nova 'create empty --name TimeOfDay' | Out-Null
        Invoke-Nova 'add-component TimeOfDay DayNightCycle --values "{\"dayLengthMinutes\":0}"' | Out-Null
        Invoke-Nova 'create sphere --name MirrorBall --position 0,1,0' | Out-Null
        Invoke-Nova 'set MirrorBall --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/DayNightTest/Mirror.mat\"]}"' | Out-Null
        Invoke-Nova 'create cylinder --name Pole --position 1.6,1.5,1.2 --scale 0.08,1.5,0.08' | Out-Null
        Invoke-Nova 'create sphere --name Bulb --position 1.6,3.05,1.2 --scale 0.3,0.3,0.3' | Out-Null
        Invoke-Nova 'set Bulb --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/DayNightTest/Bulb.mat\"]}"' | Out-Null
        Invoke-Nova 'create point-light --name Lamp --position 1.6,2.8,1.2' | Out-Null
        Invoke-Nova 'add-component Lamp NightLight' | Out-Null
        # create point-light = Point (예전에는 Directional 로 남았다), 소수 칸에 정수 JSON (예전에는 0) — 세기 · 거리를 정수로
        Invoke-Nova 'set Lamp --component Light --values "{\"pointLightRange\":9,\"intensity\":1}"' | Out-Null
        $lg = Invoke-NovaJson 'get Lamp'; $ll = @($lg.components | Where-Object { $_.type -eq 'Light' })[0]
        Add-Result daynight 'Point Light from create point-light is a Point light; integer JSON sets a float field' ($ll -and [int]$ll.lightType -eq 2 -and [double]$ll.pointLightRange -eq 9) "lightType $($ll.lightType) (2 = Point), pointLightRange $($ll.pointLightRange) (9)"
        Invoke-Nova 'create empty --name Probe --position 0,1,0' | Out-Null
        Invoke-Nova 'add-component Probe ReflectionProbe --values "{\"size\":[16,8,16],\"resolution\":128}"' | Out-Null
        Invoke-Nova 'set "Main Camera" --position -0.6,1.3,-2.6 --rotation 4,12,0' | Out-Null
        Invoke-Nova 'scene save --as Assets/DayNightTest/Street.scene' | Out-Null
        Invoke-Nova 'daynight set --time 12 --probes 0' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $bk = Invoke-NovaJson 'probe bake'
        # 낮 → 밤 (자정): 프로브를 30 분마다 — 구운 프로브도 실행 중의 큐브로 다시 찍힌다
        function ProbeState { $i = Invoke-NovaJson 'probe info'; $p = @($i.probes | Where-Object { $_.name -eq 'Probe' })[0]; return $p }
        Invoke-Nova 'daynight set --time 12 --probes 30' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $p12 = ProbeState
        Invoke-Nova 'daynight set --time 0' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $p0 = ProbeState
        Add-Result daynight 'reflection probe is captured again for the time of day (baked probe → run-time cube at noon, then midnight)' ($bk -and $p12.relit -and [math]::Abs([double]$p12.capturedTime - 12) -lt 0.01 -and $p0.relit -and [math]::Abs([double]$p0.capturedTime) -lt 0.01) "baked $($bk.baked[0].bakedTexture), noon relit $($p12.relit) at $($p12.capturedTime), midnight relit $($p0.relit) at $($p0.capturedTime)"
        # 밤 반사: 다시 찍은 프로브 = 지금의 하늘 · 빛 — 거울 구 위쪽에 밤 하늘 (어둡다), 아래쪽에 비친 바닥이 바닥만큼 밝다 (하늘 보정을 두 번 받지 않는다) /
        #  끄면 낮에 구운 큐브를 밤 하늘만큼 어둡게 — 구 위쪽에 낮 구름이 비친다. 구 = 화면 (0.507, 0.55), 반지름 = 너비 × 0.08
        function BallStats([string]$png)
        {
            $b = New-Object System.Drawing.Bitmap $png
            try
            {
                function Median([double]$x0, [double]$x1, [double]$y0, [double]$y1)
                {
                    $v = New-Object System.Collections.Generic.List[double]
                    for ($y = [int]$y0; $y -lt [int]$y1; $y += 2) { for ($x = [int]$x0; $x -lt [int]$x1; $x += 2) { $c = $b.GetPixel($x, $y); $v.Add(($c.R + $c.G + $c.B) / 3.0) } }
                    $v.Sort(); return $v[[int]($v.Count / 2)]
                }
                $cx = $b.Width * 0.507; $cy = $b.Height * 0.55; $r = $b.Width * 0.08
                return [pscustomobject]@{ Ball = (Median ($cx - 0.5 * $r) ($cx + 0.5 * $r) ($cy + 0.2 * $r) ($cy + 0.6 * $r)); Ground = (Median ($b.Width * 0.05) ($b.Width * 0.35) ($b.Height * 0.6) ($b.Height * 0.8))
                    Sky = (Median ($cx - 0.8 * $r) ($cx - 0.4 * $r) ($cy - 0.6 * $r) ($cy - 0.25 * $r)) }
            }
            finally { $b.Dispose() }
        }
        $live = Join-Path $dir 'street_night_live.png'
        Invoke-Nova "screenshot `"$live`" --view game" | Out-Null
        Invoke-Nova 'daynight set --probes 0' | Out-Null
        Invoke-Nova 'wait 6' | Out-Null
        $pOff = ProbeState
        $stale = Join-Path $dir 'street_night_baked.png'
        Invoke-Nova "screenshot `"$stale`" --view game" | Out-Null
        $sl = BallStats $live; $ss = BallStats $stale
        Add-Result daynight 'night reflection: the re-captured probe shows the night sky and is not darkened twice (refresh off = the noon cube with day clouds)' ((-not $pOff.relit) -and $sl.Ball -gt $sl.Ground * 0.75 -and $sl.Sky -lt $ss.Sky * 0.7) ("sky in the ball: live {0:F0}, baked at noon {1:F0}; ground in the ball: live {2:F0} (ground {3:F0}); relit after off: {4}" -f $sl.Sky, $ss.Sky, $sl.Ball, $sl.Ground, $pOff.relit)

        # 가로등 (NightLight, Play): 낮 = 꺼짐, 밤 = 켜짐 (세기 그대로), 다시 낮 = 꺼짐
        Invoke-Nova 'daynight set --time 12 --probes 30' | Out-Null
        Invoke-Nova 'play' | Out-Null
        Wait-Sec 1
        $lampCs = 'var l = GameObject.Find("Lamp"); var n = l.GetComponent<NightLight>(); var li = l.GetComponent<Light>(); return n.isOn + " " + li.enabled + " " + li.intensity.ToString("F2");'
        $d1 = Exec 'lamp_day' $lampCs
        Invoke-Nova 'daynight set --time 22' | Out-Null
        Wait-Sec 3.5
        $n1 = Exec 'lamp_night' $lampCs
        Invoke-Nova "screenshot `"$(Join-Path $dir 'street_night.png')`" --view game" | Out-Null
        Invoke-Nova 'daynight set --time 9' | Out-Null
        Wait-Sec 3.5
        $d2 = Exec 'lamp_morning' $lampCs
        Add-Result daynight 'NightLight: street light off by day, on at night (full intensity), off again in the morning' ($d1 -eq 'False False 0.00' -and $n1 -match '^True True (\d+\.\d+)$' -and [double]$Matches[1] -gt 0.5 -and $d2 -eq 'False False 0.00') "noon $d1 / 22:00 $n1 / 9:00 $d2"
        Invoke-Nova 'stop' | Out-Null
        Remove-Item $tdir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item "$tdir.meta" -Force -ErrorAction SilentlyContinue

        # Adaptive Probe Volume + 낮 · 밤: 프로브가 모으는 하늘에 시각의 환경광 배율 (예전: 밤에도 낮 하늘 그대로 — 프로브 빛 전체를 화면에서 줄였다)
        #  열린 땅 위 1.5 m 프로브의 위쪽 빛 = 거의 하늘 → 자정은 정오보다 환경광 배율만큼 어둡다
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 40,1,40' | Out-Null
        Invoke-Nova 'create empty --name TimeOfDay' | Out-Null
        Invoke-Nova 'add-component TimeOfDay DayNightCycle' | Out-Null
        Invoke-Nova 'create empty --name APV --position 0,1,0' | Out-Null
        Invoke-Nova 'add-component APV AdaptiveProbeVolume' | Out-Null
        Invoke-Nova 'create cube --name Box --position 0,0.5,0' | Out-Null
        Invoke-Nova 'create point-light --name Lamp --position 1.5,2,-1' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.6,-6 --rotation 8,0,0' | Out-Null
        function ApvUp($pos) { $j = Invoke-NovaJson "probevolume probe --position $pos"; $u = $j.cascades[0].probe.ambientUp; if ($u) { 0.2126 * $u[0] + 0.7152 * $u[1] + 0.0722 * $u[2] } else { -1 } }
        $apvDay = Invoke-NovaJson 'daynight set --time 12'
        Invoke-Nova 'wait 240' | Out-Null
        $upDay = ApvUp '0,1.5,0'
        Invoke-Nova "screenshot `"$(Join-Path $dir 'apv_noon.png')`" --view game" | Out-Null
        $apvNight = Invoke-NovaJson 'daynight set --time 0'
        Invoke-Nova 'wait 240' | Out-Null
        $upNight = ApvUp '0,1.5,0'
        Invoke-Nova "screenshot `"$(Join-Path $dir 'apv_night.png')`" --view game" | Out-Null   # 이 스위트는 Game 탭이 앞 (Scene 뷰는 그리지 않아 예전 그림)
        $ambRatio = if ($apvDay -and [double]$apvDay.ambientIntensity -gt 0) { [double]$apvNight.ambientIntensity / [double]$apvDay.ambientIntensity } else { 1 }
        $upRatio = if ($upDay -gt 0) { $upNight / $upDay } else { 1 }
        Add-Result daynight 'Adaptive Probe Volume follows the time of day: the probes gather the night sky (dark), not the noon skybox' ($upDay -gt 0 -and $upNight -ge 0 -and $ambRatio -lt 0.6 -and $upRatio -lt [math]::Max(0.1, $ambRatio * 1.5)) ("probe up-light noon {0:N3} midnight {1:N3} (x{2:N2}), ambient intensity x{3:N2}" -f $upDay, $upNight, $upRatio, $ambRatio)
        Invoke-Nova 'daynight set --time 12' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'package remove com.nova.daynight' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        if ($before) { [IO.File]::WriteAllText($manifest, $before) }
    }
}

function Suite-Cloth
{
    # 천 (Cloth → Jolt Soft Body): 2 x 2 m Plane 천이 구 위로 떨어져 덮인다 (자기 Mesh Collider 는 무시, 구 안 · 바닥 아래로 안 들어감),
    #  세운 Plane 커튼 (위쪽 가장자리 고정 + 바람): 고정점은 그대로 · 아래는 바람 쪽으로, 오브젝트를 옮기면 고정점이 따라온다, C# Cloth API
    Write-Host '[cloth]'
    $dir = Join-Path $Out 'cloth'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 30,1,30' | Out-Null
        Invoke-Nova 'create sphere --name Ball --position 0,1,0' | Out-Null
        Invoke-Nova 'create plane --name Sheet --position 0,2.2,0 --scale 0.2,1,0.2' | Out-Null
        Invoke-Nova 'add-component Sheet Cloth --values "{\"bendingStiffness\":0.2,\"thickness\":0.03}"' | Out-Null
        Invoke-Nova 'create plane --name Curtain --position 4,2,0 --rotation -90,0,0 --scale 0.2,1,0.2' | Out-Null   # 앞면이 카메라 쪽 (-Z)
        # 바람 6 m/s² (감쇠 0.6 → 출렁이지 않고 바람 쪽으로 기운 채 — 한 번 재도 된다)
        Invoke-Nova 'add-component Curtain Cloth --values "{\"pin\":1,\"damping\":0.6,\"externalAcceleration\":[0,0,6]}"' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 2,2.6,-6.5 --rotation 9,0,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null

        $sheetCs = Join-Path $dir 'cloth_sheet.cs'
        'var t = GameObject.Find("Sheet").transform; var c = t.GetComponent<Cloth>(); var v = c.vertices; float minY = 99, minD = 99, centerY = 0, best = 99; foreach (var lp in v) { var p = t.TransformPoint(lp); minY = Mathf.Min(minY, p.y); minD = Mathf.Min(minD, (p - new Vector3(0, 1, 0)).magnitude); float h = new Vector2(p.x, p.z).magnitude; if (h < best) { best = h; centerY = p.y; } } return c.isSimulating + " " + v.Length + " " + centerY.ToString("F2") + " " + minY.ToString("F2") + " " + minD.ToString("F2");' | Set-Content -Encoding utf8 $sheetCs
        $curtainCs = Join-Path $dir 'cloth_curtain.cs'
        # 고정 줄 = 오브젝트 평면 위 (z = 오브젝트 z) 의 위쪽 가장자리 높이 (오브젝트 y + 1) 그대로인 정점, 바람 = 나머지 정점의 평균 z
        'var t = GameObject.Find("Curtain").transform; var v = t.GetComponent<Cloth>().vertices; float topY = t.position.y + 1f; float tx = 0, tz = 0, fz = 0; int nt = 0, nf = 0; foreach (var lp in v) { var p = t.TransformPoint(lp); if (Mathf.Abs(p.y - topY) < 0.005f && Mathf.Abs(p.z - t.position.z) < 0.005f) { tx += p.x; tz += p.z; nt++; } else { fz += p.z - t.position.z; nf++; } } return nt + " " + (nt > 0 ? tx / nt : 0).ToString("F2") + " " + (nt > 0 ? tz / nt : 0).ToString("F2") + " " + (nf > 0 ? fz / nf : 0).ToString("F2") + " " + topY.ToString("F2");' | Set-Content -Encoding utf8 $curtainCs

        Invoke-Nova 'play' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 4) { Invoke-Nova 'wait 20' | Out-Null }
        Invoke-Nova "screenshot `"$(Join-Path $dir 'cloth.png')`" --view game" | Out-Null
        $s = Invoke-NovaJson "exec --file `"$sheetCs`""
        $sv = if ($s) { "$($s.result)" -split ' ' } else { @() }
        Add-Result cloth 'cloth drapes over the sphere (own collider ignored, not inside, not under ground)' ($sv.Count -eq 5 -and $sv[0] -eq 'True' -and [double]$sv[2] -gt 1.48 -and [double]$sv[2] -lt 1.65 -and [double]$sv[3] -gt -0.05 -and [double]$sv[4] -gt 0.44) "simulating $($sv[0]), $($sv[1]) vertices, centre y $($sv[2]) (1.5 .. 1.6), lowest $($sv[3]) (> -0.05), closest to ball centre $($sv[4]) (> 0.44)"
        $c = Invoke-NovaJson "exec --file `"$curtainCs`""
        $cv = if ($c) { "$($c.result)" -split ' ' } else { @() }
        Add-Result cloth 'curtain: pinned top edge stays, wind blows the cloth (+Z)' ($cv.Count -eq 5 -and [int]$cv[0] -eq 10 -and [math]::Abs([double]$cv[1] - 4) -lt 0.05 -and [double]$cv[3] -gt 0.2) "pinned $($cv[0]) (10) at x $($cv[1]) (4), free vertices z $($cv[3]) (> 0.2)"
        $moveCs = Join-Path $dir 'cloth_move.cs'
        'GameObject.Find("Curtain").transform.position += new Vector3(2, 0, 0); return 1;' | Set-Content -Encoding utf8 $moveCs
        Invoke-Nova "exec --file `"$moveCs`"" | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 1.5) { Invoke-Nova 'wait 10' | Out-Null }
        $c2 = Invoke-NovaJson "exec --file `"$curtainCs`""
        $cv2 = if ($c2) { "$($c2.result)" -split ' ' } else { @() }
        $spanCs = Join-Path $dir 'cloth_span.cs'
        'var t = GameObject.Find("Curtain").transform; float a = 99, b = -99; foreach (var lp in t.GetComponent<Cloth>().vertices) { var p = t.TransformPoint(lp); a = Mathf.Min(a, p.x); b = Mathf.Max(b, p.x); } return a.ToString("F2") + " " + b.ToString("F2");' | Set-Content -Encoding utf8 $spanCs
        $span = Invoke-NovaJson "exec --file `"$spanCs`""
        Add-Result cloth 'teleporting the object moves the cloth with it (no whip)' ($cv2.Count -eq 5 -and [int]$cv2[0] -eq 10 -and [math]::Abs([double]$cv2[1] - 6) -lt 0.05 -and "$($span.result)" -match '^(\S+) (\S+)$' -and [double]$Matches[1] -gt 4.8 -and [double]$Matches[2] -lt 7.2) "pinned $($cv2[0]) at x $($cv2[1]) (6), cloth x span $($span.result) (5 .. 7)"
        $apiCs = Join-Path $dir 'cloth_api.cs'
        'var c = GameObject.Find("Curtain").GetComponent<Cloth>(); c.damping = 0.3f; c.externalAcceleration = new Vector3(1, 0, 0); return c.pin + " " + c.damping.ToString("F1") + " " + c.externalAcceleration.x.ToString("F0") + " " + c.useGravity + " " + c.stretchingStiffness.ToString("F0");' | Set-Content -Encoding utf8 $apiCs
        $api = Invoke-NovaJson "exec --file `"$apiCs`""
        Add-Result cloth 'C# Cloth API (pin, damping, externalAcceleration, useGravity, stiffness)' ("$($api.result)" -eq 'TopEdge 0.3 1 True 1') "$($api.result)"
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

function Suite-ClothSkin
{
    # 스킨 위의 천 (Skinned Mesh Renderer + Cloth): 모델 편집기로 치마 · 망토를 입힌 치비 (docs/examples/model_chibi_cloth.txt) → FBX → 캐릭터.
    #  (VRM 으로 내보냄 — lilToon 툰 재질)
    #  치마 = 위 가장자리 고정 (허리 = 피부에 붙음) · 나머지는 늘어진다, 캐릭터를 옮기면 따라온다 (순간 이동 = 피부 자리로),
    #  망토 = coefficients 의 maxDistance 로 피부 가까이 (바람이 불어도), C# coefficients API
    Write-Host '[clothskin]'
    $dir = Join-Path $Out 'clothskin'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $assetDir = Join-Path $Project 'Assets\ClothTest'
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $manifestBefore = if (Test-Path $manifest) { [IO.File]::ReadAllBytes($manifest) } else { $null }
    $ed = Start-TestEditor
    try
    {
        Invoke-NovaJson 'package add com.nova.modeling' | Out-Null
        function M([string]$line) { Invoke-NovaJson "model $line" }
        function Wait-Sec([double]$s) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Invoke-Nova 'wait 10' | Out-Null } }
        function Exec([string]$name, [string]$code) { $f = Join-Path $dir "$name.cs"; $code | Set-Content -Encoding utf8 $f; $r = Invoke-NovaJson "exec --file `"$f`""; if ($r) { "$($r.result)" } else { '' } }
        M 'new' | Out-Null
        Invoke-NovaJson "model batch $root\docs\examples\model_chibi.txt" | Out-Null
        Invoke-NovaJson "model batch $root\docs\examples\model_chibi_cloth.txt" | Out-Null
        $ck = M 'rig.check'
        M "render --path $dir\model.png --views front,right --size 256 --shading toon --wire false --bones false" | Out-Null
        Remove-Item $assetDir -Recurse -Force -ErrorAction SilentlyContinue
        $fx = M "export --path $assetDir\ClothChibi.vrm --title ClothChibi"
        Add-Result clothskin 'model: chibi with a skirt and a cape, rigged, exported .vrm' ($fx -and $ck.ok -and (Test-Path "$assetDir\ClothChibi.vrm")) "rig.check $($ck.ok), vrm $(Test-Path "$assetDir\ClothChibi.vrm")"

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 30,1,30' | Out-Null
        Invoke-Nova 'create character --name Girl --model Assets\ClothTest\ClothChibi.vrm' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $names = Exec 'cs_names' 'var s = ""; foreach (var r in GameObject.Find("Girl").GetComponentsInChildren<SkinnedMeshRenderer>()) s += r.gameObject.name + ","; return s;'
        Invoke-Nova 'add-component Skirt Cloth --values "{\"pin\":1,\"damping\":0.2,\"bendingStiffness\":0.1,\"randomAcceleration\":[4,0,4]}"' | Out-Null
        Invoke-Nova 'add-component Cape Cloth --values "{\"pin\":1,\"damping\":0.3,\"externalAcceleration\":[0,0,-8]}"' | Out-Null
        # 망토: 모든 정점 maxDistance 3 cm (고정 줄은 Pin 으로 0) — 바람이 불어도 피부 가까이
        $coef = Exec 'cs_coef' 'var c = GameObject.Find("Cape").GetComponent<Cloth>(); var k = c.coefficients; for (int i = 0; i < k.Length; i++) k[i].maxDistance = 0.03f; c.coefficients = k; var b = c.coefficients; return k.Length + " " + b[b.Length - 1].maxDistance.ToString("F2") + " " + (GameObject.Find("Skirt").GetComponent<Cloth>().coefficients[0].maxDistance > 1e30f);'
        Add-Result clothskin 'C# Cloth.coefficients (one per cloth vertex, set / get, default = free)' ($coef -match '^(\d+) 0\.03 True$' -and [int]$Matches[1] -gt 20) "$coef (renderers: $names)"
        # 치마: 뒤 막이 2 cm (collisionSphereDistance) — 흔들려도 피부 (원뿔) 안쪽으로 2 cm 넘게 들어가지 않는다 (안의 원피스가 뚫고 나오지 않게)
        Exec 'cs_back' 'var c = GameObject.Find("Skirt").GetComponent<Cloth>(); var k = c.coefficients; for (int i = 0; i < k.Length; i++) k[i].collisionSphereDistance = 0.02f; c.coefficients = k; return k.Length;' | Out-Null
        # 시작 자세 (월드): 치마의 가장 높은 · 낮은 y, 캐릭터에서 가장 먼 거리, 가운데 x / 망토 정점 (오브젝트 로컬)
        $span = 'var t = GameObject.Find("Skirt").transform; var g = GameObject.Find("Girl").transform.position; float hi = -9, lo = 9, far = 0, cx = 0; var v = t.GetComponent<Cloth>().vertices; foreach (var lp in v) { var p = t.TransformPoint(lp); hi = Mathf.Max(hi, p.y); lo = Mathf.Min(lo, p.y); far = Mathf.Max(far, new Vector2(p.x - g.x, p.z - g.z).magnitude); cx += p.x; } return hi.ToString("F3") + " " + lo.ToString("F3") + " " + far.ToString("F3") + " " + (cx / v.Length - g.x).ToString("F3") + " " + t.GetComponent<Cloth>().isSimulating;'
        $rest = Exec 'cs_span0' $span
        $capeV = 'var v = GameObject.Find("Cape").GetComponent<Cloth>().vertices; var s = new System.Text.StringBuilder(); foreach (var p in v) s.Append(p.x.ToString("F4") + "," + p.y.ToString("F4") + "," + p.z.ToString("F4") + ";"); return s.ToString();'
        $cape0 = Exec 'cs_cape0' $capeV
        $skirtV = $capeV -replace '"Cape"', '"Skirt"'
        $skirt0 = Exec 'cs_skirt0' $skirtV

        Invoke-Nova 'set "Main Camera" --position 1.2,0.85,-1.3 --rotation 10,-43,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'play' | Out-Null
        Wait-Sec 3
        Invoke-Nova "screenshot `"$(Join-Path $dir 'clothskin.png')`" --view game" | Out-Null
        $now = Exec 'cs_span1' $span
        $log = Invoke-Nova 'log --grep "skinned cloth" -n 4'
        $r0 = "$rest" -split ' '; $r1 = "$now" -split ' '
        $ok = $r0.Count -eq 5 -and $r1.Count -eq 5 -and $r1[4] -eq 'True' -and [math]::Abs([double]$r1[0] - [double]$r0[0]) -lt 0.04 -and [double]$r1[1] -gt 0.0 -and [double]$r1[1] -lt [double]$r0[1] + 0.02 -and [double]$r1[2] -lt 0.6
        Add-Result clothskin 'skirt: waist stays on the skin, the rest hangs (not under the ground, not flying away)' ($ok -and ($log -match "skinned cloth 'Skirt'")) "top $($r0[0]) → $($r1[0]), bottom $($r0[1]) → $($r1[1]), farthest $($r1[2]) (< 0.6), log $((($log | Out-String) -replace '\s+', ' ').Trim())"
        # 망토: 바람 (−Z 8 m/s²) 이 불어도 정점마다 시작 자리에서 몇 cm 안 (maxDistance 3 cm + 숨쉬기 자세)
        $cape1 = Exec 'cs_cape1' $capeV
        $a = @("$cape0" -split ';' | Where-Object { $_ }); $b = @("$cape1" -split ';' | Where-Object { $_ })
        $maxMove = -1
        if ($a.Count -gt 0 -and $a.Count -eq $b.Count)
        {
            $maxMove = 0
            for ($i = 0; $i -lt $a.Count; $i++)
            {
                $p = $a[$i] -split ','; $q = $b[$i] -split ','
                $d = [math]::Sqrt([math]::Pow([double]$p[0] - [double]$q[0], 2) + [math]::Pow([double]$p[1] - [double]$q[1], 2) + [math]::Pow([double]$p[2] - [double]$q[2], 2))
                if ($d -gt $maxMove) { $maxMove = $d }
            }
        }
        Add-Result clothskin 'cape: maxDistance 3 cm keeps it on the skin against the wind' ($maxMove -ge 0 -and $maxMove -lt 0.08) ("{0} vertices, largest move {1:N3} m (< 0.08)" -f $a.Count, $maxMove)
        # 치마: 정점마다 축에서의 거리 (오브젝트 로컬 XZ) 가 시작보다 얼마나 줄었나 — 중력이 원뿔을 안으로 당겨 뒤 막이에 기대고 (1 cm 넘게),
        #  가장 많이 들어간 것이 뒤 막이 2 cm (+ 숨쉬기) 안
        $skirt1 = Exec 'cs_skirt1' $skirtV
        $a = @("$skirt0" -split ';' | Where-Object { $_ }); $b = @("$skirt1" -split ';' | Where-Object { $_ })
        $inward = 99; $outward = -99
        if ($a.Count -gt 0 -and $a.Count -eq $b.Count)
        {
            $inward = 0; $outward = 0
            for ($i = 0; $i -lt $a.Count; $i++)
            {
                $p = $a[$i] -split ','; $q = $b[$i] -split ','
                $d = [math]::Sqrt([double]$q[0] * [double]$q[0] + [double]$q[2] * [double]$q[2]) - [math]::Sqrt([double]$p[0] * [double]$p[0] + [double]$p[2] * [double]$p[2])
                if (-$d -gt $inward) { $inward = -$d }
                if ($d -gt $outward) { $outward = $d }
            }
        }
        Add-Result clothskin 'skirt: back stop (collisionSphereDistance 2 cm) keeps the shaking skirt outside the body' ($inward -gt 0.01 -and $inward -lt 0.035) ("{0} vertices, deepest inward {1:N3} m (0.01 .. 0.035), outward up to {2:N3} m" -f $a.Count, $inward, $outward)
        # 캐릭터를 3 m 옮기면 (순간 이동) 치마가 피부 자리로 같이 — 휘날리지 않는다
        Exec 'cs_move' 'GameObject.Find("Girl").transform.position += new Vector3(3, 0, 0); return 1;' | Out-Null
        Wait-Sec 1
        $moved = Exec 'cs_span2' $span
        $r2 = "$moved" -split ' '
        Add-Result clothskin 'teleporting the character brings the skirt along (no whip)' ($r2.Count -eq 5 -and [double]$r2[2] -lt 0.6 -and [math]::Abs([double]$r2[3]) -lt 0.15 -and [math]::Abs([double]$r2[0] - [double]$r0[0]) -lt 0.04) "top $($r2[0]), farthest from the character $($r2[2]) (< 0.6), centre offset x $($r2[3])"
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item "$assetDir.meta" -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Starter
{
    # Starter Assets 예제: 차 (create car — 프리팹 · 재질 · Follow Camera, CarController 를 스크립트 입력으로: 쉬기 · 가속 · 조향 · 손 브레이크 · 후진),
    #  래그돌 표적 (create ragdoll-target — 서 있다가 RagdollShooter 의 광선에 맞으면 쓰러져 밀려남 → 일어나기 클립 + 섞기), Camera.ScreenPointToRay,
    #  차에 타고 내리기 (VehicleEnterExit — 숨김 · 운전 · 카메라 전환 → 운전석 쪽에 내려 다시 걷기)
    Write-Host '[starter]'
    $dir = Join-Path $Out 'starter'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\StarterAssets'
    $manifest = Join-Path $Project 'Packages\manifest.json'
    $manifestBefore = if (Test-Path $manifest) { [IO.File]::ReadAllBytes($manifest) } else { $null }
    Remove-Item $assetDir -Recurse -Force -ErrorAction SilentlyContinue
    $ed = Start-TestEditor
    try
    {
        function Wait-Sec([double]$s) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Invoke-Nova 'wait 10' | Out-Null } }
        function Exec([string]$name, [string]$code) { $f = Join-Path $dir "$name.cs"; $code | Set-Content -Encoding utf8 $f; $r = Invoke-NovaJson "exec --file `"$f`""; if ($r) { "$($r.result)" } else { '' } }
        function Wait-Compile { $sw = [Diagnostics.Stopwatch]::StartNew(); do { Invoke-Nova 'wait 20' | Out-Null; $i = Invoke-NovaJson 'info' } while ($sw.Elapsed.TotalSeconds -lt 90 -and $i -and $i.compiling) }

        # ---- 차
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,40 --scale 200,1,200' | Out-Null
        # 처음 차 = 프리팹을 만든다 (세워 둘 차), 다음 차 = 프리팹 인스턴스 + Follow Camera 가 새 차로 (운전할 차)
        Invoke-Nova 'create car --name Parked --position 8,0,0' | Out-Null
        Invoke-Nova 'create car --name Car' | Out-Null
        Wait-Compile
        $prefab = Test-Path (Join-Path $assetDir 'Car.prefab')
        $mats = @(Get-ChildItem (Join-Path $assetDir 'Materials') -Filter *.mat -ErrorAction SilentlyContinue).Count
        $inst = Invoke-Nova 'log --grep "instantiated" -n 2'
        Add-Result starter 'create car: prefab + materials saved in Assets/StarterAssets, the next car is an instance' ($prefab -and $mats -ge 5 -and $inst -match 'instantiated .*Car\.prefab') "prefab $prefab, materials $mats, $((($inst | Out-String) -replace '\s+', ' ').Trim())"

        $state = 'var c = GameObject.Find("Car"); var k = c.GetComponent<StarterAssets.CarController>(); var cam = Camera.main.transform.position; var p = c.transform.position; return k.speed.ToString("F2") + " " + p.x.ToString("F2") + " " + p.y.ToString("F2") + " " + p.z.ToString("F2") + " " + c.transform.eulerAngles.y.ToString("F1") + " " + c.transform.up.y.ToString("F3") + " " + k.groundedWheels + " " + (cam - p).magnitude.ToString("F1") + " " + (cam.z - p.z).ToString("F1");'
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'play' | Out-Null
        Wait-Sec 2
        # C# 스크립트 컴포넌트는 Play 중에 만들어진다
        $info = Exec 'car_info' 'var c = GameObject.Find("Car"); var f = Camera.main.GetComponent<FollowCamera>(); return c.GetComponentsInChildren<WheelCollider>().Length + " " + (c.GetComponent<StarterAssets.CarController>() != null) + " " + (f != null && f.target != null ? f.target.name : "none") + " " + c.GetComponent<Rigidbody>().mass.ToString("F0");'
        Add-Result starter 'car: 4 Wheel Colliders, CarController, Follow Camera on the Main Camera, 1200 kg' ($info -eq '4 True Car 1200') "$info (wheels, controller, camera target, mass)"
        $s0 = (Exec 'car_s0' $state) -split ' '
        Add-Result starter 'car rests on 4 wheels (upright, still)' ($s0.Count -eq 9 -and [int]$s0[6] -eq 4 -and [double]$s0[5] -gt 0.99 -and [math]::Abs([double]$s0[0]) -lt 0.2) "speed $($s0[0]), y $($s0[2]), up $($s0[5]), grounded $($s0[6])"
        Exec 'car_go' 'var k = GameObject.Find("Car").GetComponent<StarterAssets.CarController>(); k.readKeyboard = false; k.throttle = 1f; return 1;' | Out-Null
        Wait-Sec 4
        Invoke-Nova "screenshot `"$(Join-Path $dir 'car.png')`" --view game" | Out-Null
        $s1 = (Exec 'car_s1' $state) -split ' '
        Add-Result starter 'throttle: drives forward, the camera follows behind' ($s1.Count -eq 9 -and [double]$s1[0] -gt 8 -and [double]$s1[3] -gt 12 -and [math]::Abs([double]$s1[1]) -lt 1 -and [double]$s1[7] -lt 12 -and [double]$s1[8] -lt 0) "speed $($s1[0]) m/s, z $($s1[3]), x $($s1[1]), camera $($s1[7]) m away (behind: dz $($s1[8]))"
        Exec 'car_turn' 'var k = GameObject.Find("Car").GetComponent<StarterAssets.CarController>(); k.steer = 1f; k.throttle = 0.5f; return 1;' | Out-Null
        Wait-Sec 2.5
        $s2 = (Exec 'car_s2' $state) -split ' '
        $yaw = if ($s2.Count -eq 9) { [double]$s2[4] } else { 0 }
        Add-Result starter 'steer right: turns, stays upright' ($s2.Count -eq 9 -and $yaw -gt 30 -and $yaw -lt 300 -and [double]$s2[5] -gt 0.9) "yaw $yaw°, up $($s2[5])"
        # S (뒤로) 를 누른 채: 달리는 중엔 네 바퀴 브레이크로 멈추고, 멈추면 후진
        #  (멈춤을 재려고 처음엔 후진 최고 속도 0 — 브레이크만)
        Exec 'car_brake' 'var k = GameObject.Find("Car").GetComponent<StarterAssets.CarController>(); k.steer = 0f; k.throttle = -1f; k.maxReverseSpeed = 0f; return 1;' | Out-Null
        Wait-Sec 3
        $s3 = (Exec 'car_s3' $state) -split ' '
        Exec 'car_rev' 'var k = GameObject.Find("Car").GetComponent<StarterAssets.CarController>(); k.maxReverseSpeed = 6f; return 1;' | Out-Null
        Wait-Sec 2.5
        $s4 = (Exec 'car_s4' $state) -split ' '
        Add-Result starter 'S while driving: brakes to a stop, then reverses' ($s3.Count -eq 9 -and [math]::Abs([double]$s3[0]) -lt 0.3 -and $s4.Count -eq 9 -and [double]$s4[0] -lt -1) "stopped at $($s3[0]) m/s, then speed $($s4[0])"
        Invoke-Nova 'stop' | Out-Null

        # ---- 래그돌 표적 + 쏘기
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 30,1,30' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0.6,1.5,-4 --rotation 6,0,0' | Out-Null
        Invoke-Nova 'create ragdoll-target --name Dummy' | Out-Null
        Wait-Compile
        Invoke-Nova 'play' | Out-Null
        Wait-Sec 1.5
        $rinfo = Exec 'rd_info' 'var d = GameObject.Find("Dummy"); return d.GetComponent<Ragdoll>().bodyCount + " " + d.GetComponent<Ragdoll>().active + " " + (d.GetComponent<StarterAssets.RagdollTarget>() != null) + " " + (Camera.main.GetComponent<StarterAssets.RagdollShooter>() != null);'
        Add-Result starter 'create ragdoll-target: 11 bodies (off = standing), RagdollTarget, RagdollShooter on the Main Camera' ($rinfo -eq '11 False True True') "$rinfo"
        $ray = Exec 'rd_ray' 'var c = Camera.main; var a = c.ScreenPointToRay(new Vector3(Screen.width * 0.5f, Screen.height * 0.5f, 0)); var b = c.ViewportPointToRay(new Vector3(1, 0.5f, 0)); float half = Vector3.Angle(a.direction, b.direction); return Vector3.Dot(a.direction, c.transform.forward).ToString("F4") + " " + half.ToString("F1") + " " + (Mathf.Atan(Mathf.Tan(c.fieldOfView * 0.5f * Mathf.Deg2Rad) * c.aspect) * Mathf.Rad2Deg).ToString("F1");'
        $rv = "$ray" -split ' '
        Add-Result starter 'Camera.ScreenPointToRay / ViewportPointToRay (centre = forward, edge = half the horizontal FOV)' ($rv.Count -eq 3 -and [double]$rv[0] -gt 0.9999 -and [math]::Abs([double]$rv[1] - [double]$rv[2]) -lt 0.2) "centre dot $($rv[0]), right edge $($rv[1])° (expect $($rv[2])°)"
        $before = Exec 'rd_before' 'var d = GameObject.Find("Dummy"); var p = d.GetComponent<Animator>().GetBonePosition(HumanBodyBones.Hips); return p.y.ToString("F2") + " " + p.z.ToString("F2");'
        $shot = Exec 'rd_shoot' 'var d = GameObject.Find("Dummy"); var chest = d.GetComponent<Animator>().GetBonePosition(HumanBodyBones.Chest); var s = Camera.main.GetComponent<StarterAssets.RagdollShooter>(); s.readMouse = false; bool hit = s.ShootAt(chest); var t = d.GetComponent<StarterAssets.RagdollTarget>(); return hit + " " + t.down + " " + s.lastHit + " " + t.lastBody;'
        Wait-Sec 2.5
        Invoke-Nova "screenshot `"$(Join-Path $dir 'ragdoll.png')`" --view game" | Out-Null
        $after = Exec 'rd_after' 'var d = GameObject.Find("Dummy"); Rigidbody pelvis = null; float lo = 9; foreach (var rb in d.GetComponentsInChildren<Rigidbody>()) { if (pelvis == null || rb.gameObject.name.Contains("Pelvis")) pelvis = rb; lo = Mathf.Min(lo, rb.position.y); } var p = pelvis.position; return d.GetComponent<Ragdoll>().active + " " + p.y.ToString("F2") + " " + p.z.ToString("F2") + " " + pelvis.gameObject.name;'
        $b = "$before" -split ' '; $a = "$after" -split ' '
        $ok = "$shot" -match '^True True ' -and $a.Count -ge 4 -and $a[0] -eq 'True' -and $b.Count -eq 2 -and [double]$a[1] -lt [double]$b[0] - 0.3 -and [double]$a[2] -gt [double]$b[1] + 0.1
        Add-Result starter 'shooting the target: ragdoll falls, pushed away from the camera' $ok "shot: $shot / hips before y $($b[0]) z $($b[1]) → $($a[3]) y $($a[1]) z $($a[2])"
        # 일어나기: 누운 방향의 클립 (등 = GetUpBack, 배 = GetUpFront) + 쓰러진 자세에서 섞기, 루트는 골반 자리로 → 끝나면 Idle (표적에는 Character Controller 가 없다)
        $gu = Exec 'rd_getup' 'var c = System.Globalization.CultureInfo.InvariantCulture; var d = GameObject.Find("Dummy"); var r = d.GetComponent<Ragdoll>(); Rigidbody pelvis = null; foreach (var rb in d.GetComponentsInChildren<Rigidbody>()) if (rb.gameObject.name.Contains("Pelvis")) pelvis = rb; var pp = pelvis.position; bool up = r.isFaceUp; d.GetComponent<StarterAssets.RagdollTarget>().Recover(); return up + " " + pp.x.ToString("F2", c) + " " + pp.z.ToString("F2", c);'
        Wait-Sec 0.2
        $guRead = 'var c = System.Globalization.CultureInfo.InvariantCulture; var d = GameObject.Find("Dummy"); var r = d.GetComponent<Ragdoll>(); var t = d.GetComponent<StarterAssets.RagdollTarget>(); var a = d.GetComponent<Animator>(); var p = d.transform.position; return a.GetCurrentAnimatorStateInfo(0).name + " " + r.isBlending + " " + t.gettingUp + " " + t.down + " " + p.x.ToString("F2", c) + " " + p.z.ToString("F2", c) + " " + a.GetBonePosition(HumanBodyBones.Head).y.ToString("F2", c) + " " + r.active;'
        $g1 = (Exec 'rd_getup1' $guRead) -split ' '
        Wait-Sec 0.8
        Invoke-Nova "screenshot `"$(Join-Path $dir 'getup.png')`" --view game" | Out-Null
        Wait-Sec 2.5
        $g2 = (Exec 'rd_getup2' $guRead) -split ' '
        $gs = "$gu" -split ' '
        $clip = if ($gs.Count -eq 3 -and $gs[0] -eq 'True') { 'GetUpBack' } else { 'GetUpFront' }
        $okG = $gs.Count -eq 3 -and $g1.Count -eq 8 -and $g1[0] -eq $clip -and $g1[2] -eq 'True' -and $g1[3] -eq 'False' -and $g1[7] -eq 'False' -and
            [math]::Abs([double]$g1[4] - [double]$gs[1]) -lt 0.05 -and [math]::Abs([double]$g1[5] - [double]$gs[2]) -lt 0.05 -and
            $g2.Count -eq 8 -and $g2[0] -eq 'Idle' -and $g2[1] -eq 'False' -and $g2[2] -eq 'False' -and $g2[3] -eq 'False' -and [double]$g2[6] -gt [double]$g1[6] + 0.4
        Add-Result starter 'get up: clip for the fallen side, root moved to the pelvis, blends from the ragdoll pose, then Idle (controls back on)' $okG "face up $($gs[0]) → $($g1[0]), blending $($g1[1]), root $($g1[4]),$($g1[5]) (pelvis $($gs[1]),$($gs[2])), head y $($g1[6]) → $($g2[6]); after: $($g2[0]) blending $($g2[1]) gettingUp $($g2[2]) down $($g2[3])"
        Invoke-Nova 'stop' | Out-Null
        # ---- 차에 타고 내리기 (VehicleEnterExit — create player 가 붙인다): 캐릭터를 숨기고 차를 운전, 카메라가 차 뒤로 → 운전석 쪽에 내려 다시 걷기
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,30 --scale 100,1,100' | Out-Null
        Invoke-Nova 'create car --name Car --position 3,0,0' | Out-Null
        Invoke-Nova 'create player' | Out-Null
        Wait-Compile
        Invoke-Nova 'play' | Out-Null
        Wait-Sec 2
        $veRead = 'var c = System.Globalization.CultureInfo.InvariantCulture; var pl = GameObject.Find("Player"); var v = pl.GetComponent<StarterAssets.VehicleEnterExit>(); var car = GameObject.Find("Car"); var f = Camera.main.GetComponent<FollowCamera>(); var p = pl.transform.position; var q = car.transform.position; return v.driving + " " + pl.GetComponentInChildren<SkinnedMeshRenderer>().enabled + " " + pl.GetComponent<CharacterController>().enabled + " " + (f.target != null ? f.target.name : "none") + " " + f.followTargetRotation + " " + p.x.ToString("F2", c) + " " + p.y.ToString("F2", c) + " " + p.z.ToString("F2", c) + " " + q.x.ToString("F2", c) + " " + q.z.ToString("F2", c) + " " + Camera.main.transform.position.z.ToString("F2", c);'
        $e0 = (Exec 've_enter' 'var v = GameObject.Find("Player").GetComponent<StarterAssets.VehicleEnterExit>(); v.readKeyboard = false; var near = v.NearestCar(); return (near != null ? near.name : "none") + " " + v.Enter(near) + " " + near.readKeyboard + " " + near.handbrake;') -split ' '
        $e1 = (Exec 've_in' $veRead) -split ' '
        Add-Result starter 'enter car: nearest car within reach, character hidden, controller off, Follow Camera on the car' (($e0 -join ' ') -eq 'Car True False False' -and $e1.Count -eq 11 -and $e1[0] -eq 'True' -and $e1[1] -eq 'False' -and $e1[2] -eq 'False' -and $e1[3] -eq 'Car' -and $e1[4] -eq 'True') "enter: $($e0 -join ' '); driving $($e1[0]) visible $($e1[1]) controller $($e1[2]) camera $($e1[3]) turns with it $($e1[4])"
        Exec 've_drive' 'var k = GameObject.Find("Car").GetComponent<StarterAssets.CarController>(); k.throttle = 1f; return 1;' | Out-Null
        Wait-Sec 3
        Invoke-Nova "screenshot `"$(Join-Path $dir 'drive.png')`" --view game" | Out-Null
        $e2 = (Exec 've_driving' $veRead) -split ' '
        Add-Result starter 'driving: the car moves, the hidden character rides along, the camera follows behind' ($e2.Count -eq 11 -and [double]$e2[9] -gt 8 -and [math]::Abs([double]$e2[7] - [double]$e2[9]) -lt 1.0 -and [double]$e2[10] -lt [double]$e2[9] - 3) "car z $($e2[9]), character z $($e2[7]), camera z $($e2[10])"
        Exec 've_stop' 'var k = GameObject.Find("Car").GetComponent<StarterAssets.CarController>(); k.throttle = 0f; k.handbrake = true; return 1;' | Out-Null
        Wait-Sec 2
        $x0 = Exec 've_exit' 'return GameObject.Find("Player").GetComponent<StarterAssets.VehicleEnterExit>().Exit() + "";'
        $e3 = (Exec 've_out' $veRead) -split ' '
        $side = if ($e3.Count -eq 11) { [math]::Sqrt([math]::Pow([double]$e3[5] - [double]$e3[8], 2) + [math]::Pow([double]$e3[7] - [double]$e3[9], 2)) } else { -1 }
        Exec 've_walk' 'var i = GameObject.Find("Player").GetComponent<StarterAssets.StarterAssetsInputs>(); i.readKeyboard = false; i.move = new Vector2(0, 1); return 1;' | Out-Null
        Wait-Sec 1.5
        $e4 = (Exec 've_walked' $veRead) -split ' '
        $okExit = $x0 -eq 'True' -and $e3.Count -eq 11 -and $e3[0] -eq 'False' -and $e3[1] -eq 'True' -and $e3[2] -eq 'True' -and $e3[3] -eq 'Player' -and $e3[4] -eq 'False' -and
            $side -gt 1.4 -and $side -lt 2.3 -and [double]$e3[5] -lt [double]$e3[8] -and [math]::Abs([double]$e3[6]) -lt 0.3 -and $e4.Count -eq 11 -and [double]$e4[7] -gt [double]$e3[7] + 0.5
        Add-Result starter 'exit car: on the ground at the driver side, visible, camera back on the character, walks again' $okExit "exit $x0; character x $($e3[5]) y $($e3[6]) (car x $($e3[8]), $([math]::Round($side, 2)) m to the side), camera $($e3[3]) turns $($e3[4]); walked z $($e3[7]) → $($e4[7])"
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item "$assetDir.meta" -Force -ErrorAction SilentlyContinue
        if ($manifestBefore) { [IO.File]::WriteAllBytes($manifest, $manifestBefore) }
    }
}

function Suite-Behaviour
{
    # C# Behaviour.enabled · Collider.enabled → 네이티브 컴포넌트 (Inspector 체크 상자): Animator 를 끄면 멈추고 다시 켜면 간다,
    #  Directional Light 를 끄면 바닥이 어두워진다, 바닥 Collider 를 끄면 위의 상자가 떨어진다,
    #  activeInHierarchy: 부모를 끄면 자식 (메시 · 스킨 메시 · 그림자) 이 안 보이고, 자식 스크립트 Update 가 멈추며 OnDisable · 소리 멈춤 (다시 켜면 OnEnable · 다시 재생)
    Write-Host '[behaviour]'
    $dir = Join-Path $Out 'behaviour'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create plane --name Ground --scale 3,1,3' | Out-Null
        Invoke-Nova 'create character --name Ch' | Out-Null
        Invoke-Nova 'create cube --name Floor --position 4,1,0 --scale 2,0.2,2' | Out-Null
        Invoke-Nova 'create cube --name Box --position 4,2,0 --scale 0.5,0.5,0.5' | Out-Null
        Invoke-Nova 'add-component Box RigidBody' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,3,-6 --rotation 25,0,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Add-Type -AssemblyName System.Drawing
        function GroundLum([string]$png)
        {
            $b = New-Object System.Drawing.Bitmap $png
            try { $s = 0.0; $n = 0; for ($y = [int]($b.Height * 0.8); $y -lt [int]($b.Height * 0.95); $y += 3) { for ($x = [int]($b.Width * 0.1); $x -lt [int]($b.Width * 0.4); $x += 3) { $c = $b.GetPixel($x, $y); $s += ($c.R + $c.G + $c.B) / 3.0; $n++ } }; return $s / $n }
            finally { $b.Dispose() }
        }
        function Cs([string]$name, [string]$code) { $f = Join-Path $dir "$name.cs"; $code | Set-Content -Encoding utf8 $f; return (Invoke-NovaJson "exec --file `"$f`"").result }

        Invoke-Nova 'play' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 1.5) { Invoke-Nova 'wait 10' | Out-Null }

        # Animator
        $off = Cs 'anim_off' 'var a = GameObject.Find("Ch").GetComponent<Animator>(); a.enabled = false; return a.enabled + " " + a.GetCurrentAnimatorStateInfo(0).normalizedTime.ToString("F4");'
        Invoke-Nova 'wait 30' | Out-Null
        $still = Cs 'anim_still' 'return GameObject.Find("Ch").GetComponent<Animator>().GetCurrentAnimatorStateInfo(0).normalizedTime.ToString("F4");'
        $json = Invoke-NovaJson 'get Ch --component Animator'
        $on = Cs 'anim_on' 'var a = GameObject.Find("Ch").GetComponent<Animator>(); a.enabled = true; return a.enabled.ToString();'
        Invoke-Nova 'wait 30' | Out-Null
        $moved = Cs 'anim_moved' 'return GameObject.Find("Ch").GetComponent<Animator>().GetCurrentAnimatorStateInfo(0).normalizedTime.ToString("F4");'
        $ov = "$off" -split ' '
        Add-Result behaviour 'Animator.enabled = false stops it (Inspector flag too), true resumes' ($ov.Count -eq 2 -and $ov[0] -eq 'False' -and $ov[1] -eq "$still" -and $json.enabled -eq $false -and "$on" -eq 'True' -and "$moved" -ne "$still") "off: $off, after 30 frames $still, json enabled $($json.enabled); on: $on, after 30 frames $moved"

        # Light
        Invoke-Nova 'wait 3' | Out-Null
        $litPng = Join-Path $dir 'lit.png'
        Invoke-Nova "screenshot `"$litPng`" --view game" | Out-Null
        $lres = Cs 'light_off' 'var l = GameObject.Find("Directional Light").GetComponent<Light>(); l.enabled = false; return l.enabled.ToString();'
        Invoke-Nova 'wait 5' | Out-Null
        $darkPng = Join-Path $dir 'dark.png'
        Invoke-Nova "screenshot `"$darkPng`" --view game" | Out-Null
        $lj = Invoke-NovaJson 'get "Directional Light" --component Light'
        $lit = GroundLum $litPng; $dark = GroundLum $darkPng
        Add-Result behaviour 'Light.enabled = false: the scene loses that light' ("$lres" -eq 'False' -and $lj.enabled -eq $false -and $dark -lt $lit * 0.9) ("ground {0:F0} → {1:F0}, C# {2}, json enabled {3}" -f $lit, $dark, $lres, $lj.enabled)

        # Collider
        $y0 = [double](Invoke-NovaJson 'get Box').position[1]
        $cres = Cs 'col_off' 'GameObject.Find("Floor").GetComponent<Collider>().enabled = false; return GameObject.Find("Floor").GetComponent<BoxCollider>().enabled.ToString();'
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 1.5) { Invoke-Nova 'wait 10' | Out-Null }
        $y1 = [double](Invoke-NovaJson 'get Box').position[1]
        Add-Result behaviour 'Collider.enabled = false: the box on it falls through' ("$cres" -eq 'False' -and $y0 -gt 1.1 -and $y1 -lt 0.5) ("box y {0:F2} on the floor → {1:F2} (C# enabled {2})" -f $y0, $y1, $cres)
        Invoke-Nova 'stop' | Out-Null

        # ---- activeInHierarchy (Unity): 부모를 끄면 자식도 꺼진다 — 그리기 (메시 · 스킨 메시 · 그림자) · Update · OnDisable · 소리
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create plane --name Ground --scale 3,1,3' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,2,-6 --rotation 12,0,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        function Shot([string]$name) { Invoke-Nova 'wait 6' | Out-Null; $p = Join-Path $dir "$name.png"; Invoke-Nova "screenshot `"$p`" --view game" | Out-Null; return $p }
        function MeanDiff([string]$a, [string]$b)
        {
            $x = New-Object System.Drawing.Bitmap $a; $y = New-Object System.Drawing.Bitmap $b
            try
            {
                $s = 0.0; $n = 0
                for ($j = 0; $j -lt $x.Height; $j += 3) { for ($i = 0; $i -lt $x.Width; $i += 3) { $c = $x.GetPixel($i, $j); $d = $y.GetPixel($i, $j); $s += [math]::Abs($c.R - $d.R) + [math]::Abs($c.G - $d.G) + [math]::Abs($c.B - $d.B); $n++ } }
                return $s / (3 * $n)
            }
            finally { $x.Dispose(); $y.Dispose() }
        }
        $bg = Shot 'active_bg'
        Invoke-Nova 'create empty --name Parent' | Out-Null
        Invoke-Nova "create cube --name Kid --parent Parent --position -1.5,0.5,0" | Out-Null
        Invoke-Nova "create character --name Ch2 --position 1.2,0,0" | Out-Null
        $on = Shot 'active_on'
        Invoke-Nova 'set Parent --active false' | Out-Null
        Invoke-Nova 'set Ch2 --active false' | Out-Null
        $off = Shot 'active_off'
        $dOn = MeanDiff $on $bg; $dOff = MeanDiff $off $bg
        Add-Result behaviour 'inactive parent hides its children (cube under an empty, character skinned meshes, shadows)' ($dOn -gt 0.4 -and $dOff -lt 0.2) ("difference from the empty scene: active {0:F2} (> 0.4), parent off {1:F2} (< 0.2)" -f $dOn, $dOff)

        # Play: 자식의 스크립트 · 소리 — 부모를 끄면 Update 멈춤 · OnDisable · 소리 멈춤, 다시 켜면 OnEnable · Play On Awake 로 다시
        $probeDir = Join-Path $Project 'Assets\ActiveProbe'
        New-Item -ItemType Directory -Force $probeDir | Out-Null
        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        Copy-Item (Join-Path $PSScriptRoot 'active_probe.cs') (Join-Path $probeDir 'ActiveProbe.cs') -Force
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw.Elapsed.TotalSeconds -lt 60 -and (($inf -and $inf.compiling) -or $now -eq $dllBefore))
        Invoke-Nova 'create empty --name Parent2' | Out-Null
        Invoke-Nova 'create empty --name Kid2 --parent Parent2' | Out-Null
        Invoke-Nova 'add-component Kid2 AudioSource --values "{\"clip\":\"Assets/TestAssets/Audio/long.mp3\",\"loop\":true,\"playOnAwake\":true,\"volume\":0.05}"' | Out-Null
        Invoke-Nova 'add-component Kid2 ActiveProbe' | Out-Null
        Invoke-Nova 'create particle-system --name Sparks --parent Parent2' | Out-Null
        Invoke-Nova 'create character --name Ch3 --parent Parent2 --position 3,0,4' | Out-Null
        Invoke-Nova 'play' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 1) { Invoke-Nova 'wait 10' | Out-Null }
        $s0 = "$(Cs 'act_0' 'return ActiveProbe.State();')" -split ' '
        $x0 = "$(Cs 'act_x0' 'return ActiveProbe.Extra();')" -split ' '
        Cs 'act_off' 'ActiveProbe.parent.SetActive(false); return 1;' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $s1 = "$(Cs 'act_1' 'return ActiveProbe.State();')" -split ' '
        $x1 = "$(Cs 'act_x1' 'return ActiveProbe.Extra();')" -split ' '
        Invoke-Nova 'wait 20' | Out-Null
        $s2 = "$(Cs 'act_2' 'return ActiveProbe.State() + " " + (GameObject.Find("Kid2") == null);')" -split ' '
        $x2 = "$(Cs 'act_x2' 'return ActiveProbe.Extra();')" -split ' '
        Cs 'act_on' 'ActiveProbe.parent.SetActive(true); return 1;' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $s3 = "$(Cs 'act_3' 'return ActiveProbe.State();')" -split ' '
        $x3 = "$(Cs 'act_x3' 'return ActiveProbe.Extra();')" -split ' '
        # Animator: 꺼진 동안 시각이 멈춰 있고 (두 번 읽어 같다), 다시 켜면 기본 상태부터 (멈춘 시각보다 작다 — 그대로 이어 가면 더 크다)
        $okX = $x0.Count -eq 3 -and [int]$x0[0] -gt 0 -and $x0[1] -eq 'True' -and $x1.Count -eq 3 -and [int]$x1[0] -eq 0 -and $x1[1] -eq 'False' -and
            $x2.Count -eq 3 -and $x2[2] -eq $x1[2] -and $x3.Count -eq 3 -and $x3[1] -eq 'True' -and [double]$x3[2] -lt [double]$x2[2]
        Add-Result behaviour 'inactive parent: particles cleared and replayed, Animator frozen then restarts from its default state' $okX "particles playing animTime — active: $($x0 -join ' '), off: $($x1 -join ' ') → $($x2 -join ' '), on again: $($x3 -join ' ')"
        $ok = $s0.Count -eq 4 -and [int]$s0[0] -gt 0 -and $s0[1] -eq '1' -and $s0[2] -eq '0' -and $s0[3] -eq 'True' -and
            $s1.Count -eq 4 -and $s1[2] -eq '1' -and $s1[3] -eq 'False' -and $s2.Count -eq 5 -and $s2[0] -eq $s1[0] -and $s2[4] -eq 'True' -and
            $s3.Count -eq 4 -and $s3[1] -eq '2' -and [int]$s3[0] -gt [int]$s2[0] -and $s3[3] -eq 'True'
        Add-Result behaviour 'inactive parent: child script stops updating (OnDisable / OnEnable), its audio stops and replays' $ok "updates enables disables playing — active: $($s0 -join ' '), parent off: $($s1 -join ' ') → $($s2[0..3] -join ' ') (Find = null $($s2[4])), on again: $($s3 -join ' ')"
        Invoke-Nova 'stop' | Out-Null
        Remove-Item $probeDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item "$probeDir.meta" -Force -ErrorAction SilentlyContinue
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
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
        # 4 프레임 · 12 fps = 한 바퀴 0.33 초: 한 번만 비교하면 간격이 한 바퀴와 겹쳐 같은 프레임이 나올 수 있다 → 몇 번 읽어 바뀐 것을 찾는다
        $p1f = ("$($f1.result)" -split ' ')[2]
        foreach ($wait in 0.15, 0.1, 0.12)
        {
            Wait-Sec $wait
            $f2 = Invoke-NovaJson "exec --file $cf"
            if (("$($f2.result)" -split ' ')[2] -ne $p1f) { break }
        }
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

function Suite-ProbeVolume
{
    # Adaptive Probe Volume (실시간 간접광): 닫힌 방 안이 어두움 · 색 번짐 · 굽기 없이 따라감 (빛깔 · 지붕) · 저장
    Write-Host '[probevolume]'
    $dir = Join-Path $Out 'probevolume'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\APVTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $base = Get-Content (Join-Path $Project 'Assets\Materials\Red Plastic.mat') -Raw | ConvertFrom-Json
    function MakeMat([string]$name, $color)
    {
        $m = $base.PSObject.Copy(); $m.BaseColor = $color; $m.Metallic = 0.0; $m.Smoothness = 0.1; $m.ResourcePath = "Assets\APVTest\$name.mat"
        $m | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir "$name.mat")
    }
    MakeMat 'Red' @(0.9, 0.05, 0.05, 1)
    MakeMat 'Green' @(0.05, 0.9, 0.05, 1)
    MakeMat 'White' @(0.9, 0.9, 0.9, 1)
    $ed = Start-TestEditor
    try
    {
        # 큰 셰이더 (32) 가 백그라운드로 다 만들어질 때까지 (그동안은 예전 셰이더로 그린다)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 120)
        {
            $t = Invoke-Nova 'log -n 400'
            if ($t -match 'compiled 32\. InstancedBasic|cache hit 32\. InstancedBasic') { break }
            Start-Sleep -Milliseconds 500
        }
        function Mat([string]$obj, [string]$mat) { Invoke-Nova ("set $obj --component MeshRenderer --values `"{\`"m_MaterialPaths\`":[\`"Assets/APVTest/$mat.mat\`"]}`"") | Out-Null }
        function Shot([string]$name, [double]$x0, [double]$x1, [double]$y0, [double]$y1)
        {
            $p = Join-Path $dir $name
            Invoke-Nova "screenshot $p --view scene" | Out-Null
            if (-not (Test-Path $p)) { return $null }
            $bm = [System.Drawing.Bitmap]::FromFile($p); $n = 0; $r = 0; $g = 0; $b = 0
            for ($y = [int]($bm.Height * $y0); $y -lt [int]($bm.Height * $y1); $y += 2) { for ($x = [int]($bm.Width * $x0); $x -lt [int]($bm.Width * $x1); $x += 2) { $c = $bm.GetPixel($x, $y); $n++; $r += $c.R; $g += $c.G; $b += $c.B } }
            $bm.Dispose(); $n = [math]::Max(1, $n)
            [pscustomobject]@{ R = $r / $n; G = $g / $n; B = $b / $n; L = (0.2126 * $r + 0.7152 * $g + 0.0722 * $b) / $n }
        }
        function Txt($s) { if ($s) { 'rgb={0:N0},{1:N0},{2:N0}' -f $s.R, $s.G, $s.B } else { 'no capture' } }
        function Up($pos) { $j = Invoke-NovaJson "probevolume probe --position $pos"; $u = $j.cascades[0].probe.ambientUp; if ($u) { 0.2126 * $u[0] + 0.7152 * $u[1] + 0.0722 * $u[2] } else { -1 } }

        # ---- 닫힌 방 (벽 · 지붕 0.3 m)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 30,1,30' | Out-Null
        Invoke-Nova 'create cube --name WallL --position -3,1.5,0 --scale 0.3,3,6' | Out-Null
        Invoke-Nova 'create cube --name WallR --position 3,1.5,0 --scale 0.3,3,6' | Out-Null
        Invoke-Nova 'create cube --name WallB --position 0,1.5,3 --scale 6.3,3,0.3' | Out-Null
        Invoke-Nova 'create cube --name WallF --position 0,1.5,-3 --scale 6.3,3,0.3' | Out-Null
        Invoke-Nova 'create cube --name Roof --position 0,3.15,0 --scale 6.6,0.3,6.6' | Out-Null
        Invoke-Nova 'create cube --name Box --position 0,0.5,1 --scale 1,1,1' | Out-Null
        Invoke-Nova 'camera --position 0,1.6,-2.5 --target 0,0.8,2' | Out-Null
        Invoke-Nova 'wait 30' | Out-Null
        $sky = Shot 'room_sky.png' 0.3 0.7 0.2 0.8
        Invoke-Nova 'create empty --name APV --position 0,1,0' | Out-Null
        Invoke-Nova 'add-component APV AdaptiveProbeVolume' | Out-Null
        Invoke-Nova 'camera --position 0,1.6,-2.5 --target 0,0.8,2' | Out-Null
        Invoke-Nova 'wait 300' | Out-Null
        $info = Invoke-NovaJson 'probevolume info'
        $live = @($info.cascades | Where-Object { $_.voxelsLive }).Count
        Add-Result probevolume 'Adaptive Probe Volume starts by itself (no bake): 3 cascades of 32 x 16 x 32 probes with live voxels' ($info.active -and $live -eq 3 -and $info.probesPerCascade -eq 16384) "active=$($info.active) live=$live probes=$($info.probesPerCascade)"
        $in = Up '0,1.5,0'; $outside = Up '8,1.5,0'
        Add-Result probevolume 'closed room: probe inside gets far less light than outside (walls · roof block the sky)' ($in -ge 0 -and $outside -gt 0 -and $in -lt $outside * 0.2) ("inside={0:N3} outside={1:N3}" -f $in, $outside)
        $apv = Shot 'room_apv.png' 0.3 0.7 0.2 0.8
        Add-Result probevolume 'closed room on screen: much darker than sky-only ambient' ($sky -and $apv -and $apv.L -lt $sky.L * 0.6) ("sky L={0:N0} apv L={1:N0}" -f $sky.L, $apv.L)

        # 지붕을 치우면 (장면이 바뀜) 굽지 않아도 방 안이 밝아진다
        Invoke-Nova 'delete Roof' | Out-Null
        Invoke-Nova 'wait 300' | Out-Null
        $open = Up '0,1.5,0'
        Add-Result probevolume 'remove the roof: the scene change re-voxelizes by itself and the room fills with sky light' ($open -gt $in * 3) ("closed={0:N3} open={1:N3}" -f $in, $open)

        # ---- 색 번짐: 해를 받는 빨간 벽 옆 흰 벽 (그늘 쪽)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 30,1,30' | Out-Null
        Mat 'Ground' 'White'
        Invoke-Nova 'create cube --name ColorWall --position 0,1.5,2 --scale 4,3,0.3' | Out-Null
        Mat 'ColorWall' 'Red'
        Invoke-Nova 'create cube --name WhiteWall --position 2,1.5,0.3 --scale 0.3,3,3.4' | Out-Null
        Mat 'WhiteWall' 'White'
        Invoke-Nova 'camera --position -2.5,1.6,-2.5 --target 1.8,1.2,0.8' | Out-Null
        Invoke-Nova 'wait 30' | Out-Null
        $w0 = Shot 'bleed_sky.png' 0.50 0.66 0.35 0.70
        Invoke-Nova 'create empty --name APV --position 0,1,0' | Out-Null
        Invoke-Nova 'add-component APV AdaptiveProbeVolume' | Out-Null
        Invoke-Nova 'camera --position -2.5,1.6,-2.5 --target 1.8,1.2,0.8' | Out-Null
        Invoke-Nova 'wait 300' | Out-Null
        $w1 = Shot 'bleed_red.png' 0.50 0.66 0.35 0.70
        Add-Result probevolume 'color bleeding: the shaded white wall turns reddish from the sunlit red wall' ($w0 -and $w1 -and ($w1.R - $w1.B) -gt ($w0.R - $w0.B) + 6) "sky: $(Txt $w0) | apv: $(Txt $w1)"
        Mat 'ColorWall' 'Green'
        Invoke-Nova 'wait 300' | Out-Null
        $w2 = Shot 'bleed_green.png' 0.50 0.66 0.35 0.70
        Add-Result probevolume 'realtime: paint the wall green — the bounce turns green with no bake' ($w1 -and $w2 -and ($w2.G - $w2.R) -gt ($w1.G - $w1.R) + 8) "red: $(Txt $w1) | green: $(Txt $w2)"

        # ---- 저장 → 다시 열기
        Invoke-Nova 'set APV --component AdaptiveProbeVolume --values "{\"probeSpacing\":0.5,\"cascades\":2,\"raysPerProbe\":64,\"intensityMultiplier\":1.5}"' | Out-Null
        Invoke-Nova 'scene save --as Assets/APVTest/APVScene.scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'scene open Assets/APVTest/APVScene.scene --force' | Out-Null
        $g = Invoke-NovaJson 'get APV'
        $c = @($g.components | Where-Object { $_.type -eq 'AdaptiveProbeVolume' })[0]
        Add-Result probevolume 'saved and reopened: spacing, cascades, rays, intensity kept' ($c -and [double]$c.probeSpacing -eq 0.5 -and $c.cascades -eq 2 -and $c.raysPerProbe -eq 64 -and [double]$c.intensityMultiplier -eq 1.5) "spacing=$($c.probeSpacing) cascades=$($c.cascades) rays=$($c.raysPerProbe) intensity=$($c.intensityMultiplier)"
        # ---- 발광 재질 (APV 2 단계): 닫힌 방 (햇빛 없음) 안의 주황 발광 구가 바닥 · 벽을 비춘다 — 굽기 없이
        $lamp = $base.PSObject.Copy(); $lamp.BaseColor = @(1, 0.6, 0.2, 1); $lamp.Metallic = 0.0; $lamp.Smoothness = 0.2; $lamp.ResourcePath = 'Assets\APVTest\Lamp.mat'
        $lamp.Emission = $true; $lamp.EmissionColor = @(1, 0.55, 0.15); $lamp.EmissionIntensity = 8
        $lamp | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir 'Lamp.mat')
        function UpRgb($pos) { $j = Invoke-NovaJson "probevolume probe --position $pos"; $j.cascades[0].probe.ambientUp }
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 30,1,30' | Out-Null; Mat 'Ground' 'White'
        foreach ($w in @(@('WallL', '-3,1.5,0', '0.3,3,6'), @('WallR', '3,1.5,0', '0.3,3,6'), @('WallB', '0,1.5,3', '6.3,3,0.3'), @('WallF', '0,1.5,-3', '6.3,3,0.3'), @('Roof', '0,3.15,0', '6.6,0.3,6.6')))
        {
            Invoke-Nova "create cube --name $($w[0]) --position $($w[1]) --scale $($w[2])" | Out-Null; Mat $w[0] 'White'
        }
        Invoke-Nova 'create sphere --name Lamp --position 0,0.5,0.8 --scale 0.8,0.8,0.8' | Out-Null; Mat 'Lamp' 'White'
        Invoke-Nova 'create empty --name APV --position 0,1,0' | Out-Null
        Invoke-Nova 'add-component APV AdaptiveProbeVolume' | Out-Null
        Invoke-Nova 'select Ground' | Out-Null
        Invoke-Nova 'camera --position -0.5,1.2,-1.5 --target -3,0.6,3' | Out-Null
        Invoke-Nova 'wait 240' | Out-Null
        $e0 = UpRgb '-1.5,0.6,0.8'
        # 방 안쪽 모서리 (왼쪽 벽 · 뒤 벽 · 천장 · 바닥이 만나는 곳): 양옆보다 밝은 1 px 선 (예전엔 벽 너머 프로브가 모서리 복셀로 새어 생겼다)
        function Ridges([string]$pc)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($pc); $ridge = 0; $rows = 0
            for ($y = [int]($bm.Height * 0.05); $y -lt [int]($bm.Height * 0.55); $y += 2) {   # 바닥 (격자선) 위
                $rows++
                for ($x = [int]($bm.Width * 0.3); $x -lt [int]($bm.Width * 0.7); $x++) {
                    $a = $bm.GetPixel($x - 2, $y); $m = $bm.GetPixel($x, $y); $b = $bm.GetPixel($x + 2, $y)
                    $la = 0.3 * $a.R + 0.59 * $a.G + 0.11 * $a.B; $lm = 0.3 * $m.R + 0.59 * $m.G + 0.11 * $m.B; $lb = 0.3 * $b.R + 0.59 * $b.G + 0.11 * $b.B
                    if ($lm -gt $la + 4 -and $lm -gt $lb + 4) { $ridge++ } } }
            $bm.Dispose(); @($ridge, $rows)
        }
        $pc = Join-Path $dir 'corner.png'; Invoke-Nova "screenshot $pc --view scene" | Out-Null
        $rl = Ridges $pc
        Invoke-Nova 'camera --position 0.5,1.2,-1.5 --target 3,0.6,3' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
        $pr = Join-Path $dir 'corner_right.png'; Invoke-Nova "screenshot $pr --view scene" | Out-Null
        $rr = Ridges $pr
        Add-Result probevolume 'room corners (+X/-Z and -X/-Z walls): no thin bright line where the walls meet (6 face slots per voxel)' ($rl[0] -lt $rl[1] * 0.1 -and $rr[0] -lt $rr[1] * 0.1) ("bright 1-px ridges: left {0} / {1} rows, right {2} / {3} rows" -f $rl[0], $rl[1], $rr[0], $rr[1])
        Mat 'Lamp' 'Lamp'
        Invoke-Nova 'wait 240' | Out-Null
        $e1 = UpRgb '-1.5,0.6,0.8'
        $i = Invoke-NovaJson 'probevolume info'
        Add-Result probevolume 'emissive material lights the dark room (orange) without a bake' ($e0 -and $e1 -and $e1[0] -gt $e0[0] * 5 -and $e1[0] -gt $e1[2] * 3 -and $i.emissiveRenderers -eq 1) ("probe near the lamp {0:N3},{1:N3},{2:N3} -> {3:N3},{4:N3},{5:N3}, emissive renderers {6}" -f $e0[0], $e0[1], $e0[2], $e1[0], $e1[1], $e1[2], $i.emissiveRenderers)
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-DepthOfField
{
    # Volume 후처리 Depth Of Field (Gaussian · Bokeh) · Motion Blur: 가장자리 선명도 (옆 픽셀 밝기 차의 합) 로 흐림을 잰다
    Write-Host '[depthoffield]'
    $dir = Join-Path $Out 'depthoffield'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\DofTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    function Profile([string]$name, [string]$type, [hashtable]$values)
    {
        $params = @{}
        foreach ($k in $values.Keys) { $params[$k] = @{ override = $true; value = @($values[$k], 0, 0, 0) } }
        @{ nova_volume_profile = 1; components = @(@{ type = $type; active = $true; params = $params }) } | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $assetDir "$name.volumeprofile")
    }
    Profile 'Gauss' 'DepthOfField' @{ mode = 1; gaussianStart = 8; gaussianEnd = 16; gaussianMaxRadius = 1.5 }
    Profile 'BokehFar' 'DepthOfField' @{ mode = 2; focusDistance = 20; focalLength = 85; aperture = 1.4 }
    Profile 'DofOff' 'DepthOfField' @{ mode = 0; gaussianStart = 1; gaussianEnd = 2 }
    Profile 'Motion' 'MotionBlur' @{ intensity = 1; clamp = 0.1; quality = 2 }
    '{"nova_volume_profile": 1, "components": []}' | Set-Content -Encoding utf8 (Join-Path $assetDir 'None.volumeprofile')
    @'
using NovaEngine;

// Motion Blur 검사: 카메라를 프레임마다 2 도씩 돌린다
public class DofTestSpin : MonoBehaviour
{
    void Update() { transform.Rotate(0f, 2f, 0f); }   // 프레임마다 같은 각 — 켜고 끈 두 번이 같은 화면, 프레임 속도와 상관없는 흐림
}
'@ | Set-Content -Encoding utf8 (Join-Path $assetDir 'DofTestSpin.cs')
    $ed = Start-TestEditor
    try
    {
        function Vol([string]$name) { Invoke-Nova ('set "Global Volume" --component Volume --values "{\"profile\":\"Assets/DofTest/' + $name + '.volumeprofile\"}"') | Out-Null }
        # 상자 윤곽의 가장 큰 밝기 계단 (줄마다 가로 이웃 차의 최대값, 줄 평균) — 초점이 맞으면 계단이 그대로
        function Edge([string]$p, [double]$x0, [double]$x1, [double]$y0, [double]$y1)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($p); $sum = 0.0; $rows = 0
            for ($y = [int]($bm.Height * $y0); $y -lt [int]($bm.Height * $y1); $y++) {
                $best = 0.0
                for ($x = [int]($bm.Width * $x0); $x -lt [int]($bm.Width * $x1) - 1; $x++) {
                    $c = $bm.GetPixel($x, $y); $r = $bm.GetPixel($x + 1, $y)
                    $d = [math]::Abs((0.3 * $c.R + 0.59 * $c.G + 0.11 * $c.B) - (0.3 * $r.R + 0.59 * $r.G + 0.11 * $r.B))
                    if ($d -gt $best) { $best = $d } }
                $sum += $best; $rows++ }
            $bm.Dispose(); $sum / [math]::Max(1, $rows)
        }
        function Shot([string]$name, [string]$view = 'scene') { $p = Join-Path $dir $name; Invoke-Nova 'wait 10' | Out-Null; Invoke-Nova "screenshot $p --view $view" | Out-Null; $p }
        $near = @(0.11, 0.19, 0.42, 0.72)   # 4 m 상자의 왼쪽 윤곽 (화면 영역)
        $far = @(0.49, 0.525, 0.47, 0.53)   # 20 m 상자의 왼쪽 윤곽

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,20 --scale 40,1,80' | Out-Null
        $i = 0
        foreach ($z in @(4, 10, 20, 40)) { Invoke-Nova "create cube --name C$z --position $(-3 + $i * 2),1,$z --scale 1.2,2,1.2" | Out-Null; $i++ }
        Invoke-Nova 'camera --position 0,1.3,-1 --target 0,1,30' | Out-Null
        Vol 'None'; $p0 = Shot 'none.png'
        $n0 = Edge $p0 @near; $f0 = Edge $p0 @far
        Vol 'Gauss'; $p1 = Shot 'gaussian.png'
        $n1 = Edge $p1 @near; $f1 = Edge $p1 @far
        Add-Result depthoffield 'Gaussian (Start 8, End 16): 4 m cube stays sharp, 20 m cube blurs' ($n1 -gt $n0 * 0.85 -and $f1 -lt $f0 * 0.6) ("near {0:N2} -> {1:N2}, far {2:N2} -> {3:N2}" -f $n0, $n1, $f0, $f1)
        Vol 'BokehFar'; $p2 = Shot 'bokeh_far.png'
        $n2 = Edge $p2 @near; $f2 = Edge $p2 @far
        Add-Result depthoffield 'Bokeh (focus 20 m, 85 mm f/1.4): the 20 m cube is sharp, the 4 m cube blurs' ($n2 -lt $n0 * 0.6 -and $f2 -gt $f0 * 0.7) ("near {0:N2} -> {1:N2}, far {2:N2} -> {3:N2}" -f $n0, $n2, $f0, $f2)
        Vol 'DofOff'; $p3 = Shot 'mode_off.png'
        $n3 = Edge $p3 @near; $f3 = Edge $p3 @far
        Add-Result depthoffield 'Mode Off = no blur (same as no override)' ([math]::Abs($n3 - $n0) -lt $n0 * 0.05 -and [math]::Abs($f3 - $f0) -lt $f0 * 0.05) ("near {0:N2}/{1:N2}, far {2:N2}/{3:N2}" -f $n3, $n0, $f3, $f0)

        # Motion Blur: Game 뷰, Play 중 카메라가 돈다 (C# 컴파일이 끝난 뒤)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 60) { $t = Invoke-Nova 'log -n 300'; if ($t -match 'Assembly-CSharp loaded') { break }; Start-Sleep -Milliseconds 500 }
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.3,-1 --rotation 0,0,0' | Out-Null
        Invoke-Nova 'add-component "Main Camera" DofTestSpin' | Out-Null
        Vol 'Motion'
        Invoke-Nova 'play' | Out-Null
        $p4 = Shot 'motion_on.png' 'game'
        Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
        Vol 'None'
        Invoke-Nova 'play' | Out-Null
        $p5 = Shot 'motion_off.png' 'game'
        Invoke-Nova 'stop' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
        $m1 = Edge $p4 0.0 0.35 0.42 0.62; $m0 = Edge $p5 0.0 0.35 0.42 0.62   # 왼쪽 상자들의 세로 윤곽
        Add-Result depthoffield 'Motion Blur (camera turning 2 deg per frame): the cube edges smear sideways in the Game view' ($m1 -lt $m0 * 0.6) ("edge step off {0:N1} -> on {1:N1}" -f $m0, $m1)
        Invoke-Nova 'window scene' | Out-Null
        Vol 'Motion'
        $p6 = Shot 'motion_sceneview.png'
        $s6 = Edge $p6 @near
        Add-Result depthoffield 'Motion Blur is not applied in the Scene view (Unity)' ([math]::Abs($s6 - $n0) -lt $n0 * 0.05) ("near {0:N2} (none {1:N2})" -f $s6, $n0)
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-LODGroup
{
    # LOD Group: 같은 자리의 빨강 (LOD 0, 지름 1) · 초록 (LOD 1, 0.9) · 파랑 (LOD 2, 0.8) 구 — 카메라 거리로 하나만, 크로스페이드는 화면 디더
    Write-Host '[lodgroup]'
    $dir = Join-Path $Out 'lodgroup'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\LodTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $base = Get-Content (Join-Path $Project 'Assets\Materials\Red Plastic.mat') -Raw | ConvertFrom-Json
    foreach ($m in @(@('Red', @(0.9, 0.08, 0.08, 1)), @('Green', @(0.08, 0.8, 0.08, 1)), @('Blue', @(0.08, 0.15, 0.9, 1))))
    {
        $mat = $base.PSObject.Copy(); $mat.BaseColor = $m[1]; $mat.Smoothness = 0.1; $mat.ResourcePath = "Assets\LodTest\$($m[0]).mat"
        $mat | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir "$($m[0]).mat")
    }
    $ed = Start-TestEditor
    try
    {
        function Mat([string]$obj, [string]$mat) { Invoke-Nova ("set $obj --component MeshRenderer --values `"{\`"m_MaterialPaths\`":[\`"Assets/LodTest/$mat.mat\`"]}`"") | Out-Null }
        function Shot([string]$name, [string]$view = 'scene') { $p = Join-Path $dir $name; Invoke-Nova "screenshot $p --view $view" | Out-Null; $p }
        function Cam([double]$d) { Invoke-Nova ("camera --position 0,1,-{0} --target 0,1,0" -f $d) | Out-Null; Invoke-Nova 'wait 3' | Out-Null }
        function LodInfo { (Invoke-NovaJson 'lod info').groups[0] }   # (Group 은 Group-Object 별칭)
        # 화면 가운데 영역의 빨강 · 초록 · 파랑 · 그 밖 (하늘 · 바닥) 비율
        function Colors([string]$p, [double]$x0 = 0.485, [double]$x1 = 0.515, [double]$y0 = 0.47, [double]$y1 = 0.53)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($p); $r = 0; $g = 0; $b = 0; $o = 0
            for ($y = [int]($bm.Height * $y0); $y -lt [int]($bm.Height * $y1); $y++) {
                for ($x = [int]($bm.Width * $x0); $x -lt [int]($bm.Width * $x1); $x++) {
                    $c = $bm.GetPixel($x, $y)
                    if ($c.R -gt $c.G + 40 -and $c.R -gt $c.B + 40) { $r++ } elseif ($c.G -gt $c.R + 40 -and $c.G -gt $c.B + 30) { $g++ } elseif ($c.B -gt $c.R + 40 -and $c.B -gt $c.G + 30) { $b++ } else { $o++ } } }
            $bm.Dispose(); $n = [math]::Max(1, $r + $g + $b + $o)
            [pscustomobject]@{ R = $r / $n; G = $g / $n; B = $b / $n; O = $o / $n }
        }
        function Fmt($c) { "R {0:P0} G {1:P0} B {2:P0} other {3:P0}" -f $c.R, $c.G, $c.B, $c.O }

        # 32 (디더) 가 백그라운드로 다 만들어질 때까지
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 120) { $t = Invoke-Nova 'log -n 400'; if ($t -match 'compiled 32\. InstancedBasic|cache hit 32\. InstancedBasic') { break }; Start-Sleep -Milliseconds 500 }
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create sphere --name L0 --position 0,1,0' | Out-Null; Mat 'L0' 'Red'
        Invoke-Nova 'create sphere --name L1 --position 0,1,0 --scale 0.9,0.9,0.9' | Out-Null; Mat 'L1' 'Green'
        Invoke-Nova 'create sphere --name L2 --position 0,1,0 --scale 0.8,0.8,0.8' | Out-Null; Mat 'L2' 'Blue'
        Invoke-Nova 'create empty --name Group --position 0,1,0' | Out-Null
        Invoke-Nova 'add-component Group LODGroup' | Out-Null
        foreach ($i in 0..2) { Invoke-Nova "lod assign --name Group --lod $i --object L$i" | Out-Null }
        $g = LodInfo
        Add-Result lodgroup 'Add LOD Group: Unity defaults (60 / 30 / 10 %), renderers assigned, bounds = sphere (Object Size 1)' ($g.lods.Count -eq 3 -and [math]::Abs($g.lods[1].height - 0.3) -lt 1e-4 -and $g.lods[2].renderers[0] -eq 'L2' -and [math]::Abs($g.size - 1) -lt 0.02) ("lods {0}, size {1:N3}" -f $g.lods.Count, $g.size)

        # 거리 → LOD (Scene 뷰 FOV 60: 화면 높이 = 0.866 / 거리)
        $ok = $true; $detail = @()
        foreach ($case in @(@(1.2, 0, 'R'), @(2.0, 1, 'G'), @(5.0, 2, 'B'), @(12.0, -1, 'O')))
        {
            Cam $case[0]; $p = Shot ("dist_{0}.png" -f $case[0]); $c = Colors $p; $g = LodInfo
            $pass = $g.sceneLOD -eq $case[1] -and $c.($case[2]) -gt 0.9
            $ok = $ok -and $pass
            $detail += ("{0} m: LOD {1} ({2:P1}) {3}" -f $case[0], $g.sceneLOD, $g.sceneHeight, (Fmt $c))
        }
        Add-Result lodgroup 'Distance picks LOD 0 / 1 / 2 / Culled — only that LOD draws' $ok ($detail -join '; ')

        # Cross Fade: LOD 0 의 Fade Transition Width 1 → 화면 높이 60 ~ 100 % 에서 빨강 비율 = (높이 - 0.6) / 0.4
        Invoke-Nova 'lod set --name Group --fadeMode 1 --lod 0 --fadeWidth 1' | Out-Null
        Cam 1.0825
        $p = Shot 'crossfade.png'; $c = Colors $p; $g = LodInfo
        $wide = Colors $p 0.42 0.58 0.3 0.7   # 초록 구 안쪽 전체 — 빈 픽셀 (하늘) 이 하나도 없어야
        $expect = ($g.sceneHeight - 0.6) / 0.4
        Add-Result lodgroup 'Cross Fade (Fade Transition Width 1): both LODs dithered, red share = fade, no holes (depth prepass = same pattern)' ([math]::Abs($c.R - $expect) -lt 0.08 -and $c.G -gt 0.2 -and $wide.O -lt 0.0002) ("height {0:P1}, expected red {1:P0}: {2}; holes {3:P3}" -f $g.sceneHeight, $expect, (Fmt $c), $wide.O)

        # Animate Cross-fading: 경계를 넘는 순간부터 0.5 초 동안 섞고 끝나면 새 LOD 만
        Invoke-Nova 'lod set --name Group --animate true --lod 0 --fadeWidth 0' | Out-Null
        Cam 1.2; Start-Sleep -Milliseconds 800
        Invoke-Nova ("camera --position 0,1,-2 --target 0,1,0") | Out-Null
        $p1 = Shot 'animate_now.png'; $c1 = Colors $p1
        Start-Sleep -Milliseconds 900
        $p2 = Shot 'animate_after.png'; $c2 = Colors $p2
        Add-Result lodgroup 'Animate Cross-fading: right after the switch both show, after 0.5 s only LOD 1' ($c1.R -gt 0.3 -and $c1.G -gt 0.01 -and $c2.G -gt 0.97) ("now {0} -> later {1}" -f (Fmt $c1), (Fmt $c2))

        # Game 뷰: Main Camera 로 따로 고른다
        Invoke-Nova 'lod set --name Group --fadeMode 0 --animate false' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1,-5 --rotation 0,0,0' | Out-Null
        $p = Shot 'game.png' 'game'; $c = Colors $p; $g1 = LodInfo
        Invoke-Nova 'window scene' | Out-Null
        Cam 1.2
        $g = LodInfo
        Add-Result lodgroup 'Game view picks with the Main Camera (5 m → LOD 2), the Scene view with its own camera (LOD 0)' ($g1.gameLOD -eq 2 -and $c.B -gt 0.9 -and $g.sceneLOD -eq 0 -and $g.gameLOD -eq 2) ("game LOD {0} ({1:P1}) {2}; scene LOD {3}" -f $g1.gameLOD, $g1.gameHeight, (Fmt $c), $g.sceneLOD)

        # 꺼진 LOD Group = 모든 LOD 를 그린다 (Unity) — Culled 거리에서도 보인다
        Invoke-Nova 'set Group --component LODGroup --values "{\"enabled\":false}"' | Out-Null
        Cam 12.0
        $p = Shot 'disabled.png'; $c = Colors $p 0.49 0.51 0.48 0.52
        Invoke-Nova 'set Group --component LODGroup --values "{\"enabled\":true}"' | Out-Null
        Add-Result lodgroup 'Disabled LOD Group draws every LOD (seen even past Culled)' ($c.R -gt 0.9) (Fmt $c)

        # 저장 · 다시 열기: 렌더러 참조 (fileID) 가 그대로
        Invoke-Nova 'scene save --as Assets/LodTest/LodScene.scene' | Out-Null
        Invoke-Nova 'scene open Assets/LodTest/LodScene.scene --force' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        Cam 5.0
        $p = Shot 'reopen.png'; $c = Colors $p; $g = LodInfo
        Add-Result lodgroup 'Save → reopen: LODs and renderer references kept' ($g.lods[0].renderers[0] -eq 'L0' -and $g.lods[2].renderers[0] -eq 'L2' -and $g.sceneLOD -eq 2 -and $c.B -gt 0.9) ("renderers {0} / {1} / {2}, LOD {3}" -f $g.lods[0].renderers[0], $g.lods[1].renderers[0], $g.lods[2].renderers[0], $g.sceneLOD)
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Occlusion
{
    # 오클루전 컬링 (GPU Hi-Z, 굽기 없음): 큰 벽 뒤 상자 100 개 — 가려진 것은 그리지 않고, 화면은 끈 것과 같아야
    Write-Host '[occlusion]'
    $dir = Join-Path $Out 'occlusion'
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $ed = Start-TestEditor
    try
    {
        function Shot([string]$name, [string]$view = 'scene', [int]$frames = 8) { $p = Join-Path $dir $name; Invoke-Nova "wait $frames" | Out-Null; Invoke-Nova "screenshot $p --view $view" | Out-Null; $p }
        function Occ { Invoke-Nova 'wait 10' | Out-Null; Invoke-NovaJson 'occlusion info' }   # GPU 결과는 몇 프레임 늦게 읽힌다
        function OccOn([bool]$on) { Invoke-Nova ("occlusion set --enabled {0}" -f $(if ($on) { 'true' } else { 'false' })) | Out-Null }
        # 평균 차 (0..765) 와 많이 다른 픽셀 비율 (2 px 마다)
        function ImgDiff([string]$a, [string]$b)
        {
            $ba = [System.Drawing.Bitmap]::FromFile($a); $bb = [System.Drawing.Bitmap]::FromFile($b); $s = 0.0; $n = 0; $big = 0
            for ($y = 0; $y -lt $ba.Height; $y += 2) { for ($x = 0; $x -lt $ba.Width; $x += 2) {
                $p = $ba.GetPixel($x, $y); $q = $bb.GetPixel($x, $y); $d = [math]::Abs($p.R - $q.R) + [math]::Abs($p.G - $q.G) + [math]::Abs($p.B - $q.B)
                $s += $d; $n++; if ($d -gt 24) { $big++ } } }
            $ba.Dispose(); $bb.Dispose()
            [pscustomobject]@{ Mean = $s / [math]::Max(1, $n); Big = $big / [math]::Max(1, $n) }
        }
        function Same($d) { $d.Mean -lt 0.5 -and $d.Big -lt 0.0005 }
        function Fmt($d) { "mean diff {0:N3}, differing {1:P3}" -f $d.Mean, $d.Big }

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create plane --name Floor --position 0,0,0 --scale 6,1,6' | Out-Null
        Invoke-Nova 'create cube --name Wall --position 0,2.5,0 --scale 24,5,0.5' | Out-Null
        $i = 0
        foreach ($z in 0..9) { foreach ($x in 0..9) { Invoke-Nova ("create cube --name H{0} --position {1},0.5,{2}" -f $i, ($x * 2 - 9), (2 + $z * 1.5)) | Out-Null; $i++ } }
        foreach ($f in @(@('F1', '-3,0.5,-4'), @('F2', '0,0.5,-5'), @('F3', '3,0.5,-4'))) { Invoke-Nova ("create cube --name {0} --position {1}" -f $f[0], $f[1]) | Out-Null }
        Invoke-Nova 'camera --position 0,1.5,-14 --target 0,1.5,0' | Out-Null

        # 1. 벽 뒤 100 개는 가려진다 (절두체 안이라 예전에는 모두 그렸다)
        $o = Occ
        Add-Result occlusion 'Supported on DirectX 11 and on in the Scene view' ($o.supported -and $o.enabled -and $o.scene.active) ("supported {0}, enabled {1}, scene active {2}" -f $o.supported, $o.enabled, $o.scene.active)
        Add-Result occlusion 'Behind the wall: 100 boxes culled, the wall · floor · 3 front boxes drawn' ($o.scene.tested -ge 105 -and $o.scene.culled -ge 98 -and $o.scene.visible -le 8) ("tested {0}, visible {1}, culled {2}" -f $o.scene.tested, $o.scene.visible, $o.scene.culled)
        $pOn = Shot 'wall_on.png'
        OccOn $false; $pOff = Shot 'wall_off.png'; $off = Occ; OccOn $true
        $d = ImgDiff $pOn $pOff
        Add-Result occlusion 'Same picture as with occlusion culling off (no holes, no missing objects)' ((Same $d) -and -not $off.scene.active) ("{0}; off: active {1}" -f (Fmt $d), $off.scene.active)

        # 2. 벽 위에서 내려다보면 모두 보인다 (지난 프레임 기록이 틀려도 같은 프레임에 그린다)
        Invoke-Nova 'camera --position 0,14,-12 --target 0,0,8' | Out-Null
        $pOn = Shot 'above_on.png' 'scene' 1
        $o = Occ
        OccOn $false; $pOff = Shot 'above_off.png'; OccOn $true
        $d = ImgDiff $pOn $pOff
        # (벽 바로 뒤 줄은 이 높이에서도 벽에 가린다 — 시선이 z = 2 에서 y 3.5 를 지난다)
        Add-Result occlusion 'Looking over the wall: the boxes show up in the first frame (only the rows right behind the wall stay culled), same picture' ((Same $d) -and $o.scene.culled -le 30 -and $o.scene.tested -ge 105) ("first frame {0}; tested {1}, culled {2}" -f (Fmt $d), $o.scene.tested, $o.scene.culled)

        # 3. 움직인 물체: 벽 뒤 상자를 앞으로 옮기면 그 프레임에 보인다 (굽기 없음 — 움직이는 물체도)
        Invoke-Nova 'camera --position 0,1.5,-14 --target 0,1.5,0' | Out-Null
        $pBefore = Shot 'move_before.png'
        Invoke-Nova 'set H55 --position -6,0.5,-2' | Out-Null
        $pAfter = Shot 'move_after.png' 'scene' 1
        OccOn $false; $pOff = Shot 'move_off.png'; OccOn $true
        $dMoved = ImgDiff $pBefore $pAfter; $d = ImgDiff $pAfter $pOff
        Add-Result occlusion 'A hidden box moved in front of the wall is drawn in the next frame' ($dMoved.Big -gt 0.002 -and (Same $d)) ("moved region {0:P2}; vs off {1}" -f $dMoved.Big, (Fmt $d))

        # 6. Skinned Mesh Renderer (오클루전 예측 쿼리) · 나무 (GPU 인스턴스 목록) 도 벽 뒤에서 빠진다
        Invoke-Nova 'create character --name HidA' | Out-Null; Invoke-Nova 'set HidA --position 3,0,6' | Out-Null
        Invoke-Nova 'create character --name HidB' | Out-Null; Invoke-Nova 'set HidB --position -4,0,9' | Out-Null
        Invoke-Nova 'create character --name SeenC' | Out-Null; Invoke-Nova 'set SeenC --position -6,0,-4' | Out-Null
        Invoke-Nova 'create tree --name TreeA --position -3,0,24 --scale 0.25,0.25,0.25' | Out-Null
        Invoke-Nova 'create tree --name TreeB --position 5,0,26 --scale 0.25,0.25,0.25' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null
        $pOn = Shot 'skinned_trees_on.png' 'scene' 10
        $o = Occ
        OccOn $false; $pOff = Shot 'skinned_trees_off.png'; OccOn $true
        $d = ImgDiff $pOn $pOff
        Add-Result occlusion 'Skinned Mesh Renderers (occlusion predicate queries) and trees (GPU instance lists) behind the wall are skipped, same picture' ((Same $d) -and $o.scene.queries -ge 3 -and $o.scene.queriesHidden -ge 2 -and $o.scene.instancesTested -ge 2 -and $o.scene.instancesCulled -ge 2) ("queries {0} (hidden {1}), tree instances {2} (culled {3}); {4}" -f $o.scene.queries, $o.scene.queriesHidden, $o.scene.instancesTested, $o.scene.instancesCulled, (Fmt $d))
        foreach ($n in 'HidA', 'HidB', 'SeenC', 'TreeA', 'TreeB') { Invoke-Nova "delete $n" | Out-Null }

        # 4. Game 뷰: Camera 의 Occlusion Culling (Unity 와 같은 값) 로 켜고 끈다
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.5,-14 --rotation 0,0,0' | Out-Null
        $pOn = Shot 'game_on.png' 'game' 10
        $o = Occ
        Invoke-Nova 'set "Main Camera" --component Camera --values "{\"occlusionCulling\":false}"' | Out-Null
        $pOff = Shot 'game_off.png' 'game' 10
        $o2 = Occ
        Invoke-Nova 'set "Main Camera" --component Camera --values "{\"occlusionCulling\":true}"' | Out-Null
        $d = ImgDiff $pOn $pOff
        Invoke-Nova 'window scene' | Out-Null
        Add-Result occlusion 'Game view: Camera Occlusion Culling on culls the hidden boxes, off draws them, same picture' ($o.game.active -and $o.game.culled -ge 98 -and -not $o2.game.active -and (Same $d)) ("on: culled {0}/{1}; off: active {2}; {3}" -f $o.game.culled, $o.game.tested, $o2.game.active, (Fmt $d))

        # 5. 성능: 벽 뒤 구 2000 개 (약 150 만 삼각형) — Scene 뷰 GPU 시간 (깊이 프리패스 · 본 패스에서 빠진다, 그림자는 그대로)
        $cs = Join-Path $dir 'spheres.cs'
        'for (int i = 0; i < 2000; i++) { var g = GameObject.CreatePrimitive(PrimitiveType.Sphere); g.name = "P" + i; g.transform.position = new Vector3(-11f + (i % 40) * 0.56f, 0.5f, 2.5f + (i / 40) * 0.55f); } return 2000;' | Set-Content -Encoding utf8 $cs
        Invoke-Nova "exec --file $cs" | Out-Null
        $o = Occ
        $perfOn = Invoke-NovaJson 'perf --frames 120'
        OccOn $false; Invoke-Nova 'wait 10' | Out-Null
        $perfOff = Invoke-NovaJson 'perf --frames 120'
        OccOn $true
        # 단계별 GPU 일 (PIPELINE_STATISTICS 의 삼각형 — 시간은 CPU 가 늦은 Debug 프레임에서 GPU 가 쉬는 시간이 구간에 붙어 흔들린다)
        function Work($perf, [string]$n) { $p = @($perf.gpuPasses | Where-Object { $_.pass -match "^\.$n$" })[0]; if ($p) { [double]$p.primitives } else { 0 } }
        $phases = (@('Depth Prepass', 'Opaque', 'Shadows') | ForEach-Object { "{0} {1:N0}k/{2:N0}k tris" -f $_, ((Work $perfOn $_) / 1000), ((Work $perfOff $_) / 1000) }) -join ', '
        $less = (Work $perfOn 'Depth Prepass') -lt (Work $perfOff 'Depth Prepass') -and (Work $perfOn 'Opaque') -lt (Work $perfOff 'Opaque') * 0.5
        Add-Result occlusion 'Performance: 2000 hidden spheres — far fewer triangles in the depth prepass and opaque pass' ($o.scene.culled -ge 2000 -and $less) ("culled {0}/{1}; {6}; GPU {2:N2} ms (on) vs {3:N2} ms (off), CPU {4:N2} vs {5:N2} ms" -f $o.scene.culled, $o.scene.tested, $perfOn.gpuMs, $perfOff.gpuMs, $perfOn.cpuMs, $perfOff.cpuMs, $phases)
        # 그림자 캐스터: 빛 방향으로 쓸어 늘린 상자가 카메라 깊이에 가려진 캐스터 (벽 뒤 구의 그림자는 벽 뒤 바닥에만 떨어진다)
        $shOn = Work $perfOn 'Shadows'; $shOff = Work $perfOff 'Shadows'
        Add-Result occlusion 'Shadow casters whose shadow cannot reach a visible surface are skipped (every-frame cascades) — fewer shadow triangles' ($o.scene.shadowTested -gt 0 -and $o.scene.shadowCulled -ge 100 -and $shOn -lt $shOff) ("shadow casters {0}, culled {1}; Shadows {2:N0}k vs {3:N0}k triangles" -f $o.scene.shadowTested, $o.scene.shadowCulled, ($shOn / 1000), ($shOff / 1000))
        $perfOn.gpuPasses | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $dir 'perf_on.json')
        $perfOff.gpuPasses | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $dir 'perf_off.json')
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
    }
}

function Suite-OcclusionGL { Suite-OcclusionApi 'occlusiongl' 'OpenGL' }
function Suite-OcclusionVK { Suite-OcclusionApi 'occlusionvk' 'Vulkan' }

function Suite-OcclusionApi([string]$suite, [string]$api)
{
    # 오클루전 컬링을 OpenGL · Vulkan 으로: 같은 .fx (ShaderCross → GLSL compute · SPIR-V), SSBO · 스토리지 이미지, 간접 그리기,
    # 조건부 그리기 (캐릭터 — GL 조건부 렌더링 · VK_EXT_conditional_rendering)
    Write-Host "[$suite]"
    $dir = Join-Path $Out $suite
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $ed = Start-TestEditor -OpenGL:($api -eq 'OpenGL') -Vulkan:($api -eq 'Vulkan')
    try
    {
        function Shot([string]$name, [int]$frames = 8) { $p = Join-Path $dir $name; Invoke-Nova "wait $frames" | Out-Null; Invoke-Nova "screenshot $p --view scene" | Out-Null; $p }
        function Occ { Invoke-Nova 'wait 10' | Out-Null; Invoke-NovaJson 'occlusion info' }
        function OccOn([bool]$on) { Invoke-Nova ("occlusion set --enabled {0}" -f $(if ($on) { 'true' } else { 'false' })) | Out-Null }
        function ImgDiff([string]$a, [string]$b)
        {
            $ba = [System.Drawing.Bitmap]::FromFile($a); $bb = [System.Drawing.Bitmap]::FromFile($b); $s = 0.0; $n = 0; $big = 0
            for ($y = 0; $y -lt $ba.Height; $y += 2) { for ($x = 0; $x -lt $ba.Width; $x += 2) {
                $p = $ba.GetPixel($x, $y); $q = $bb.GetPixel($x, $y); $d = [math]::Abs($p.R - $q.R) + [math]::Abs($p.G - $q.G) + [math]::Abs($p.B - $q.B)
                $s += $d; $n++; if ($d -gt 24) { $big++ } } }
            $ba.Dispose(); $bb.Dispose()
            [pscustomobject]@{ Mean = $s / [math]::Max(1, $n); Big = $big / [math]::Max(1, $n) }
        }
        function Same($d) { $d.Mean -lt 0.5 -and $d.Big -lt 0.0005 }
        function Fmt($d) { "mean diff {0:N3}, differing {1:P3}" -f $d.Mean, $d.Big }

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create plane --name Floor --position 0,0,0 --scale 6,1,6' | Out-Null
        Invoke-Nova 'create cube --name Wall --position 0,2.5,0 --scale 24,5,0.5' | Out-Null
        $cs = Join-Path $dir 'cubes.cs'
        'for (int i = 0; i < 100; i++) { var g = GameObject.CreatePrimitive(PrimitiveType.Cube); g.name = "H" + i; g.transform.position = new Vector3((i % 10) * 2 - 9, 0.5f, 2 + (i / 10) * 1.5f); } return 100;' | Set-Content -Encoding utf8 $cs
        Invoke-Nova "exec --file $cs" | Out-Null
        foreach ($f in @(@('F1', '-3,0.5,-4'), @('F2', '0,0.5,-5'), @('F3', '3,0.5,-4'))) { Invoke-Nova ("create cube --name {0} --position {1}" -f $f[0], $f[1]) | Out-Null }
        Invoke-Nova 'create character --name HidA' | Out-Null; Invoke-Nova 'set HidA --position 3,0,6' | Out-Null
        Invoke-Nova 'create character --name SeenC' | Out-Null; Invoke-Nova 'set SeenC --position -6,0,-4' | Out-Null
        Invoke-Nova 'create tree --name TreeA --position -3,0,24 --scale 0.25,0.25,0.25' | Out-Null
        Invoke-Nova 'camera --position 0,1.5,-14 --target 0,1.5,0' | Out-Null
        Invoke-Nova 'wait 30' | Out-Null

        $o = Occ
        $pOn = Shot 'wall_on.png'
        OccOn $false; $pOff = Shot 'wall_off.png'; OccOn $true
        $d = ImgDiff $pOn $pOff
        Add-Result $suite "${api}: supported, boxes · character · tree · shadow casters behind the wall culled, same picture" ($o.supported -and $o.scene.active -and $o.scene.culled -ge 98 -and $o.scene.queries -ge 2 -and $o.scene.queriesHidden -ge 1 -and $o.scene.instancesCulled -ge 1 -and $o.scene.shadowCulled -gt 0 -and (Same $d)) ("culled {0}/{1}, queries {2} (hidden {3}), tree instances culled {4}, shadow casters culled {5}/{6}; {7}" -f $o.scene.culled, $o.scene.tested, $o.scene.queries, $o.scene.queriesHidden, $o.scene.instancesCulled, $o.scene.shadowCulled, $o.scene.shadowTested, (Fmt $d))

        Invoke-Nova 'camera --position 0,14,-12 --target 0,0,8' | Out-Null
        $pOn = Shot 'above_on.png' 1
        OccOn $false; $pOff = Shot 'above_off.png'; OccOn $true
        $d = ImgDiff $pOn $pOff
        Add-Result $suite "${api}: looking over the wall, first frame same picture" (Same $d) (Fmt $d)

        Invoke-Nova 'camera --position 0,1.5,-14 --target 0,1.5,0' | Out-Null
        $pBefore = Shot 'move_before.png'
        Invoke-Nova 'set H55 --position -6,0.5,-2' | Out-Null
        $pAfter = Shot 'move_after.png' 1
        OccOn $false; $pOff = Shot 'move_off.png'; OccOn $true
        $dMoved = ImgDiff $pBefore $pAfter; $d = ImgDiff $pAfter $pOff
        Add-Result $suite "${api}: a hidden box moved in front of the wall is drawn in the next frame" ($dMoved.Big -gt 0.002 -and (Same $d)) ("moved region {0:P2}; vs off {1}" -f $dMoved.Big, (Fmt $d))
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
    }
}

function Suite-Material([string]$Api = 'dx')
{
    # Api = dx · gl · vk (스위트 이름 material · materialgl · materialvk)
    # C# Renderer.material (이 렌더러만의 사본) · sharedMaterial · Material.SetColor/GetFloat · MaterialPropertyBlock (같은 값 = 한 인스턴싱 묶음)
    $sn = if ($Api -eq 'dx') { 'material' } else { 'material' + $Api }
    Write-Host "[$sn]"
    $dir = Join-Path $Out $sn
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $matDir = Join-Path $Project 'Assets\MatTest'
    New-Item -ItemType Directory -Force $matDir | Out-Null
    Copy-Item (Join-Path $Project 'Assets\Materials\Red Plastic.mat') (Join-Path $matDir 'Shared.mat') -Force
    $ed = Start-TestEditor -OpenGL:($Api -eq 'gl') -Vulkan:($Api -eq 'vk') -D3D12:($Api -eq '12')
    try
    {
        function Exec([string]$code) { $f = Join-Path $dir 'exec.cs'; $code | Set-Content -Encoding utf8 $f; (Invoke-NovaJson "exec --file $f").result }
        # Game 뷰 그림의 색 칸 수 (빨강 · 파랑 · 초록 · 노랑 · 청록)
        function Colors([string]$name)
        {
            $p = Join-Path $dir $name
            Invoke-Nova 'wait 6' | Out-Null
            Invoke-Nova "screenshot $p --view game" | Out-Null
            $r = [pscustomobject]@{ Red = 0; Blue = 0; Green = 0; Yellow = 0; Cyan = 0 }
            if (-not (Test-Path $p)) { return $r }
            $bm = [System.Drawing.Bitmap]::FromFile($p)
            for ($y = 0; $y -lt $bm.Height; $y += 3) { for ($x = 0; $x -lt $bm.Width; $x += 3) {
                $c = $bm.GetPixel($x, $y)
                if ($c.R -gt $c.G + 50 -and $c.R -gt $c.B + 50) { $r.Red++ }
                elseif ($c.B -gt $c.R + 50 -and $c.B -gt $c.G + 30) { $r.Blue++ }
                elseif ($c.G -gt $c.R + 50 -and $c.G -gt $c.B + 50) { $r.Green++ }
                elseif ($c.R -gt 120 -and $c.G -gt 120 -and $c.B -lt [math]::Min($c.R, $c.G) - 60) { $r.Yellow++ }
                elseif ($c.G -gt 110 -and $c.B -gt 110 -and $c.R -lt [math]::Min($c.G, $c.B) - 60) { $r.Cyan++ } } }
            $bm.Dispose()
            $r
        }
        function Batches { (Invoke-NovaJson 'perf --frames 20').stats.'Game View/Mesh Batches' }

        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 30,1,30' | Out-Null
        $cp = (Invoke-NovaJson 'get "Main Camera"').worldPosition
        $z = $cp[2] + 7
        foreach ($o in @(@('A', -1.6), @('B', 1.6)))
        {
            Invoke-Nova ("create cube --name {0} --position {1},{2},{3} --scale 2,2,2" -f $o[0], ($cp[0] + $o[1]), ($cp[1] - 0.2), $z) | Out-Null
            Invoke-Nova ('set {0} --component MeshRenderer --values "{{\"m_MaterialPaths\":[\"Assets/MatTest/Shared.mat\"]}}"' -f $o[0]) | Out-Null
        }
        $c0 = Colors 'shared_red.png'

        # 1. material: A 만 파랑 (B 는 공유 재질 그대로 빨강), 이름 "Shared (Instance)"
        $r1 = Exec 'var a = GameObject.Find("A").GetComponent<MeshRenderer>(); var b = GameObject.Find("B").GetComponent<MeshRenderer>(); var m = a.material; m.color = Color.blue; return a.material.name + "|" + b.sharedMaterial.name + "|" + (a.sharedMaterial == b.sharedMaterial) + "|" + (a.material == m) + "|" + b.sharedMaterial.GetFloat("_Smoothness").ToString("0.00") + "|" + b.sharedMaterial.HasProperty("_BaseColor") + "|" + b.sharedMaterial.HasProperty("_Nope");'
        $c1 = Colors 'instance_blue.png'
        Add-Result $sn 'Renderer.material: a copy for A only (A blue, B keeps the shared red), name "Shared (Instance)"' ($c0.Red -gt 200 -and $c1.Blue -gt $c0.Blue + 1000 -and $c1.Red -gt 100 -and $c1.Red -lt $c0.Red * 0.7 -and "$r1" -like 'Shared (Instance)|Shared|False|True|*|True|False') ("$r1; red {0} -> {1}, blue {2}" -f $c0.Red, $c1.Red, $c1.Blue)

        # 2. sharedMaterial: B 의 공유 재질을 초록으로 — 공유하는 렌더러 모두 (A 는 사본이라 파랑 그대로)
        $r2 = Exec 'var b = GameObject.Find("B").GetComponent<MeshRenderer>(); b.sharedMaterial.SetColor("_BaseColor", new Color(0.1f, 0.8f, 0.1f, 1f)); return b.sharedMaterial.color.g.ToString("0.0");'
        $c2 = Colors 'shared_green.png'
        Add-Result $sn 'Renderer.sharedMaterial: changing the shared material recolors its users (B green), the copy stays (A blue)' ($c2.Green -gt 100 -and $c2.Blue -gt $c0.Blue + 1000 -and $c2.Red -lt 30 -and "$r2" -eq '0.8') ("green {0}, blue {1}, red {2}; color.g={3}" -f $c2.Green, $c2.Blue, $c2.Red, $r2)

        # 3. MaterialPropertyBlock: 공유 재질 상자 24 개 (Material.Load + sharedMaterial) — 노랑 12 · 청록 12. _BaseColor 뿐이라 인스턴스 값 → 묶음이 늘지 않는다
        $pos = '{0}f + (i % 12) * 0.9f, {1}f + (i / 12) * 0.9f, {2}f' -f ($cp[0] - 5), ($cp[1] + 1.4), ($z + 3)
        $r3 = Exec ('var mat = Material.Load("Assets/MatTest/Shared.mat"); for (int i = 0; i < 24; i++) { var g = GameObject.CreatePrimitive(PrimitiveType.Cube); g.name = "P" + i; g.transform.position = new Vector3(' + $pos + '); g.transform.localScale = new Vector3(0.6f, 0.6f, 0.6f); g.GetComponent<MeshRenderer>().sharedMaterial = mat; } return mat.name;')
        $b0 = Batches
        $r4 = Exec 'var y = new MaterialPropertyBlock(); y.SetColor("_BaseColor", new Color(0.9f, 0.85f, 0.1f, 1f)); var c = new MaterialPropertyBlock(); c.SetColor(Shader.PropertyToID("_BaseColor"), new Color(0.1f, 0.8f, 0.85f, 1f)); for (int i = 0; i < 24; i++) GameObject.Find("P" + i).GetComponent<MeshRenderer>().SetPropertyBlock(i % 2 == 0 ? y : c); var got = new MaterialPropertyBlock(); var r = GameObject.Find("P1").GetComponent<MeshRenderer>(); r.GetPropertyBlock(got); return got.GetColor("_BaseColor").b.ToString("0.00") + "|" + r.HasPropertyBlock() + "|" + r.sharedMaterial.name;'
        $b1 = Batches
        $c3 = Colors 'block_colors.png'
        Add-Result $sn 'MaterialPropertyBlock: per-renderer colors on a shared material (yellow · cyan) — GPU instancing property, no extra batch' ($c3.Yellow -gt 30 -and $c3.Cyan -gt 30 -and $b1 -eq $b0 -and "$r3" -eq 'Shared' -and "$r4" -eq '0.85|True|Shared') ("yellow {0}, cyan {1}; mesh batches {2} -> {3}; {4}; {5}" -f $c3.Yellow, $c3.Cyan, $b0, $b1, $r3, $r4)

        # 3b. 인스턴스 값이 아닌 속성 (_BumpScale) 이 든 블록은 파생 재질 → 묶음이 나뉜다 (같은 값끼리는 하나)
        Exec 'var y = new MaterialPropertyBlock(); y.SetColor("_BaseColor", new Color(0.9f, 0.85f, 0.1f, 1f)); y.SetFloat("_BumpScale", 0.5f); for (int i = 0; i < 24; i += 2) GameObject.Find("P" + i).GetComponent<MeshRenderer>().SetPropertyBlock(y); return 0;' | Out-Null
        $b2 = Batches
        $c3b = Colors 'block_derived.png'
        Add-Result $sn 'A block with a non-instanced property (_BumpScale) uses a derived material: one more batch, same colors' (($b2 - $b0) -eq 1 -and $c3b.Yellow -gt $c3.Yellow * 0.7 -and $c3b.Cyan -gt 30) ("mesh batches {0} -> {1}; yellow {2}, cyan {3}" -f $b0, $b2, $c3b.Yellow, $c3b.Cyan)

        # 3d. _EmissionColor · _Metallic · _Smoothness 도 인스턴스 값: 묶음 그대로, 같은 블록 + 인스턴스가 안 되는 속성 (_Cutoff — 잘라내기 없는 재질이라 그림에 영향 없음)
        #     을 넣어 파생 재질로 그린 그림과 같아야 한다 (셰이더의 인스턴스 값 읽기 = 재질 값과 같은 결과)
        function Blocks([string]$extra) { Exec ('for (int i = 0; i < 24; i++) { var b = new MaterialPropertyBlock(); b.SetColor("_BaseColor", i % 3 == 0 ? new Color(0.9f, 0.85f, 0.1f, 1f) : new Color(0.2f, 0.3f, 0.9f, 1f)); b.SetColor("_EmissionColor", i % 2 == 0 ? new Color(0f, 0f, 0f, 1f) : new Color(0.8f, 0.1f, 0.6f, 1f) * 1.5f); b.SetFloat("_Metallic", (i % 4) / 3f); b.SetFloat("_Smoothness", i % 5 == 0 ? 0.95f : 0.2f); ' + $extra + ' GameObject.Find("P" + i).GetComponent<MeshRenderer>().SetPropertyBlock(b); } return 0;') | Out-Null }
        function ShotDiff([string]$a, [string]$b)
        {
            $ba = [System.Drawing.Bitmap]::FromFile($a); $bb = [System.Drawing.Bitmap]::FromFile($b); $sum = 0.0; $n = 0; $big = 0
            for ($y = 0; $y -lt $ba.Height; $y += 2) { for ($x = 0; $x -lt $ba.Width; $x += 2) {
                $p = $ba.GetPixel($x, $y); $q = $bb.GetPixel($x, $y); $d = [math]::Abs($p.R - $q.R) + [math]::Abs($p.G - $q.G) + [math]::Abs($p.B - $q.B)
                $sum += $d; $n++; if ($d -gt 24) { $big++ } } }
            $ba.Dispose(); $bb.Dispose()
            [pscustomobject]@{ Mean = $sum / [math]::Max(1, $n); Big = $big / [math]::Max(1, $n) }
        }
        Blocks ''
        $b5 = Batches
        $c5i = Colors 'props_instanced.png'
        Blocks 'b.SetFloat("_Cutoff", 0.5f);'
        $b6 = Batches
        $c5d = Colors 'props_derived.png'
        $dd = ShotDiff (Join-Path $dir 'props_instanced.png') (Join-Path $dir 'props_derived.png')
        Add-Result $sn '_EmissionColor · _Metallic · _Smoothness are instance values too: no extra batch, same picture as derived materials' ($b5 -eq $b0 -and $b6 -gt $b0 + 4 -and $dd.Mean -lt 0.6 -and $dd.Big -lt 0.002 -and $c5i.Yellow -gt 20) ("mesh batches {0} (instanced) · {1} (derived) · base {2}; picture diff mean {3:N3}, >24: {4:P3}; yellow {5}" -f $b5, $b6, $b0, $dd.Mean, $dd.Big, $c5i.Yellow)
        Exec 'for (int i = 0; i < 24; i++) GameObject.Find("P" + i).GetComponent<MeshRenderer>().SetPropertyBlock(null); return 0;' | Out-Null

        # 3c. 렌더러마다 다른 색 100 개: 묶음 하나 그대로, 렌더러가 64 개를 넘어 GPU 오클루전 경로 (Compact 가 기본색도 옮긴다) — 끈 화면과 같은 그림
        #  (A 뒤에 숨은 작은 상자 8 개: 가린 것이 있어야 오클루전이 쉬지 않는다)
        $pos = '{0}f + (i % 20) * 0.5f, {1}f + (i / 20) * 0.5f, {2}f' -f ($cp[0] - 4.75), ($cp[1] + 2.6), ($z + 4)
        $hid = '{0}f + (i % 4) * 0.3f, {1}f + (i / 4) * 0.3f, {2}f' -f ($cp[0] - 2.05), ($cp[1] - 0.35), ($z + 1.6)
        Exec ('var mat = Material.Load("Assets/MatTest/Shared.mat"); for (int i = 0; i < 100; i++) { var g = GameObject.CreatePrimitive(PrimitiveType.Cube); g.name = "R" + i; g.transform.position = new Vector3(' + $pos + '); g.transform.localScale = new Vector3(0.4f, 0.4f, 0.4f); g.GetComponent<MeshRenderer>().sharedMaterial = mat; } for (int i = 0; i < 8; i++) { var h = GameObject.CreatePrimitive(PrimitiveType.Cube); h.name = "Hid" + i; h.transform.position = new Vector3(' + $hid + '); h.transform.localScale = new Vector3(0.2f, 0.2f, 0.2f); } return 0;') | Out-Null
        $b3 = Batches
        Exec 'var block = new MaterialPropertyBlock(); for (int i = 0; i < 100; i++) { float h = (i % 20) / 20f * 6f; float r = Mathf.Clamp01(Mathf.Abs(h - 3f) - 1f), g = Mathf.Clamp01(2f - Mathf.Abs(h - 2f)), b = Mathf.Clamp01(2f - Mathf.Abs(h - 4f)); block.SetColor("_BaseColor", new Color(r, g, b, 1f)); GameObject.Find("R" + i).GetComponent<MeshRenderer>().SetPropertyBlock(block); } return 0;' | Out-Null
        $b4 = Batches
        Invoke-Nova 'wait 40' | Out-Null   # 쉬던 오클루전이 다시 검사할 때까지 (30 번)
        $occ = Invoke-NovaJson 'occlusion info'
        $cR = Colors 'rainbow_occlusion.png'
        Invoke-Nova 'occlusion set --enabled false' | Out-Null
        $cRoff = Colors 'rainbow_cpu.png'
        Invoke-Nova 'occlusion set --enabled true' | Out-Null
        $hues = @($cR.Red, $cR.Yellow, $cR.Green, $cR.Cyan, $cR.Blue) | Where-Object { $_ -gt 15 }
        $sameRain = [math]::Abs($cR.Red - $cRoff.Red) + [math]::Abs($cR.Yellow - $cRoff.Yellow) + [math]::Abs($cR.Green - $cRoff.Green) + [math]::Abs($cR.Cyan - $cRoff.Cyan) + [math]::Abs($cR.Blue - $cRoff.Blue)
        Add-Result $sn '100 renderers, 100 different colors: still one batch, GPU occlusion path keeps the colors (same as CPU path)' ($b4 -eq $b3 -and $hues.Count -ge 5 -and $occ.game.active -and $sameRain -le 20) ("mesh batches {0} -> {1}; hues R {2} Y {3} G {4} C {5} B {6}; GPU path {7} (tested {8}, culled {10}); diff vs CPU {9}" -f $b3, $b4, $cR.Red, $cR.Yellow, $cR.Green, $cR.Cyan, $cR.Blue, $occ.game.active, $occ.game.tested, $sameRain, $occ.game.culled)
        Exec 'for (int i = 0; i < 100; i++) GameObject.Destroy(GameObject.Find("R" + i)); for (int i = 0; i < 8; i++) GameObject.Destroy(GameObject.Find("Hid" + i)); return 0;' | Out-Null

        # 3e. Shader Graph 도: 그래프 속성 _BaseColor 는 GPU 인스턴싱 속성 (묶음 그대로), material.SetColor · GetColor 는 그래프 속성 (엔진 값이 아니라)
        Invoke-NovaJson 'shadergraph new Assets/MatTest/SGTint.shadergraph --timeout 240' | Out-Null
        Invoke-NovaJson 'shadergraph property.add --name BaseColor --type Color --value 0.2,0.8,0.2,1 --node --x -300 --y 0' | Out-Null
        Invoke-NovaJson 'shadergraph connect --from 1 --to Master --in "Base Color"' | Out-Null
        $sgSave = Invoke-NovaJson 'shadergraph save --timeout 240'
        Invoke-NovaJson 'shadergraph material' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        $r9 = Exec 'var mat = Material.Load("Assets/MatTest/SGTint.mat"); for (int i = 0; i < 24; i++) GameObject.Find("P" + i).GetComponent<MeshRenderer>().sharedMaterial = mat; var copy = new Material(mat); copy.SetColor("_BaseColor", new Color(0.1f, 0.2f, 0.9f, 1f)); return mat.shaderName + "|" + mat.GetColor("_BaseColor").g.ToString("0.0") + "|" + copy.GetColor("_BaseColor").b.ToString("0.0") + "|" + mat.HasProperty("_BaseColor");'
        $b9 = Batches
        Exec 'var y = new MaterialPropertyBlock(); y.SetColor("_BaseColor", new Color(0.9f, 0.85f, 0.1f, 1f)); var c = new MaterialPropertyBlock(); c.SetColor("_BaseColor", new Color(0.1f, 0.8f, 0.85f, 1f)); for (int i = 0; i < 24; i++) GameObject.Find("P" + i).GetComponent<MeshRenderer>().SetPropertyBlock(i % 2 == 0 ? y : c); return 0;' | Out-Null
        $b10 = Batches
        $c9 = Colors 'sg_block.png'
        Add-Result $sn 'Shader Graph: _BaseColor block is a GPU instancing property (no extra batch), SetColor / GetColor use the graph property' ($sgSave.built -and "$r9" -eq 'Shader Graphs/SGTint|0.8|0.9|True' -and $b10 -eq $b9 -and $c9.Yellow -gt 30 -and $c9.Cyan -gt 30) ("built {0}; {1}; mesh batches {2} -> {3}; yellow {4}, cyan {5}" -f $sgSave.built, $r9, $b9, $b10, $c9.Yellow, $c9.Cyan)
        Exec 'var mat = Material.Load("Assets/MatTest/Shared.mat"); for (int i = 0; i < 24; i++) { var r = GameObject.Find("P" + i).GetComponent<MeshRenderer>(); r.SetPropertyBlock(null); r.sharedMaterial = mat; } return 0;' | Out-Null

        # 4. SetPropertyBlock(null) → 공유 재질 색 (초록) 으로
        Exec 'for (int i = 0; i < 24; i++) GameObject.Find("P" + i).GetComponent<MeshRenderer>().SetPropertyBlock(null); return 0;' | Out-Null
        $c4 = Colors 'block_cleared.png'
        Add-Result $sn 'SetPropertyBlock(null): back to the shared material' ($c4.Yellow -lt 10 -and $c4.Cyan -lt 10 -and $c4.Green -gt $c2.Green) ("yellow {0}, cyan {1}, green {2}" -f $c4.Yellow, $c4.Cyan, $c4.Green)

        # 6. Renderer 공통 (GetComponent<Renderer>): bounds · shadowCastingMode (Unity 값) · enabled = false 면 그리지 않는다
        $want = '{0:0.00},{1:0.00},{2:0.00}' -f ($cp[0] - 1.6), ($cp[1] - 0.2), $z
        $r6 = Exec 'var a = GameObject.Find("A"); Renderer r = a.GetComponent<Renderer>(); var bb = r.bounds; var s = (r is MeshRenderer) + "|" + bb.center.x.ToString("0.00") + "," + bb.center.y.ToString("0.00") + "," + bb.center.z.ToString("0.00") + "|" + bb.size.x.ToString("0.00") + "|" + r.shadowCastingMode; r.shadowCastingMode = NovaEngine.Rendering.ShadowCastingMode.ShadowsOnly; s += "|" + a.GetComponent<MeshRenderer>().shadowCastingMode; r.shadowCastingMode = NovaEngine.Rendering.ShadowCastingMode.On; r.enabled = false; return s + "|" + a.GetComponent<MeshRenderer>().enabled;'
        $c6 = Colors 'renderer_disabled.png'
        Exec 'GameObject.Find("A").GetComponent<Renderer>().enabled = true; return 0;' | Out-Null
        $c6b = Colors 'renderer_enabled.png'
        Add-Result $sn 'Renderer base class: GetComponent<Renderer>, bounds, shadowCastingMode, enabled = false hides A' ("$r6" -eq "True|$want|2.00|On|ShadowsOnly|False" -and $c6.Blue -le $c0.Blue + 100 -and $c6b.Blue -gt $c0.Blue + 1000) ("$r6 (want center $want); blue disabled {0} / enabled {1} (sky {2})" -f $c6.Blue, $c6b.Blue, $c0.Blue)

        # 7. Skinned Mesh Renderer 도 같은 API: MaterialPropertyBlock 노랑 · enabled
        Invoke-Nova 'create character --name Hero' | Out-Null
        Invoke-Nova ("set Hero --position {0},0,{1}" -f ($cp[0] + 4.2), ($z - 1)) | Out-Null
        $h0 = Colors 'hero.png'
        $r7 = Exec 'var hero = GameObject.Find("Hero"); var y = new MaterialPropertyBlock(); y.SetColor("_BaseColor", new Color(1f, 0.85f, 0f, 1f)); int n = 0; foreach (var k in hero.GetComponentsInChildren<SkinnedMeshRenderer>()) { k.SetPropertyBlock(y); n++; } Renderer r = hero.GetComponentInChildren<Renderer>(); return n + "|" + (r is SkinnedMeshRenderer) + "|" + r.sharedMaterials.Length + "|" + r.HasPropertyBlock() + "|" + r.shadowCastingMode + "|" + r.bounds.size.y.ToString("0.0");'
        $h1 = Colors 'hero_yellow.png'
        Exec 'foreach (var k in GameObject.Find("Hero").GetComponentsInChildren<SkinnedMeshRenderer>()) k.enabled = false; return 0;' | Out-Null
        $h2 = Colors 'hero_disabled.png'
        $p7 = "$r7".Split('|')
        Add-Result $sn 'SkinnedMeshRenderer: SetPropertyBlock recolors the character, enabled = false hides it' ($p7.Count -eq 6 -and [int]$p7[0] -ge 1 -and $p7[1] -eq 'True' -and [int]$p7[2] -ge 1 -and $p7[3] -eq 'True' -and $p7[4] -eq 'On' -and [double]$p7[5] -gt 1 -and $h1.Yellow -gt $h0.Yellow + 50 -and $h2.Yellow -lt $h0.Yellow + 20) ("$r7; yellow {0} -> {1} -> disabled {2}" -f $h0.Yellow, $h1.Yellow, $h2.Yellow)
        Invoke-Nova 'delete Hero' | Out-Null

        # 8. Sprite Renderer 도 Renderer: 재질은 없다 (null), 그림자 Off, bounds = 그림 사각형 × 크기
        $r8 = Exec 'var g = new GameObject("Spr"); var sr = g.AddComponent<SpriteRenderer>(); sr.sprite = Sprite.FromPath("builtin:Square"); g.transform.position = new Vector3(0f, 50f, 0f); g.transform.localScale = new Vector3(2f, 3f, 1f); Renderer r = g.GetComponent<Renderer>(); var bb = r.bounds; r.SetPropertyBlock(new MaterialPropertyBlock()); return (r is SpriteRenderer) + "|" + (r.material == null) + "|" + r.shadowCastingMode + "|" + bb.size.x.ToString("0.0") + "," + bb.size.y.ToString("0.0") + "|" + bb.center.y.ToString("0.0");'
        Add-Result $sn 'SpriteRenderer is a Renderer: no material (null), shadows Off, bounds = sprite rect' ("$r8" -eq 'True|True|Off|2.0,3.0|50.0') "$r8"
        Invoke-Nova 'delete Spr' | Out-Null

        # 9. 재질 칸 블록 (SetPropertyBlock(block, materialIndex)): 렌더러 블록 (청록) 위에 칸 0 블록 (노랑) — 칸 쪽이 이긴다, 칸 블록을 지우면 청록
        $r9a = Exec 'var r = GameObject.Find("P0").GetComponent<MeshRenderer>(); var c = new MaterialPropertyBlock(); c.SetColor("_BaseColor", new Color(0.1f, 0.8f, 0.85f, 1f)); r.SetPropertyBlock(c); var y = new MaterialPropertyBlock(); y.SetColor("_BaseColor", new Color(0.9f, 0.85f, 0.1f, 1f)); r.SetPropertyBlock(y, 0); var got = new MaterialPropertyBlock(); r.GetPropertyBlock(got, 0); var all = new MaterialPropertyBlock(); r.GetPropertyBlock(all); return got.GetColor("_BaseColor").b.ToString("0.0") + "|" + all.GetColor("_BaseColor").b.ToString("0.00") + "|" + r.HasPropertyBlock();'
        $c9a = Colors 'index_block.png'
        Exec 'GameObject.Find("P0").GetComponent<MeshRenderer>().SetPropertyBlock(null, 0); return 0;' | Out-Null
        $c9b = Colors 'index_block_cleared.png'
        Exec 'GameObject.Find("P0").GetComponent<MeshRenderer>().SetPropertyBlock(null); return 0;' | Out-Null
        Add-Result $sn 'SetPropertyBlock(block, materialIndex): the slot block overrides the renderer block, clearing it shows the renderer block again' ("$r9a" -eq '0.1|0.85|True' -and $c9a.Yellow -gt 20 -and $c9b.Yellow -lt 5 -and $c9b.Cyan -gt $c9a.Cyan) ("$r9a; yellow {0} -> {1}, cyan {2} -> {3}" -f $c9a.Yellow, $c9b.Yellow, $c9a.Cyan, $c9b.Cyan)

        # 10. Line Renderer 도 Renderer: GetComponent<Renderer>, bounds = 점 + 너비의 반, 재질 없음, 정렬 값 (sortingOrder · sortingLayerName)
        $lx = '{0}f, {1}f, {2}f' -f $cp[0], ($cp[1] + 3.6), ($z + 1)
        $r10 = Exec ('var center = new Vector3(' + $lx + '); var a = new GameObject("LineA").AddComponent<LineRenderer>(); a.SetPositions(new[] { center + new Vector3(-1.5f, 0, 0), center + new Vector3(1.5f, 0, 0) }); a.widthMultiplier = 0.5f; a.startColor = new Color(1, 0, 0, 1); a.endColor = new Color(1, 0, 0, 1); var b = new GameObject("LineB").AddComponent<LineRenderer>(); b.SetPositions(new[] { center + new Vector3(0, -1.5f, 0), center + new Vector3(0, 1.5f, 0) }); b.widthMultiplier = 0.5f; b.startColor = new Color(0, 0, 1, 1); b.endColor = new Color(0, 0, 1, 1); Renderer r = a.gameObject.GetComponent<Renderer>(); var bb = r.bounds; r.sortingOrder = 1; return (r is LineRenderer) + "|" + bb.size.x.ToString("0.0") + "," + bb.size.y.ToString("0.0") + "|" + (r.material == null) + "|" + r.shadowCastingMode + "|" + r.sortingOrder + "|" + r.sortingLayerName + "|" + r.enabled;')
        $c10a = Colors 'lines_red_on_top.png'
        Exec 'GameObject.Find("LineA").GetComponent<Renderer>().sortingOrder = 0; GameObject.Find("LineB").GetComponent<Renderer>().sortingOrder = 1; return 0;' | Out-Null
        $c10b = Colors 'lines_blue_on_top.png'
        Exec 'GameObject.Find("LineA").GetComponent<Renderer>().enabled = false; return 0;' | Out-Null
        $c10c = Colors 'line_a_disabled.png'
        Add-Result $sn 'LineRenderer is a Renderer: bounds, no material, shadows Off; sortingOrder decides which crossing line is on top; enabled = false hides it' ("$r10" -eq 'True|3.5,0.5|True|Off|1|Default|True' -and $c10a.Red -gt $c10b.Red -and $c10b.Blue -gt $c10a.Blue -and $c10c.Red -lt $c10b.Red * 0.3) ("$r10; red {0} -> {1} (blue on top), blue {2} -> {3}; A disabled red {4}" -f $c10a.Red, $c10b.Red, $c10a.Blue, $c10b.Blue, $c10c.Red)
        Invoke-Nova 'delete LineA' | Out-Null
        Invoke-Nova 'delete LineB' | Out-Null

        # 5. 사본은 씬에 저장되지 않는다 (Unity 처럼): 저장 · 다시 열면 A 는 공유 재질
        Invoke-Nova 'scene save --as Assets/MatTest/MatTest.scene' | Out-Null
        Invoke-Nova 'scene open Assets/MatTest/MatTest.scene --force' | Out-Null
        $c5 = Colors 'reopened.png'
        Add-Result $sn 'Runtime copies are not saved: after reopening, A uses the shared material again' ($c5.Blue -le $c0.Blue + 100 -and $c5.Green -gt $c4.Green * 0.8) ("blue {0} (sky only before: {3}), green {1}, red {2}" -f $c5.Blue, $c5.Green, $c5.Red, $c0.Blue)
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item -Recurse -Force $matDir -ErrorAction SilentlyContinue
        Remove-Item -Force "$matDir.meta" -ErrorAction SilentlyContinue
    }
}

function Suite-LineTrail
{
    # Line Renderer · Trail Renderer (Unity 이름): 하늘 앞 흰 선 — 끈 화면과의 차이로 굵기 · 색 · 끝 모양 · 꼬리를 잰다
    Write-Host '[linetrail]'
    $dir = Join-Path $Out 'linetrail'
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $ed = Start-TestEditor
    try
    {
        function Shot([string]$name, [string]$view = 'scene', [int]$frames = 4) { $p = Join-Path $dir $name; Invoke-Nova "wait $frames" | Out-Null; Invoke-Nova "screenshot $p --view $view" | Out-Null; $p }
        function Exec([string]$code) { $f = Join-Path $dir 'exec.cs'; $code | Set-Content -Encoding utf8 $f; (Invoke-NovaJson "exec --file $f").result }
        # 기준 화면과 다른 픽셀: 열 x (화면 비율) 에서 세로로 다른 픽셀 수, 행 y 에서 가로 범위, 다른 픽셀의 평균 색
        if (-not ('NovaLineMask' -as [type]))
        {
            Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class NovaLineMask
{
    // 두 그림에서 많이 다른 픽셀 (RGB 차 합 > 60) 표시와 그 픽셀의 평균 색 (두 번째 그림)
    public static object[] Diff(string a, string b)
    {
        using (var ba = new Bitmap(a)) using (var bb = new Bitmap(b))
        {
            int w = ba.Width, h = ba.Height; var m = new bool[w, h]; double r = 0, g = 0, bl = 0; int n = 0;
            var ra = new Rectangle(0, 0, w, h);
            var da = ba.LockBits(ra, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb); var db = bb.LockBits(ra, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var pa = new byte[da.Stride * h]; var pb = new byte[db.Stride * h];
            Marshal.Copy(da.Scan0, pa, 0, pa.Length); Marshal.Copy(db.Scan0, pb, 0, pb.Length);
            ba.UnlockBits(da); bb.UnlockBits(db);
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
            {
                int i = y * da.Stride + x * 4;
                int d = Math.Abs(pa[i] - pb[i]) + Math.Abs(pa[i + 1] - pb[i + 1]) + Math.Abs(pa[i + 2] - pb[i + 2]);
                if (d > 60) { m[x, y] = true; bl += pb[i]; g += pb[i + 1]; r += pb[i + 2]; n++; }
            }
            return new object[] { m, w, h, n, r / Math.Max(1, n), g / Math.Max(1, n), bl / Math.Max(1, n) };
        }
    }
    // 열 (화면 비율 fx) 에서 다른 픽셀 수
    public static int ColHeight(bool[,] m, double fx)
    {
        int x = (int)(m.GetLength(0) * fx), c = 0;
        for (int y = 0; y < m.GetLength(1); y++) if (m[x, y]) c++;
        return c;
    }
    // 다른 픽셀이 가장 많은 행의 가장 왼쪽 · 오른쪽
    public static int[] RowSpan(bool[,] m)
    {
        int w = m.GetLength(0), h = m.GetLength(1), best = 0, by = 0;
        for (int y = 0; y < h; y++) { int c = 0; for (int x = 0; x < w; x++) if (m[x, y]) c++; if (c > best) { best = c; by = y; } }
        int l = -1, r = -1;
        for (int x = 0; x < w; x++) if (m[x, by]) { if (l < 0) l = x; r = x; }
        return new[] { l, r, r - l };
    }
}
"@
        }
        function Mask([string]$a, [string]$b)
        {
            $d = [NovaLineMask]::Diff($a, $b)
            [pscustomobject]@{ M = $d[0]; W = [int]$d[1]; H = [int]$d[2]; Count = [int]$d[3]; R = [double]$d[4]; G = [double]$d[5]; B = [double]$d[6] }
        }
        function ColHeight($k, [double]$fx) { [NovaLineMask]::ColHeight($k.M, $fx) }
        function RowSpan($k) { $s = [NovaLineMask]::RowSpan($k.M); [pscustomobject]@{ L = $s[0]; R = $s[1]; Len = $s[2] } }

        Invoke-Nova 'scene new --force' | Out-Null
        # 바닥 (어두운 회청색) 앞에 선이 오게 위에서 비스듬히 — 지평선의 흰 구름은 흰 선과 구별되지 않는다
        Invoke-Nova 'camera --position 0,3,-6 --target 0,1,0' | Out-Null
        Invoke-Nova 'create empty --name Line' | Out-Null
        Invoke-Nova 'add-component Line LineRenderer' | Out-Null
        Invoke-Nova 'set Line --component LineRenderer --values "{\"positions\":[[-2,1,0],[2,1,0]],\"widthMultiplier\":0.3,\"enabled\":false}"' | Out-Null
        $base = Shot 'base.png'
        Invoke-Nova 'set Line --component LineRenderer --values "{\"enabled\":true}"' | Out-Null
        $p = Shot 'line.png'; $k = Mask $base $p
        # 너비 0.3 m, 거리 √40 m (카메라를 향한 띠), Scene 뷰 FOV 60 → 화면 높이의 0.3 / (2 · √40 · tan 30°)
        $expect = 0.3 / (2 * [math]::Sqrt(40) * [math]::Tan([math]::PI / 6)) * $k.H
        $hc = ColHeight $k 0.5
        Add-Result linetrail 'Line Renderer draws a white line, Width 0.3 m = expected pixels' ($k.Count -gt 500 -and [math]::Abs($hc - $expect) -le [math]::Max(3, $expect * 0.2) -and $k.R -gt 200 -and $k.G -gt 200 -and $k.B -gt 200) ("height {0} px (expected {1:N1}), colour {2:N0},{3:N0},{4:N0}" -f $hc, $expect, $k.R, $k.G, $k.B)

        # C#: startWidth · endWidth · startColor · endColor · positionCount
        $r = Exec 'var l = GameObject.Find("Line").GetComponent<LineRenderer>(); l.startWidth = 0.1f; l.endWidth = 0.5f; l.startColor = new Color(1, 0, 0, 1); l.endColor = new Color(1, 0, 0, 1); return l.positionCount + "," + l.startWidth.ToString("0.00") + "," + l.endWidth.ToString("0.00");'
        $p = Shot 'csharp.png'; $k = Mask $base $p
        $hl = ColHeight $k 0.42; $hr = ColHeight $k 0.58
        Add-Result linetrail 'C# startWidth / endWidth taper the line, startColor / endColor = red' ($r -eq '2,0.10,0.50' -and $hr -gt $hl * 2 -and $k.R -gt 180 -and $k.G -lt 90 -and $k.B -lt 90) ("C# {0}; left {1} px, right {2} px; colour {3:N0},{4:N0},{5:N0}" -f $r, $hl, $hr, $k.R, $k.G, $k.B)

        # C# 로 만든 그 자리에서 값 넣기 (AddComponent 한 컴포넌트는 다음 프레임까지 대기 목록)
        Exec 'var n = new GameObject("CsLine").AddComponent<LineRenderer>(); n.SetPositions(new[] { new Vector3(0, 0, 0), new Vector3(1, 0, 0), new Vector3(1, 1, 0) }); n.widthMultiplier = 0.05f; n.loop = true; return n.positionCount;' | Set-Variable made
        Invoke-Nova 'wait 2' | Out-Null
        $cl2 = Invoke-NovaJson 'get CsLine --component LineRenderer'
        Add-Result linetrail 'C# new GameObject().AddComponent<LineRenderer>() then SetPositions in the same frame' ($made -eq '3' -and $cl2.positions.Count -eq 3 -and $cl2.loop -and [math]::Abs($cl2.widthMultiplier - 0.05) -lt 1e-4) ("returned {0}, saved points {1}, loop {2}, width {3}" -f $made, $cl2.positions.Count, $cl2.loop, $cl2.widthMultiplier)
        Invoke-Nova 'delete CsLine' | Out-Null

        # End Cap Vertices: 끝이 너비의 반만큼 둥글게 늘어난다
        Exec 'var l = GameObject.Find("Line").GetComponent<LineRenderer>(); l.startWidth = 0.4f; l.endWidth = 0.4f; l.numCapVertices = 0; return 0;' | Out-Null
        $k0 = Mask $base (Shot 'cap0.png')
        Exec 'var l = GameObject.Find("Line").GetComponent<LineRenderer>(); l.numCapVertices = 8; return 0;' | Out-Null
        $k8 = Mask $base (Shot 'cap8.png')
        $s0 = RowSpan $k0; $s8 = RowSpan $k8
        $pxPerM = $s0.Len / 4.0
        Add-Result linetrail 'End Cap Vertices round the ends (each end longer by half the width)' ([math]::Abs(($s8.Len - $s0.Len) - 0.4 * $pxPerM) -le [math]::Max(4, 0.12 * $pxPerM)) ("span {0} -> {1} px (expected +{2:N1})" -f $s0.Len, $s8.Len, (0.4 * $pxPerM))

        # Trail Renderer: 움직인 길을 따라 띠 (Time 5 초), Time 을 줄이면 곧 사라진다
        Invoke-Nova 'set Line --component LineRenderer --values "{\"enabled\":false}"' | Out-Null
        Invoke-Nova 'create empty --name Mover --position -2,1,0' | Out-Null
        Invoke-Nova 'add-component Mover TrailRenderer' | Out-Null
        Invoke-Nova 'set Mover --component TrailRenderer --values "{\"time\":5,\"widthMultiplier\":0.3,\"minVertexDistance\":0.05}"' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        foreach ($x in @(-1.5, -1, -0.5, 0, 0.5, 1, 1.5, 2)) { Invoke-Nova ("set Mover --position {0},1,0" -f $x) | Out-Null; Invoke-Nova 'wait 2' | Out-Null }
        $p = Shot 'trail.png' 'scene' 2; $k = Mask $base $p; $s = RowSpan $k
        $cnt = Exec 'return GameObject.Find("Mover").GetComponent<TrailRenderer>().positionCount;'
        Add-Result linetrail 'Trail Renderer follows the moved object (a band along its path), C# positionCount' ($k.Count -gt 500 -and $s.Len -gt $pxPerM * 2.5 -and [int]$cnt -ge 5) ("band {0} px wide ({1:N1} m), points {2}" -f $s.Len, ($s.Len / [math]::Max(1, $pxPerM)), $cnt)
        Invoke-Nova 'set Mover --component TrailRenderer --values "{\"time\":0.5}"' | Out-Null
        Start-Sleep -Milliseconds 900
        $p = Shot 'trail_gone.png'; $k = Mask $base $p
        Add-Result linetrail 'Trail fades out after Time (0.5 s) once the object stops' ($k.Count -lt 50) ("{0} differing pixels" -f $k.Count)
        Exec 'var t = GameObject.Find("Mover").GetComponent<TrailRenderer>(); t.AddPosition(new Vector3(-2, 1, 0)); var n = t.positionCount; t.Clear(); return n + "," + t.positionCount;' | Set-Variable cl
        Add-Result linetrail 'C# TrailRenderer.AddPosition / Clear' ($cl -match '^[1-9]\d*,0$') $cl

        # 저장 · 다시 열기 · Game 뷰
        Invoke-Nova 'set Line --component LineRenderer --values "{\"enabled\":true}"' | Out-Null
        $assetDir = Join-Path $Project 'Assets\LineTest'; New-Item -ItemType Directory -Force $assetDir | Out-Null
        Invoke-Nova 'scene save --as Assets/LineTest/Lines.scene' | Out-Null
        Invoke-Nova 'scene open Assets/LineTest/Lines.scene --force' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $c = Invoke-NovaJson 'get Line --component LineRenderer'
        $t = Invoke-NovaJson 'get Mover --component TrailRenderer'
        Add-Result linetrail 'Save → reopen keeps positions, width, caps and trail time' ($c.positions.Count -eq 2 -and $c.numCapVertices -eq 8 -and [math]::Abs($c.widthMultiplier - 0.3) -lt 1e-4 -and [math]::Abs($t.time - 0.5) -lt 1e-4) ("points {0}, caps {1}, width x{2}, trail time {3}" -f $c.positions.Count, $c.numCapVertices, $c.widthMultiplier, $t.time)
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1,-6 --rotation 0,0,0' | Out-Null
        Invoke-Nova 'set Line --component LineRenderer --values "{\"enabled\":false}"' | Out-Null
        $g0 = Shot 'game_off.png' 'game' 6
        Invoke-Nova 'set Line --component LineRenderer --values "{\"enabled\":true}"' | Out-Null
        $g1 = Shot 'game_on.png' 'game' 6
        $k = Mask $g0 $g1
        Invoke-Nova 'window scene' | Out-Null
        Add-Result linetrail 'Line also draws in the Game view' ($k.Count -gt 500) ("{0} differing pixels" -f $k.Count)
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item (Join-Path $Project 'Assets\LineTest'), (Join-Path $Project 'Assets\LineTest.meta') -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-SSAO
{
    # Screen Space Ambient Occlusion (Volume — URP SSAO 와 같은 값): 바닥 위 상자 · 벽. `nova ssao map` 으로 AO 맵 (흰 = 가림 없음) 을 직접 읽는다
    #  맞닿은 곳만 어둡다 (평평한 바닥 · 하늘은 1), Enable · Intensity · Radius · Samples · Falloff Distance · Direct Lighting Strength,
    #  Game 뷰의 시야각을 바꿔도 바닥이 가려지지 않는다 (예전: 화면 구석 방향을 처음 크기 · 시야각으로만), Scene · Game 뷰 같음, OpenGL = DX11,
    #  2 차: 윤곽에 선이 남지 않는다 (떠 있는 상자), 시간 누적 (히스토리 · 노이즈 감소 · 옮긴 상자의 잔상 없음), Full Resolution, TAA 지터
    Write-Host '[ssao]'
    $dir = Join-Path $Out 'ssao'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\SsaoTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    function AoProfile([string]$name, [hashtable]$vals)
    {
        $params = @{}
        foreach ($k in $vals.Keys) { $params[$k] = @{ override = $true; value = @($vals[$k], 0, 0, 0) } }
        @{ nova_volume_profile = 1; components = @(@{ type = 'AmbientOcclusion'; active = $true; params = $params }) } | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $assetDir "$name.volumeprofile")
    }
    AoProfile 'Off' @{ enabled = 0 }
    AoProfile 'On' @{ enabled = 1 }
    AoProfile 'Strong' @{ enabled = 1; intensity = 2 }
    AoProfile 'Zero' @{ enabled = 1; intensity = 0 }
    AoProfile 'Wide' @{ enabled = 1; radius = 1.5 }
    AoProfile 'Near' @{ enabled = 1; falloffDistance = 1 }
    AoProfile 'Low' @{ enabled = 1; samples = 0 }
    AoProfile 'High' @{ enabled = 1; samples = 2 }
    AoProfile 'Direct0' @{ enabled = 1; intensity = 2; directLightingStrength = 0 }
    AoProfile 'Direct1' @{ enabled = 1; intensity = 2; directLightingStrength = 1 }
    AoProfile 'Still' @{ enabled = 1; temporalAccumulation = 0 }
    AoProfile 'Full' @{ enabled = 1; fullResolution = 1 }
    AoProfile 'LowStill' @{ enabled = 1; samples = 0; temporalAccumulation = 0 }
    AoProfile 'LowAcc' @{ enabled = 1; samples = 0; temporalAccumulation = 1 }
    AoProfile 'HighStill' @{ enabled = 1; samples = 2; temporalAccumulation = 0 }
    # AO 맵의 영역 (그림 크기의 비율): 벽 밑 · 상자 밑이 바닥에 닿는 곳, 열린 바닥 (아래), 하늘 (왼쪽 위)
    $contact = @(0.48, 0.68, 0.48, 0.56)
    $box = @(0.34, 0.47, 0.58, 0.66)
    $open = @(0.02, 0.98, 0.80, 0.98)
    $sky = @(0.0, 0.3, 0.0, 0.25)
    function AoStats([string]$png, [double[]]$r)
    {
        if (-not (Test-Path $png)) { return [pscustomobject]@{ Mean = -1; Min = -1; Dark = -1 } }
        $bm = [System.Drawing.Bitmap]::FromFile($png); $n = 0; $sum = 0.0; $lo = 1.0; $dark = 0
        for ($y = [int]($bm.Height * $r[2]); $y -lt [int]($bm.Height * $r[3]); $y++) { for ($x = [int]($bm.Width * $r[0]); $x -lt [int]($bm.Width * $r[1]); $x++) {
            $v = $bm.GetPixel($x, $y).R / 255.0; $n++; $sum += $v; if ($v -lt $lo) { $lo = $v }; if ($v -lt 0.95) { $dark++ } } }
        $bm.Dispose(); $n = [math]::Max(1, $n)
        [pscustomobject]@{ Mean = $sum / $n; Min = $lo; Dark = $dark / $n }
    }
    function Luma([string]$png, [double[]]$r)
    {
        $bm = [System.Drawing.Bitmap]::FromFile($png); $n = 0; $sum = 0.0
        for ($y = [int]($bm.Height * $r[2]); $y -lt [int]($bm.Height * $r[3]); $y += 2) { for ($x = [int]($bm.Width * $r[0]); $x -lt [int]($bm.Width * $r[1]); $x += 2) {
            $c = $bm.GetPixel($x, $y); $n++; $sum += 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B } }
        $bm.Dispose(); $sum / [math]::Max(1, $n)
    }
    # 이웃 픽셀 (가로) 차이의 평균 — 노이즈
    function AoNoise([string]$png, [double[]]$r)
    {
        $bm = [System.Drawing.Bitmap]::FromFile($png); $n = 0; $sum = 0.0
        for ($y = [int]($bm.Height * $r[2]); $y -lt [int]($bm.Height * $r[3]); $y++) { for ($x = [int]($bm.Width * $r[0]); $x -lt [int]($bm.Width * $r[1]) - 1; $x++) {
            $sum += [math]::Abs($bm.GetPixel($x, $y).R - $bm.GetPixel($x + 1, $y).R); $n++ } }
        $bm.Dispose(); $sum / [math]::Max(1, $n)
    }
    # 두 AO 맵의 영역 안 픽셀별 차이의 평균 (0 ~ 255)
    function AoDiff([string]$a, [string]$b, [double[]]$r)
    {
        $x1 = [System.Drawing.Bitmap]::FromFile($a); $x2 = [System.Drawing.Bitmap]::FromFile($b); $n = 0; $sum = 0.0
        for ($y = [int]($x1.Height * $r[2]); $y -lt [int]($x1.Height * $r[3]); $y++) { for ($x = [int]($x1.Width * $r[0]); $x -lt [int]($x1.Width * $r[1]); $x++) {
            $sum += [math]::Abs($x1.GetPixel($x, $y).R - $x2.GetPixel($x, $y).R); $n++ } }
        $x1.Dispose(); $x2.Dispose(); $sum / [math]::Max(1, $n)
    }
    function SsaoScene
    {
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,2 --scale 30,1,30' | Out-Null
        Invoke-Nova 'create cube --name Box --position -0.8,0.5,1.5' | Out-Null
        Invoke-Nova 'create cube --name Wall --position 0.8,1.5,2.6 --scale 3,3,0.3' | Out-Null
        Invoke-Nova 'camera --position 0,1.8,-2.5 --target 0,0.4,1.8' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.8,-2.5 --rotation 18.03,0,0' | Out-Null   # Scene 뷰 카메라와 같은 방향 (0,0.4,1.8 을 본다)
    }
    function WaitSsaoShader { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 120) { $t = Invoke-Nova 'log -n 400'; if ($t -match 'compiled 28\. Ssao\.fx|cache hit 28\. Ssao\.fx') { break }; Start-Sleep -Milliseconds 500 } }
    function Vol([string]$name) { Invoke-Nova ('set "Global Volume" --component Volume --values "{\"profile\":\"Assets/SsaoTest/' + $name + '.volumeprofile\"}"') | Out-Null; Invoke-Nova 'wait 8' | Out-Null }
    function AoMap([string]$name, [string]$view = 'scene') { $p = Join-Path $dir "$name.png"; $j = Invoke-NovaJson ('ssao map "' + $p + '" --view ' + $view); [pscustomobject]@{ Path = $p; Info = $j } }
    $ed = Start-TestEditor
    $dxOn = $null
    try
    {
        WaitSsaoShader
        SsaoScene

        Vol 'Off'; $off = AoMap 'off'
        $offAll = AoStats $off.Path @(0, 1, 0, 1)
        $offShot = Join-Path $dir 'shot_off.png'; Invoke-Nova "screenshot `"$offShot`" --view scene" | Out-Null
        Vol 'On'; $on = AoMap 'on'; $dxOn = $on.Path
        $onShot = Join-Path $dir 'shot_on.png'; Invoke-Nova "screenshot `"$onShot`" --view scene" | Out-Null
        Add-Result ssao 'Enable off: no AO (the map is white), the Volume reports it inactive' ($off.Info -and -not $off.Info.active -and $offAll.Min -ge 0.99) ("active {0}, map min {1:N3}" -f $off.Info.active, $offAll.Min)

        $c = AoStats $on.Path $contact; $b = AoStats $on.Path $box; $o = AoStats $on.Path $open; $s = AoStats $on.Path $sky
        # 열린 바닥은 평균도 1 에 가깝다 — 반 해상도에서 깊이를 읽은 픽셀과 다른 자리의 방향으로 위치를 되짚으면 비스듬한 바닥 전체가 옅게 가려졌다 (평균 0.993)
        Add-Result ssao 'On: darker where the box and the wall meet the ground, flat ground (no haze) and sky stay open' ($on.Info.active -and $on.Info.samples -eq 8 -and $c.Min -lt 0.8 -and $c.Mean -lt $o.Mean - 0.05 -and $b.Min -lt 0.8 -and $o.Min -ge 0.97 -and $o.Mean -ge 0.998 -and $s.Min -ge 0.99) ("contact mean {0:N3} min {1:N2}, box base min {2:N2}, open ground mean {3:N4} min {4:N3}, sky min {5:N3}" -f $c.Mean, $c.Min, $b.Min, $o.Mean, $o.Min, $s.Min)
        # 최종 그림: 맞닿은 곳만 어두워지고 열린 바닥은 그대로
        $lc0 = Luma $offShot $contact; $lc1 = Luma $onShot $contact; $lo0 = Luma $offShot $open; $lo1 = Luma $onShot $open
        Add-Result ssao 'AO reaches the lit image: the contact gets darker, open ground is unchanged' ($lc1 -lt $lc0 - 4 -and [math]::Abs($lo1 - $lo0) -lt 1.5) ("contact luminance {0:N1} -> {1:N1}, open ground {2:N1} -> {3:N1}" -f $lc0, $lc1, $lo0, $lo1)

        Vol 'Strong'; $c2 = AoStats (AoMap 'strong').Path $contact
        Vol 'Zero'; $z = AoMap 'zero'; $zAll = AoStats $z.Path @(0, 1, 0, 1)
        Add-Result ssao 'Intensity: 2 is darker than 1, 0 = no AO' ($c2.Mean -lt $c.Mean - 0.04 -and -not $z.Info.active -and $zAll.Min -ge 0.99) ("contact mean x1 {0:N3}, x2 {1:N3}; intensity 0 min {2:N3}" -f $c.Mean, $c2.Mean, $zAll.Min)

        Vol 'Wide'; $wAll = AoStats (AoMap 'wide').Path @(0, 1, 0, 1); $onAll = AoStats $on.Path @(0, 1, 0, 1)
        Add-Result ssao 'Radius 1.5 m: the occlusion reaches further than 0.5 m' ($wAll.Dark -gt $onAll.Dark * 1.5 -and $wAll.Dark -gt 0) ("pixels under 0.95: radius 0.5 {0:P1}, radius 1.5 {1:P1}" -f $onAll.Dark, $wAll.Dark)

        Vol 'Low'; $lw = AoMap 'low'; $cl = AoStats $lw.Path $contact
        Vol 'High'; $hg = AoMap 'high'; $ch = AoStats $hg.Path $contact; $oh = AoStats $hg.Path $open
        Add-Result ssao 'Samples Low 4 / High 14: both darken the contact, ground stays open' ($lw.Info.samples -eq 4 -and $hg.Info.samples -eq 14 -and $cl.Mean -lt 0.95 -and $ch.Mean -lt 0.95 -and $oh.Min -ge 0.97) ("samples {0} / {1}, contact mean {2:N2} / {3:N2}" -f $lw.Info.samples, $hg.Info.samples, $cl.Mean, $ch.Mean)

        Vol 'Near'; $nAll = AoStats (AoMap 'near').Path @(0, 1, 0, 1)
        Add-Result ssao 'Falloff Distance 1 m: everything farther than 1 m from the camera has no AO' ($nAll.Min -ge 0.99) ("map min {0:N3}" -f $nAll.Min)

        # Direct Lighting Strength: 0 = AO 는 환경광만, 1 = 직접광에도 (맞닿은 곳이 더 어둡다)
        Vol 'Direct0'; $d0 = Join-Path $dir 'shot_direct0.png'; Invoke-Nova "screenshot `"$d0`" --view scene" | Out-Null
        Vol 'Direct1'; $d1 = Join-Path $dir 'shot_direct1.png'; Invoke-Nova "screenshot `"$d1`" --view scene" | Out-Null
        $ld0 = Luma $d0 $contact; $ld1 = Luma $d1 $contact; $lg0 = Luma $d0 $open; $lg1 = Luma $d1 $open
        Add-Result ssao 'Direct Lighting Strength 1: AO also darkens the direct light at the contact (open ground unchanged)' ($ld1 -lt $ld0 - 1 -and [math]::Abs($lg1 - $lg0) -lt 1.5) ("contact luminance {0:N1} -> {1:N1}, open ground {2:N1} -> {3:N1}" -f $ld0, $ld1, $lg0, $lg1)

        # 시간 누적: 같은 값 · 같은 크기면 히스토리를 이어 쓴다 (값이 바뀌면 버린다)
        Vol 'On'; Invoke-Nova 'wait 40' | Out-Null
        $ai = Invoke-NovaJson 'ssao info --view scene'
        $ca = AoStats (AoMap 'accumulated').Path $contact
        Add-Result ssao 'Temporal Accumulation: the history carries on while the settings stay the same' ($ai.temporalAccumulation -and $ai.historyValid -and [int]$ai.accumulatedFrames -gt 10 -and $ca.Mean -lt 0.95) ("history {0}, {1} frames, contact mean {2:N2}" -f $ai.historyValid, $ai.accumulatedFrames, $ca.Mean)

        # 잔상 없음: 상자를 옮기면 옛 자리 (바닥) 의 AO 가 몇 프레임 안에 사라진다 (같은 깊이라 깊이 판정은 통과 — 이웃 범위로 자른다)
        $boxBase = @(0.32, 0.49, 0.56, 0.68)
        $before = AoStats (AoMap 'box_before').Path $boxBase
        Invoke-Nova 'set Box --position -3.5,0.5,1.5' | Out-Null
        Invoke-Nova 'wait 6' | Out-Null
        $after = AoStats (AoMap 'box_moved').Path $boxBase
        Add-Result ssao 'Temporal: moving the box leaves no AO ghost at its old place within a few frames' ($before.Min -lt 0.8 -and $after.Min -ge 0.95) ("old place min {0:N2} -> {1:N2} after 6 frames" -f $before.Min, $after.Min)
        Invoke-Nova 'set Box --position -0.8,0.5,1.5' | Out-Null

        # Full Resolution: AO 를 화면 크기로 (기본은 반)
        $half = Invoke-NovaJson 'ssao info --view scene'
        Vol 'Full'; Invoke-Nova 'wait 20' | Out-Null; $fm = AoMap 'full'; $fc = AoStats $fm.Path $contact; $fo = AoStats $fm.Path $open
        Add-Result ssao 'Full Resolution: AO computed at the view size (half by default), still only at the contact' ($fm.Info.fullResolution -and $fm.Info.aoSize[0] -eq $fm.Info.mapSize[0] -and [math]::Abs($half.aoSize[0] * 2 - $half.mapSize[0]) -le 1 -and $fc.Mean -lt 0.95 -and $fo.Min -ge 0.97) ("ao {0}x{1} of {2}x{3} (half: {4}x{5}); contact mean {6:N2}, ground min {7:N3}" -f $fm.Info.aoSize[0], $fm.Info.aoSize[1], $fm.Info.mapSize[0], $fm.Info.mapSize[1], $half.aoSize[0], $half.aoSize[1], $fc.Mean, $fo.Min)

        # 윤곽: 벽 앞 3.5 m 에 떠 있는 상자는 가릴 것이 없다 — 둘레 (왼쪽 절반) 에 선이 없어야. 벽에 붙은 상자 (오른쪽) 는 어둡다
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name BackWall --position 0,1.5,4.65 --scale 14,9,0.3' | Out-Null
        Invoke-Nova 'create cube --name Floating --position -1.3,1.5,1' | Out-Null
        Invoke-Nova 'create cube --name Touching --position 1.6,1.5,4' | Out-Null
        Invoke-Nova 'camera --position 0,1.5,-2 --target 0,1.5,4.5' | Out-Null
        foreach ($v in @('Still', 'On', 'Full'))
        {
            Vol $v; Invoke-Nova 'wait 20' | Out-Null
            $sm = AoMap "silhouette_$($v.ToLower())"
            $left = AoStats $sm.Path @(0.0, 0.48, 0.0, 1.0); $right = AoStats $sm.Path @(0.52, 1.0, 0.0, 1.0)
            Add-Result ssao "No lines along silhouettes ($v): a cube floating 3.5 m in front of a wall gets no AO, a cube touching it does" ($left.Min -ge 0.95 -and $right.Min -lt 0.8) ("floating cube side min {0:N3} (pixels under 0.95: {1:P2}), touching cube side min {2:N2}" -f $left.Min, $left.Dark, $right.Min)
        }
        SsaoScene

        # Game 뷰 (Main Camera): Scene 뷰와 같은 AO, 시야각을 바꿔도 바닥이 가려지지 않는다
        Vol 'On'
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null
        $g = AoMap 'game_on' 'game'; $gc = AoStats $g.Path $contact; $go = AoStats $g.Path $open; $gAll = AoStats $g.Path @(0, 1, 0, 1)
        Invoke-Nova 'set "Main Camera" --component Camera --values "{\"fovY\":0.6}"' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $gz = AoMap 'game_fov' 'game'; $gzo = AoStats $gz.Path $open; $gzAll = AoStats $gz.Path @(0, 1, 0, 1)
        Add-Result ssao 'Game view: same AO as the Scene view; a narrower field of view keeps the flat ground open' ($gc.Mean -lt 0.95 -and $gAll.Min -lt 0.8 -and $go.Min -ge 0.97 -and $gzAll.Min -lt 0.8 -and $gzo.Min -ge 0.97) ("contact mean {0:N2}, map min {1:N2}, ground min {2:N3}; FOV 0.6 rad: map min {3:N2}, ground min {4:N3}" -f $gc.Mean, $gAll.Min, $go.Min, $gzAll.Min, $gzo.Min)
        # TAA (지터한 투영): AO 도 같은 지터로 되짚는다 — 평평한 바닥이 가려지지 않고 맞닿은 곳만.
        #  지터는 깊이를 프레임마다 서브픽셀만큼 흔들어 AO 를 깜빡이게 한다 → 시간 누적이 연속 프레임의 차이를 줄인다
        $aoArea = @(0.25, 0.75, 0.40, 0.75)
        Invoke-Nova 'set "Main Camera" --component Camera --values "{\"fovY\":1.0471976,\"antiAliasing\":3}"' | Out-Null
        Vol 'Still'; Invoke-Nova 'wait 20' | Out-Null
        $s1 = (AoMap 'taa_still_1' 'game').Path; $s2 = (AoMap 'taa_still_2' 'game').Path
        Vol 'On'; Invoke-Nova 'wait 40' | Out-Null
        $gt = AoMap 'taa_acc_1' 'game'; $gt2 = AoMap 'taa_acc_2' 'game'
        $gtc = AoStats $gt.Path $contact; $gto = AoStats $gt.Path $open
        $flickStill = AoDiff $s1 $s2 $aoArea; $flickAcc = AoDiff $gt.Path $gt2.Path $aoArea
        Add-Result ssao 'Game view with TAA: the ground stays open, temporal accumulation steadies the AO from frame to frame' ($gtc.Mean -lt 0.95 -and $gto.Min -ge 0.97 -and $flickAcc -lt $flickStill) ("contact mean {0:N3}, ground min {1:N3}; frame-to-frame difference: fixed samples {2:N2}, accumulated {3:N2} (of 255)" -f $gtc.Mean, $gto.Min, $flickStill, $flickAcc)
        Invoke-Nova 'set "Main Camera" --component Camera --values "{\"antiAliasing\":0}"' | Out-Null
        # OpenGL 비교용: 표본을 고정한 그림 (시간 누적은 프레임마다 무늬가 달라 API 사이에 비교할 수 없다)
        Invoke-Nova 'window scene' | Out-Null
        Vol 'Still'; Invoke-Nova 'wait 10' | Out-Null
        $dxOn = (AoMap 'still_dx').Path
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }

    # OpenGL = DX11 (같은 장면 · 같은 AO 맵)
    $edGl = Start-TestEditor -OpenGL
    try
    {
        WaitSsaoShader
        SsaoScene
        Vol 'Still'; Invoke-Nova 'wait 10' | Out-Null
        $gl = AoMap 'still_gl'
        $diff = -1
        if ($dxOn -and (Test-Path $dxOn) -and (Test-Path $gl.Path))
        {
            $a = [System.Drawing.Bitmap]::FromFile($dxOn); $bgl = [System.Drawing.Bitmap]::FromFile($gl.Path)
            if ($a.Width -eq $bgl.Width -and $a.Height -eq $bgl.Height)
            {
                $sum = 0.0; $n = 0
                for ($y = 0; $y -lt $a.Height; $y += 2) { for ($x = 0; $x -lt $a.Width; $x += 2) { $sum += [math]::Abs($a.GetPixel($x, $y).R - $bgl.GetPixel($x, $y).R); $n++ } }
                $diff = $sum / [math]::Max(1, $n)
            }
            $a.Dispose(); $bgl.Dispose()
        }
        $glc = AoStats $gl.Path $contact
        Add-Result ssao 'OpenGL: the same AO map as DirectX 11' ($diff -ge 0 -and $diff -lt 2.0 -and $glc.Mean -lt 0.9) ("mean difference {0:N2} / 255, contact mean {1:N2}" -f $diff, $glc.Mean)
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $edGl)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-MotionVectors
{
    # 모션 벡터 (Volume 의 Motion Vectors — Game 뷰): `nova motionvectors map --rect` 로 영역의 화면 속도 (픽셀) 를 읽는다. 영역 = 카메라 투영으로 계산.
    #  편집 중 (멈춤) = 0, Play: 오른쪽으로 가는 상자 (+x) · 도는 판 · 걷는 캐릭터 (스킨) 는 움직이고 멈춘 상자는 0,
    #  Volume 의 Object Motion · Skinned Motion 끄기, 렌더러의 Force No Motion, 끄면 만들지 않음, 카메라가 움직이면 멈춘 상자가 왼쪽으로,
    #  TAA: 움직이는 체커 상자의 무늬가 덜 뭉개진다 (모션 벡터 = 물체를 따라 히스토리), Motion Blur Camera And Objects: 움직이는 상자가 흐려진다, OpenGL
    #  Play 중에는 CLI set 이 막혀 있어 설정마다 Play 를 다시 시작한다
    Write-Host '[motionvectors]'
    $dir = Join-Path $Out 'motionvectors'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\MotionTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    # 체커 (빨강 · 진회색 8 px 칸) 재질 — TAA 의 무늬 뭉개짐을 잰다
    $bmp = New-Object System.Drawing.Bitmap 64, 64
    for ($y = 0; $y -lt 64; $y++) { for ($x = 0; $x -lt 64; $x++) {
        $on = (([math]::Floor($x / 8) + [math]::Floor($y / 8)) % 2) -eq 0
        $bmp.SetPixel($x, $y, $(if ($on) { [System.Drawing.Color]::FromArgb(255, 230, 30, 30) } else { [System.Drawing.Color]::FromArgb(255, 40, 40, 40) })) } }
    $bmp.Save((Join-Path $assetDir 'checker.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $mat = (Get-Content (Join-Path $Project 'Assets\Materials\Red Plastic.mat') -Raw | ConvertFrom-Json).PSObject.Copy()
    $mat.BaseColor = @(1, 1, 1, 1); $mat.Metallic = 0; $mat.Smoothness = 0.3; $mat.BaseMapPath = 'Assets\MotionTest\checker.png'; $mat.ResourcePath = 'Assets\MotionTest\Checker.mat'
    $mat | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir 'Checker.mat')
    # 한 가지 빨강 (Motion Blur 가장자리 재기)
    $red = (Get-Content (Join-Path $Project 'Assets\Materials\Red Plastic.mat') -Raw | ConvertFrom-Json).PSObject.Copy()
    $red.ResourcePath = 'Assets\MotionTest\Red.mat'
    $red | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir 'Red.mat')
    function MvProfile([string]$name, [array]$comps) { @{ nova_volume_profile = 1; components = $comps } | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $assetDir "$name.volumeprofile") }
    function PV($v) { @{ override = $true; value = @($v, 0, 0, 0) } }
    MvProfile 'On' @()
    MvProfile 'Off' @(@{ type = 'MotionVectors'; active = $true; params = @{ enabled = (PV 0) } })
    MvProfile 'NoObject' @(@{ type = 'MotionVectors'; active = $true; params = @{ objectMotion = (PV 0) } })
    MvProfile 'NoSkinned' @(@{ type = 'MotionVectors'; active = $true; params = @{ skinnedMotion = (PV 0) } })
    MvProfile 'MbCam' @(@{ type = 'MotionBlur'; active = $true; params = @{ intensity = (PV 1); mode = (PV 0); quality = (PV 2) } })
    MvProfile 'MbObj' @(@{ type = 'MotionBlur'; active = $true; params = @{ intensity = (PV 1); mode = (PV 1); quality = (PV 2) } })

    $ed = Start-TestEditor
    $glDone = $false
    try
    {
        function Wait-Sec([double]$s) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Invoke-Nova 'wait 10' | Out-Null } }
        function Exec([string]$name, [string]$code) { $f = Join-Path $dir "$name.cs"; $code | Set-Content -Encoding utf8 $f; $r = Invoke-NovaJson "exec --file `"$f`""; if ($r) { "$($r.result)" } else { '' } }
        function Vol([string]$name) { Invoke-Nova ('set "Global Volume" --component Volume --values "{\"profile\":\"Assets/MotionTest/' + $name + '.volumeprofile\"}"') | Out-Null }
        function SetCam([int]$aa) { Invoke-Nova ('set "Main Camera" --component Camera --values "{\"antiAliasing\":' + $aa + '}"') | Out-Null }
        function SetSpeed([double]$v) { Invoke-Nova ('set Mover --component MotionMover --values "{\"speed\":' + $v.ToString([Globalization.CultureInfo]::InvariantCulture) + '}"') | Out-Null }
        # 영역의 속도 (픽셀): meanPixels · maxPixels · movingShare
        function MvRect([string]$name, [double[]]$r)
        {
            $line = 'motionvectors map "' + (Join-Path $dir "$name.png") + '" --rect ' + (($r | ForEach-Object { $_.ToString('F4', [Globalization.CultureInfo]::InvariantCulture) }) -join ',')
            Invoke-NovaJson $line
        }
        # 월드 상자 → 화면 비율 영역 (Main Camera: 위치 · 아래로 숙인 각 · 세로 시야각, 화면비 = 맵 크기)
        $script:mvAspect = 2.0
        function Rect([double[]]$lo, [double[]]$hi)
        {
            $p = 8.0 * [math]::PI / 180.0; $t = [math]::Tan($script:mvFovY / 2.0)
            $us = @(); $vs = @()
            foreach ($x in @($lo[0], $hi[0])) { foreach ($y in @($lo[1], $hi[1])) { foreach ($z in @($lo[2], $hi[2])) {
                $dx = $x; $dy = $y - 1.5; $dz = $z + 6.0
                $xv = $dx; $yv = $dy * [math]::Cos($p) + $dz * [math]::Sin($p); $zv = -$dy * [math]::Sin($p) + $dz * [math]::Cos($p)
                $us += 0.5 + 0.5 * $xv / ($zv * $t * $script:mvAspect); $vs += 0.5 - 0.5 * $yv / ($zv * $t) } } }
            $pad = 0.01
            # 0.0 · 1.0 (정수 0 · 1 을 주면 [math]::Max 가 정수 오버로드로 반올림한다)
            @([math]::Max(0.0, ($us | Measure-Object -Minimum).Minimum - $pad), [math]::Max(0.0, ($vs | Measure-Object -Minimum).Minimum - $pad),
              [math]::Min(1.0, ($us | Measure-Object -Maximum).Maximum + $pad), [math]::Min(1.0, ($vs | Measure-Object -Maximum).Maximum + $pad))
        }
        # 체커 상자 (빨간 칸) 안 빨강 채널 대비 · 가장자리의 반쯤 섞인 픽셀 (행마다)
        function ShotStats([string]$png)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($png)
            $w = $bm.Width; $h = $bm.Height; $xs = @(); $ys = @()
            for ($y = 0; $y -lt $h; $y += 2) { for ($x = 0; $x -lt $w; $x += 2) { $c = $bm.GetPixel($x, $y); if ($c.R - $c.G -gt 80) { $xs += $x; $ys += $y } } }
            if ($xs.Count -lt 30) { $bm.Dispose(); return $null }
            $xs = $xs | Sort-Object; $ys = $ys | Sort-Object
            $x0 = $xs[[int]($xs.Count * 0.05)]; $x1 = $xs[[int]($xs.Count * 0.95)]; $y0 = $ys[[int]($ys.Count * 0.05)]; $y1 = $ys[[int]($ys.Count * 0.95)]
            $n = 0; $s = 0.0; $s2 = 0.0
            for ($y = $y0; $y -le $y1; $y++) { for ($x = $x0; $x -le $x1; $x++) { $r = $bm.GetPixel($x, $y).R; $n++; $s += $r; $s2 += $r * $r } }
            $mean = $s / [math]::Max(1, $n)
            $bm.Dispose()
            [pscustomobject]@{ Contrast = [math]::Sqrt([math]::Max(0, $s2 / [math]::Max(1, $n) - $mean * $mean)) }
        }
        function EdgeBlur([string]$png)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($png)
            $w = $bm.Width; $h = $bm.Height; $xs = @(); $ys = @()
            for ($y = 0; $y -lt $h; $y += 2) { for ($x = 0; $x -lt $w; $x += 2) { $c = $bm.GetPixel($x, $y); if ($c.R - $c.G -gt 60) { $xs += $x; $ys += $y } } }
            if ($xs.Count -lt 30) { $bm.Dispose(); return -1 }
            $x0 = ($xs | Measure-Object -Minimum).Minimum; $x1 = ($xs | Measure-Object -Maximum).Maximum; $y0 = ($ys | Measure-Object -Minimum).Minimum; $y1 = ($ys | Measure-Object -Maximum).Maximum
            $ym0 = [int]($y0 + ($y1 - $y0) / 4); $ym1 = [int]($y1 - ($y1 - $y0) / 4); $cnt = 0
            for ($y = $ym0; $y -lt $ym1; $y++) { for ($x = [math]::Max(0, $x0 - 40); $x -lt [math]::Min($w, $x1 + 40); $x++) { $c = $bm.GetPixel($x, $y); $d = $c.R - $c.G; if ($d -gt 8 -and $d -le 60) { $cnt++ } } }
            $bm.Dispose()
            $cnt / [math]::Max(1, $ym1 - $ym0)
        }
        function Median($a) { $s = @($a | Where-Object { $_ -ne $null } | Sort-Object); if ($s.Count -eq 0) { return -1 }; $s[[int]($s.Count / 2)] }
        # 흐린 가장자리 바깥쪽 (빨간 기가 그 행 가장 진한 값의 6 ~ 30 % 인 픽셀 수, 상자 가운데 행들의 중앙값)
        #  예전 Motion Blur (픽셀마다 자기 속도로만) 는 배경 픽셀이 물체를 모으지 않고 물체 안쪽 가장자리는 절반 이상 빨강 → 0 에 가깝다.
        #  타일 최대 속도면 배경 위로 번져 가장자리마다 흐림 길이의 반쯤
        function Fringe([string]$png)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($png)
            $w = $bm.Width; $h = $bm.Height; $rows = @()
            for ($y = 0; $y -lt $h; $y += 2) { for ($x = 0; $x -lt $w; $x += 3) { $c = $bm.GetPixel($x, $y); if ($c.R - [math]::Max($c.G, $c.B) -gt 60) { $rows += $y; break } } }
            if ($rows.Count -lt 4) { $bm.Dispose(); return $null }
            $rows = $rows[[int]($rows.Count / 4)..[int]($rows.Count * 3 / 4 - 1)]
            $fs = @()
            foreach ($y in $rows)
            {
                $t = New-Object double[] $w; $mx = 0.0
                for ($x = 0; $x -lt $w; $x++) { $c = $bm.GetPixel($x, $y); $t[$x] = $c.R - [math]::Max($c.G, $c.B); if ($t[$x] -gt $mx) { $mx = $t[$x] } }
                $n = 0
                for ($x = 0; $x -lt $w; $x++) { if ($t[$x] -gt 0.06 * $mx -and $t[$x] -lt 0.3 * $mx) { $n++ } }
                $fs += $n
            }
            $bm.Dispose()
            Median $fs
        }
        function PlayRound([double]$walkSec = 0.8) { Invoke-Nova 'play' | Out-Null; Wait-Sec 1.2; Exec 'walk' 'GameObject.Find("Walker").GetComponent<Animator>().Play("Walk"); return 1;' | Out-Null; Wait-Sec $walkSec }

        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        Copy-Item (Join-Path $PSScriptRoot 'motion_probe.cs') (Join-Path $assetDir 'MotionProbe.cs') -Force
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw.Elapsed.TotalSeconds -lt 60 -and (($inf -and $inf.compiling) -or $now -eq $dllBefore))
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 120) { $t = Invoke-Nova 'log -n 400'; if ($t -match 'compiled 32\. InstancedBasic|cache hit 32\. InstancedBasic') { break }; Start-Sleep -Milliseconds 500 }

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,1 --scale 20,1,20' | Out-Null
        Invoke-Nova 'create cube --name StaticBox --position -3,0.5,1' | Out-Null
        Invoke-Nova 'create cube --name Mover --position 0,0.5,0' | Out-Null
        Invoke-Nova 'set Mover --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/MotionTest/Checker.mat\"]}"' | Out-Null
        Invoke-Nova 'add-component Mover MotionMover' | Out-Null
        Invoke-Nova 'set Mover --component MotionMover --values "{\"minX\":-1.5,\"maxX\":0.8}"' | Out-Null   # 걷는 캐릭터 앞을 지나지 않게
        Invoke-Nova 'create cube --name Spinner --position 3,1,1 --scale 1.5,1.5,0.2' | Out-Null
        Invoke-Nova 'add-component Spinner MotionSpinner' | Out-Null
        Invoke-Nova 'set Spinner --component MotionSpinner --values "{\"degreesPerSecond\":540}"' | Out-Null
        Invoke-Nova 'create character --name Walker --position 1.6,0,2.5' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.5,-6 --rotation 8,0,0' | Out-Null
        $cam = Invoke-NovaJson 'get "Main Camera" --component Camera'
        $script:mvFovY = if ($cam -and $cam.fovY) { [double]$cam.fovY } else { [math]::PI / 3 }
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null
        SetSpeed 6

        # 편집 중: 아무것도 움직이지 않는다
        $e0 = MvRect 'edit' @(0, 0, 1, 1)
        $script:mvAspect = if ($e0 -and $e0.mapSize) { [double]$e0.mapSize[0] / [double]$e0.mapSize[1] } else { 2.0 }
        $rMover = Rect @(-2.0, 0.0, -0.5) @(1.3, 1.0, 0.5)
        $rStatic = Rect @(-3.5, 0.0, 0.5) @(-2.5, 1.0, 1.5)
        $rSpin = Rect @(2.2, 0.2, 0.2) @(3.8, 1.8, 1.8)
        $rWalker = Rect @(1.2, 0.0, 2.1) @(2.0, 1.9, 2.9)
        Add-Result motionvectors 'Edit mode (nothing moves): motion vectors are zero, the Volume default is on' ($e0 -and $e0.valid -and $e0.enabled -and [double]$e0.maxPixels -lt 0.05) ("valid {0}, max {1:N3} px" -f $e0.valid, $e0.maxPixels)

        # Play: 물체 · 스킨
        Vol 'On'; SetCam 0
        PlayRound
        $mv = MvRect 'play_mover' $rMover; $st = MvRect 'play_static' $rStatic; $sp = MvRect 'play_spinner' $rSpin; $wk = MvRect 'play_walker' $rWalker
        $info = Invoke-NovaJson 'motionvectors info'
        Invoke-Nova "screenshot `"$(Join-Path $dir 'play.png')`" --view game" | Out-Null
        Invoke-Nova 'stop' | Out-Null
        Add-Result motionvectors 'Play: the box moving right has +x screen velocity, the static box has none' ([double]$mv.meanPixels[0] -gt 0.05 -and [double]$mv.meanPixels[0] -gt 3 * [math]::Abs([double]$mv.meanPixels[1]) -and [double]$mv.maxPixels -gt 0.5 -and [double]$st.maxPixels -lt 0.1) ("mover mean {0:N2},{1:N2} max {2:N2} px; static box max {3:N3} px" -f [double]$mv.meanPixels[0], [double]$mv.meanPixels[1], $mv.maxPixels, $st.maxPixels)
        Add-Result motionvectors 'Play: the spinning plate and the walking character (skinned bones) have motion, only moved renderers are redrawn' ([double]$sp.maxPixels -gt 0.3 -and [double]$wk.maxPixels -gt 0.1 -and [int]$info.objectsDrawn -ge 2 -and [int]$info.skinnedDrawn -ge 1) ("spinner max {0:N2} px, walker max {1:N2} px; redrawn: {2} objects, {3} skinned" -f $sp.maxPixels, $wk.maxPixels, $info.objectsDrawn, $info.skinnedDrawn)

        # Volume: Object Motion 끔 (카메라만) · Skinned Motion 끔 (본 애니메이션만 빠진다)
        Vol 'NoObject'; PlayRound
        $mv2 = MvRect 'noobj_mover' $rMover; $sp2 = MvRect 'noobj_spinner' $rSpin; $i2 = Invoke-NovaJson 'motionvectors info'
        Invoke-Nova 'stop' | Out-Null
        Vol 'NoSkinned'; PlayRound
        $wk3 = MvRect 'noskin_walker' $rWalker; $mv3 = MvRect 'noskin_mover' $rMover
        Invoke-Nova 'stop' | Out-Null
        Add-Result motionvectors 'Volume Object Motion off: camera motion only (the moving box and plate read zero)' ([double]$mv2.maxPixels -lt 0.1 -and [double]$sp2.maxPixels -lt 0.1 -and [int]$i2.objectsDrawn -eq 0) ("mover max {0:N3}, spinner max {1:N3} px, redrawn {2}" -f $mv2.maxPixels, $sp2.maxPixels, $i2.objectsDrawn)
        Add-Result motionvectors 'Volume Skinned Motion off: the walking character reads zero, the moving box still moves' ([double]$wk3.maxPixels -lt 0.1 -and [double]$mv3.maxPixels -gt 0.5) ("walker max {0:N3} px, mover max {1:N2} px" -f $wk3.maxPixels, $mv3.maxPixels)

        # 렌더러의 Motion Vectors = Force No Motion (Unity): 돌아도 0
        Vol 'On'
        Invoke-Nova 'set Spinner --component MeshRenderer --values "{\"motionVectors\":2}"' | Out-Null
        PlayRound
        $sp4 = MvRect 'force_spinner' $rSpin; $i4 = Invoke-NovaJson 'motionvectors info'
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'set Spinner --component MeshRenderer --values "{\"motionVectors\":1}"' | Out-Null
        Add-Result motionvectors 'Mesh Renderer Motion Vectors = Force No Motion: the spinning plate reads zero' ([double]$sp4.maxPixels -lt 0.05 -and [int]$i4.forcedNoMotion -ge 1) ("spinner max {0:N3} px, forced {1}" -f $sp4.maxPixels, $i4.forcedNoMotion)

        # Volume Enable 끔: 만들지 않는다 (TAA · Motion Blur · SSAO 는 카메라만)
        Vol 'Off'; PlayRound 0.3
        $i5 = Invoke-NovaJson 'motionvectors info'
        Invoke-Nova 'stop' | Out-Null
        Add-Result motionvectors 'Volume Enable off: no motion vectors are made' ($i5 -and -not $i5.enabled -and -not $i5.valid) ("enabled {0}, valid {1}" -f $i5.enabled, $i5.valid)

        # 카메라가 오른쪽으로 → 멈춘 상자가 화면에서 왼쪽으로
        Vol 'On'
        Invoke-Nova 'add-component "Main Camera" CameraMover' | Out-Null
        Invoke-Nova 'set "Main Camera" --component CameraMover --values "{\"speed\":3}"' | Out-Null
        SetSpeed 0   # 움직이는 상자가 영역에 들어와 섞이지 않게
        Invoke-Nova 'play' | Out-Null; Wait-Sec 0.6
        $st6 = MvRect 'camera_ground' @(0.0, 0.78, 1.0, 1.0)   # 아래쪽 바닥 (늘 보인다, 멈춰 있다)
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'remove-component "Main Camera" CameraMover' | Out-Null
        SetSpeed 6
        Add-Result motionvectors 'Camera motion: moving the camera right makes the static ground flow left' ([double]$st6.meanPixels[0] -lt -0.05 -and [math]::Abs([double]$st6.meanPixels[0]) -gt 2 * [math]::Abs([double]$st6.meanPixels[1])) ("ground mean {0:N3},{1:N3} px" -f [double]$st6.meanPixels[0], [double]$st6.meanPixels[1])

        # TAA: 움직이는 체커 상자 — 모션 벡터가 있으면 히스토리가 물체를 따라와 무늬가 덜 뭉개진다 (TAA 없는 그림이 기준)
        $taa = @{}
        foreach ($cfg in @(@('ref', 'On', 0), @('mv', 'On', 3), @('nomv', 'Off', 3)))
        {
            Vol $cfg[1]; SetCam $cfg[2]
            Invoke-Nova 'play' | Out-Null; Wait-Sec 1.0
            $cs = @()
            for ($k = 0; $k -lt 5; $k++) { $png = Join-Path $dir "taa_$($cfg[0])_$k.png"; Invoke-Nova "screenshot `"$png`" --view game" | Out-Null; $st = ShotStats $png; if ($st) { $cs += $st.Contrast }; Wait-Sec 0.11 }
            Invoke-Nova 'stop' | Out-Null
            $taa[$cfg[0]] = Median $cs
        }
        SetCam 0
        Add-Result motionvectors 'TAA: with motion vectors the moving checker box keeps more of its pattern (history follows the object)' ($taa['mv'] -gt $taa['nomv'] + 0.8 -and $taa['mv'] -le $taa['ref'] + 0.5) ("checker contrast: no TAA {0:N1}, TAA + motion vectors {1:N1}, TAA camera only {2:N1}" -f $taa['ref'], $taa['mv'], $taa['nomv'])

        # Motion Blur Mode: Camera Only (카메라가 멈춰 있으니 흐림 없음) · Camera And Objects (움직이는 상자가 흐려진다) — 빠르게 (40 m/s)
        SetSpeed 40
        # 흐림의 가장자리를 재기 쉽게 상자를 한 가지 빨강으로 (체커 무늬 대신)
        Invoke-Nova 'set Mover --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/MotionTest/Red.mat\"]}"' | Out-Null
        $mb = @{}; $mbw = @{}
        foreach ($m in @('MbCam', 'MbObj'))
        {
            Vol $m
            Invoke-Nova 'play' | Out-Null
            # 흐림 폭 = 한 프레임에 움직인 거리 — 엔진 속도와 상관없게 60 fps 로 (Application.targetFrameRate, Stop 하면 풀린다)
            Exec 'mb_fps' 'Application.targetFrameRate = 60; return Application.targetFrameRate.ToString();' | Out-Null
            Wait-Sec 1.0
            $bl = @(); $wd = @()
            for ($k = 0; $k -lt 6; $k++) { $png = Join-Path $dir "mb_$($m)_$k.png"; Invoke-Nova "screenshot `"$png`" --view game" | Out-Null; $bl += (EdgeBlur $png); $wd += (Fringe $png); Wait-Sec 0.11 }
            Invoke-Nova 'stop' | Out-Null
            $mb[$m] = Median $bl
            $mbw[$m] = Median $wd
        }
        SetSpeed 6
        Vol 'On'
        Add-Result motionvectors 'Motion Blur Mode Camera And Objects blurs the moving box, Camera Only does not (static camera)' ($mb['MbObj'] -gt $mb['MbCam'] + 2) ("half-mixed edge pixels per row: Camera Only {0:N1}, Camera And Objects {1:N1}" -f $mb['MbCam'], $mb['MbObj'])
        # 타일 최대 속도: 멈춘 배경 픽셀도 이웃 타일의 빠른 물체를 모아 → 상자 바깥으로 옅은 빨강이 번진다
        Add-Result motionvectors 'Motion Blur tile max velocity: the moving box smears out over the static background (faint fringe outside its outline)' ($mbw['MbObj'] -ge 6 -and $mbw['MbObj'] -ge $mbw['MbCam'] + 5) ("faint fringe pixels per row: sharp (Camera Only) {0}, Camera And Objects {1}" -f $mbw['MbCam'], $mbw['MbObj'])
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }

    # OpenGL: 같은 장면에서 움직이는 상자의 속도
    $edGl = Start-TestEditor -OpenGL
    try
    {
        function Wait-Sec([double]$s) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Invoke-Nova 'wait 10' | Out-Null } }
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 120) { $t = Invoke-Nova 'log -n 400'; if ($t -match 'compiled 32\. InstancedBasic|cache hit 32\. InstancedBasic') { break }; Start-Sleep -Milliseconds 500 }
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,1 --scale 20,1,20' | Out-Null
        Invoke-Nova 'create cube --name Mover --position 0,0.5,0' | Out-Null
        Invoke-Nova 'add-component Mover MotionMover' | Out-Null
        Invoke-Nova 'set Mover --component MotionMover --values "{\"speed\":6}"' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.5,-6 --rotation 8,0,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'play' | Out-Null; Wait-Sec 1.0
        $g = Invoke-NovaJson ('motionvectors map "' + (Join-Path $dir 'gl.png') + '" --rect 0.3,0.4,0.7,0.75')
        Invoke-Nova 'stop' | Out-Null
        Add-Result motionvectors 'OpenGL: motion vectors are made, the moving box has +x velocity' ($g -and $g.valid -and [int]$g.objectsDrawn -ge 1 -and [double]$g.meanPixels[0] -gt 0.02) ("valid {0}, redrawn {1}, mean x {2:N3} px" -f $g.valid, $g.objectsDrawn, $(if ($g) { [double]$g.meanPixels[0] } else { 0 }))
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $edGl)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Cinemachine
{
    # Cinemachine (패키지 com.nova.cameras): 모두 CLI `nova cinemachine info | create | priority | axis | impulse | blend …` 로 읽고 바꾼다.
    #  편집 중: GameObject > Cinemachine 이 Main Camera 에 Brain 을 붙이고, Brain 이 Live 가상 카메라의 모습을 바로 보여 준다 (Follow 오프셋),
    #  같은 Priority 면 가장 늦게 켜진 것, Prioritize, Priority 가 높은 것, Orbital Follow (세 고리 · 구) · Third Person Follow 의 자리 (손으로 계산한 값),
    #  씬 저장 · 열기. Play: Priority 를 올리면 Ease In Out 2 초로 섞기 (중간 · 끝), 흔들림 (Perlin — 출력만, 가상 카메라 Transform 은 그대로),
    #  섞는 중에 바뀌면 지금 화면에서 다시 (Mid-Blend, 튀지 않음), Custom Blend (Cut), 충격 (Impulse Source → Listener), 벽 앞으로 당기기 (Third Person),
    #  Follow 따라가기 (늦게), C# API (Priority · Lens · 축 · ActiveVirtualCamera · GenerateImpulse), 끄면 다음 카메라로
    #  Play 중에는 CLI set 이 막혀 있어 Play 에서 바꾸는 것은 cinemachine 명령 · C# 로
    Write-Host '[cinemachine]'
    $dir = Join-Path $Out 'cinemachine'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\CinemachineTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    $ed = Start-TestEditor
    try
    {
        $ic = [Globalization.CultureInfo]::InvariantCulture
        function Wait-Sec([double]$s) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Invoke-Nova 'wait 5' | Out-Null } }
        function Exec([string]$name, [string]$code) { $f = Join-Path $dir "$name.cs"; $code | Set-Content -Encoding utf8 $f; $r = Invoke-NovaJson "exec --file `"$f`""; if ($r) { "$($r.result)" } else { '' } }
        function CmInfo { Invoke-NovaJson 'cinemachine info' }
        function Cam($info, [string]$name) { $info.cameras | Where-Object { $_.name -eq $name } | Select-Object -First 1 }
        function Near($a, [double[]]$b, [double]$tol) { if ($null -eq $a) { return $false }; for ($i = 0; $i -lt $b.Count; $i++) { if ([math]::Abs([double]$a[$i] - $b[$i]) -gt $tol) { return $false } }; $true }
        function Fmt($v) { if ($null -eq $v) { return 'null' }; '(' + (($v | ForEach-Object { ([double]$_).ToString('F2', $ic) }) -join ', ') + ')' }
        function Dist($a, $b) { [math]::Sqrt(([double]$a[0] - $b[0]) * ([double]$a[0] - $b[0]) + ([double]$a[1] - $b[1]) * ([double]$a[1] - $b[1]) + ([double]$a[2] - $b[2]) * ([double]$a[2] - $b[2])) }
        function SetCm([string]$obj, [string]$type, [string]$json) { Invoke-Nova ('set "' + $obj + '" --component ' + $type + ' --values "' + ($json -replace '"', '\"') + '"') | Out-Null }

        # 패키지 + 검사 스크립트 (MotionMover)
        Invoke-Nova 'package add com.nova.cameras' | Out-Null
        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        Copy-Item (Join-Path $PSScriptRoot 'motion_probe.cs') (Join-Path $assetDir 'MotionProbe.cs') -Force
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw.Elapsed.TotalSeconds -lt 60 -and (($inf -and $inf.compiling) -or $now -eq $dllBefore))

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Target --position 0,0.5,0' | Out-Null
        Invoke-Nova 'cinemachine create --kind follow --target Target --name CamA' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $i = CmInfo
        $b = $i.brains | Select-Object -First 1
        Add-Result cinemachine 'GameObject > Cinemachine > Follow Camera adds a Cinemachine Brain to Main Camera' ($i.brains.Count -eq 1 -and $b.camera -eq 'Main Camera' -and (Cam $i 'CamA').body -eq 'CinemachineFollow' -and (Cam $i 'CamA').aim -eq 'CinemachineRotationComposer') "brains=$($i.brains.Count) camera=$($b.camera) body=$((Cam $i 'CamA').body) aim=$((Cam $i 'CamA').aim)"
        Add-Result cinemachine 'edit mode: Main Camera shows the live camera (target + Follow Offset 0,0,-10)' ($b.live -eq 'CamA' -and (Near $b.transform.position @(0, 0.5, -10) 0.01)) "live=$($b.live) main=$(Fmt $b.transform.position)"

        Invoke-Nova 'cinemachine create --kind camera --name CamB' | Out-Null
        Invoke-Nova 'set CamB --position 10,5,0 --rotation 0,-90,0' | Out-Null
        SetCm 'CamB' 'CinemachineCamera' '{"fieldOfView":30}'
        Invoke-Nova 'wait 3' | Out-Null
        $b = (CmInfo).brains[0]
        Add-Result cinemachine 'same priority: the newest camera is live (pose + lens to Main Camera)' ($b.live -eq 'CamB' -and (Near $b.transform.position @(10, 5, 0) 0.01) -and [math]::Abs($b.cameraFov - 30) -lt 0.1) "live=$($b.live) main=$(Fmt $b.transform.position) fov=$($b.cameraFov)"
        Invoke-Nova 'cinemachine prioritize CamA' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $l1 = (CmInfo).brains[0].live
        SetCm 'CamB' 'CinemachineCamera' '{"priority":10}'
        Invoke-Nova 'wait 3' | Out-Null
        $l2 = (CmInfo).brains[0].live
        Add-Result cinemachine 'Prioritize makes CamA live, a higher Priority wins' ($l1 -eq 'CamA' -and $l2 -eq 'CamB') "after prioritize=$l1, CamB priority 10=$l2"

        # Orbital Follow (FreeLook: 세 고리, World Space) — 가로 90 도 = 대상의 -x 쪽
        Invoke-Nova 'cinemachine create --kind freelook --target Target --name Free' | Out-Null
        SetCm 'Free' 'CinemachineCamera' '{"priority":-10}'
        Invoke-Nova 'cinemachine axis Free --horizontal 90' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $f1 = Cam (CmInfo) 'Free'
        Invoke-Nova 'cinemachine axis Free --vertical 45' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $f2 = Cam (CmInfo) 'Free'
        Add-Result cinemachine 'Orbital Follow Three Ring: center ring (-4, 2.75, 0), top ring (-2, 5.5, 0), Rotation Composer looks at the target' ((Near $f1.rawPosition @(-4, 2.75, 0) 0.02) -and (Near $f2.rawPosition @(-2, 5.5, 0) 0.02) -and (Near $f1.rawRotation @(29.36, 90, 0) 0.3)) "center=$(Fmt $f1.rawPosition) rot=$(Fmt $f1.rawRotation) top=$(Fmt $f2.rawPosition)"
        SetCm 'Free' 'CinemachineOrbitalFollow' '{"orbitStyle":0}'
        Invoke-Nova 'wait 3' | Out-Null
        $f3 = Cam (CmInfo) 'Free'
        Add-Result cinemachine 'Orbital Follow Sphere: radius 10, 45 deg up -> (-7.07, 7.57, 0), pitch 45' ((Near $f3.rawPosition @(-7.071, 7.571, 0) 0.02) -and (Near $f3.rawRotation @(45, 90, 0) 0.3)) "pos=$(Fmt $f3.rawPosition) rot=$(Fmt $f3.rawRotation)"

        # Third Person Follow: 어깨 (0.5,-0.4) + 팔 0.4 + 거리 2 — 대상이 90 도 돌면 같이
        Invoke-Nova 'cinemachine create --kind thirdperson --target Target --name Tps' | Out-Null
        SetCm 'Tps' 'CinemachineCamera' '{"priority":-10}'
        Invoke-Nova 'wait 3' | Out-Null
        $t1 = Cam (CmInfo) 'Tps'
        Invoke-Nova 'set Target --rotation 0,90,0' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $t2 = Cam (CmInfo) 'Tps'
        Invoke-Nova 'set Target --rotation 0,0,0' | Out-Null
        Add-Result cinemachine 'Third Person Follow: shoulder + arm + distance (0.5, 0.5, -2), target turned 90 deg -> (-2, 0.5, -0.5)' ((Near $t1.rawPosition @(0.5, 0.5, -2) 0.02) -and (Near $t2.rawPosition @(-2, 0.5, -0.5) 0.02) -and (Near $t2.rawRotation @(0, 90, 0) 0.3)) "yaw 0=$(Fmt $t1.rawPosition) yaw 90=$(Fmt $t2.rawPosition) rot=$(Fmt $t2.rawRotation)"

        # 씬 저장 · 열기
        Invoke-Nova 'scene save --as Assets/CinemachineTest/Cm.scene' | Out-Null
        Invoke-Nova 'scene open Assets/CinemachineTest/Cm.scene --force' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $i = CmInfo
        $names = ($i.cameras | ForEach-Object { "$($_.name):$($_.priority):$($_.body)" } | Sort-Object) -join ' '
        Add-Result cinemachine 'scene save + open keeps the Brain, cameras, priorities and pipeline' ($i.brains.Count -eq 1 -and $i.cameras.Count -eq 4 -and $i.brains[0].live -eq 'CamB' -and $names -match 'Free:-10:CinemachineOrbitalFollow' -and $names -match 'Tps:-10:CinemachineThirdPersonFollow') "live=$($i.brains[0].live) $names"

        # Play 준비 (편집 중에만 set 이 된다): 우선순위, CamB 흔들림, CamA 충격 듣기, 충격 상자, Third Person 앞의 벽, 움직이는 Runner + 따라가는 CamRun
        SetCm 'CamA' 'CinemachineCamera' '{"priority":10}'
        SetCm 'CamB' 'CinemachineCamera' '{"priority":0}'
        Invoke-Nova 'add-component CamB CinemachineBasicMultiChannelPerlin' | Out-Null
        SetCm 'CamB' 'CinemachineBasicMultiChannelPerlin' '{"noiseProfile":1}'
        Invoke-Nova 'add-component CamA CinemachineImpulseListener' | Out-Null
        Invoke-Nova 'create cube --name Boom --position 3,0.5,3' | Out-Null
        Invoke-Nova 'add-component Boom CinemachineImpulseSource' | Out-Null
        SetCm 'Boom' 'CinemachineImpulseSource' '{"impulseDuration":1.0}'
        Invoke-Nova 'create cube --name Wall --position 0.5,0.5,-1.2 --scale 3,3,0.2' | Out-Null
        Invoke-Nova 'create cube --name Runner --position 0,0.5,6' | Out-Null
        Invoke-Nova 'add-component Runner MotionMover --values "{\"speed\":3,\"minX\":-40,\"maxX\":40}"' | Out-Null
        Invoke-Nova 'cinemachine create --kind follow --target Runner --name CamRun' | Out-Null
        SetCm 'CamRun' 'CinemachineCamera' '{"priority":-10}'
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null

        Invoke-Nova 'play' | Out-Null
        Wait-Sec 0.8
        $i = CmInfo
        $b = $i.brains[0]
        Add-Result cinemachine 'Play: the highest priority camera is live (CamA 10)' ($b.live -eq 'CamA' -and -not $b.blending -and (Near $b.transform.position @(0, 0.5, -10) 0.05)) "live=$($b.live) blending=$($b.blending) main=$(Fmt $b.transform.position)"

        # Ease In Out 2 초 섞기
        Invoke-Nova 'cinemachine priority CamB --value 20' | Out-Null
        Invoke-Nova 'wait 2' | Out-Null
        $b0 = (CmInfo).brains[0]
        Wait-Sec 0.9
        $bm = (CmInfo).brains[0]
        Add-Result cinemachine 'raising CamB priority starts an Ease In Out 2 s blend; halfway Main Camera is between the two (pose + FOV)' ($b0.blending -and $b0.blend.from -eq 'CamA' -and $b0.blend.to -eq 'CamB' -and $b0.blend.style -eq 'Ease In Out' -and [math]::Abs($b0.blend.duration - 2) -lt 0.01 -and $bm.blending -and $bm.transform.position[0] -gt 1 -and $bm.transform.position[0] -lt 9 -and $bm.cameraFov -gt 32 -and $bm.cameraFov -lt 58) ("start {0} -> {1} {2} {3}s, at t={4:N2}: main x={5:N2} fov={6:N1}" -f $b0.blend.from, $b0.blend.to, $b0.blend.style, $b0.blend.duration, $bm.blend.t, $bm.transform.position[0], $bm.cameraFov)
        Wait-Sec 1.6
        $i = CmInfo
        $b = $i.brains[0]; $cb = Cam $i 'CamB'
        Add-Result cinemachine 'after the blend Main Camera is exactly the live camera output (CamB, FOV 30)' (-not $b.blending -and $b.live -eq 'CamB' -and (Dist $b.transform.position $cb.position) -lt 0.01 -and [math]::Abs($b.cameraFov - 30) -lt 0.1) "main=$(Fmt $b.transform.position) CamB=$(Fmt $cb.position) fov=$($b.cameraFov)"
        Wait-Sec 0.3
        $cb2 = Cam (CmInfo) 'CamB'
        $shake = [math]::Sqrt(($cb.positionCorrection | ForEach-Object { [double]$_ * $_ } | Measure-Object -Sum).Sum)
        Add-Result cinemachine 'Perlin noise (6D Shake) moves only the output: correction changes, the virtual camera Transform stays at (10, 5, 0)' ($shake -gt 0.005 -and (Dist $cb.positionCorrection $cb2.positionCorrection) -gt 0.001 -and (Near $cb2.rawPosition @(10, 5, 0) 0.001) -and (Dist $cb2.rotation $cb2.rawRotation) -gt 0.05) ("correction {0} -> {1}, raw {2}, rotation {3} vs raw {4}" -f (Fmt $cb.positionCorrection), (Fmt $cb2.positionCorrection), (Fmt $cb2.rawPosition), (Fmt $cb2.rotation), (Fmt $cb2.rawRotation))

        # 섞는 중에 바뀌면 지금 화면에서 (Linear 2 초)
        Invoke-Nova 'cinemachine blend --style Linear --time 2' | Out-Null
        Invoke-Nova 'cinemachine priority CamB --value 0' | Out-Null
        Wait-Sec 0.7
        $p1 = (CmInfo).brains[0].transform.position
        Invoke-Nova 'cinemachine priority CamB --value 30' | Out-Null
        Invoke-Nova 'wait 1' | Out-Null
        $bi = (CmInfo).brains[0]
        $jump = Dist $p1 $bi.transform.position
        Add-Result cinemachine 'a change during a blend blends again from the current view (Mid-Blend, no jump)' ($bi.blending -and $bi.blend.from -eq 'Mid-Blend' -and $bi.blend.to -eq 'CamB' -and $bi.blend.style -eq 'Linear' -and $jump -lt 1.5) ("from={0} to={1} {2}, moved {3:N2} m between the two reads" -f $bi.blend.from, $bi.blend.to, $bi.blend.style, $jump)
        Wait-Sec 2.2

        # Custom Blend: CamB → CamA = Cut
        Invoke-Nova 'cinemachine blend --from CamB --to CamA --style Cut' | Out-Null
        Invoke-Nova 'cinemachine priority CamB --value 0' | Out-Null
        Invoke-Nova 'wait 2' | Out-Null
        $b = (CmInfo).brains[0]
        Add-Result cinemachine 'Custom Blend CamB -> CamA = Cut: switches at once' ($b.live -eq 'CamA' -and -not $b.blending -and (Near $b.transform.position @(0, 0.5, -10) 0.05)) "live=$($b.live) blending=$($b.blending) main=$(Fmt $b.transform.position)"

        # 충격: Boom (Bump, 1 초, 기본 속도 0,-1,0) × 2 → CamA (Listener, 카메라 공간) 가 아래로
        Invoke-Nova 'cinemachine impulse Boom --force 2' | Out-Null
        Wait-Sec 0.35
        $ca = Cam (CmInfo) 'CamA'
        Wait-Sec 1.3
        $i = CmInfo; $ca2 = Cam $i 'CamA'
        Add-Result cinemachine 'Impulse Source -> Impulse Listener: the live camera dips down, then settles' ($ca.positionCorrection[1] -lt -0.3 -and [math]::Abs($ca2.positionCorrection[1]) -lt 0.01 -and $i.impulses -eq 0) ("correction y {0:N2} at 0.35 s, {1:N3} after, active impulses {2}" -f $ca.positionCorrection[1], $ca2.positionCorrection[1], $i.impulses)

        # Third Person: 벽 (가까운 면 z = -1.1) 앞으로 당긴다 (편집 중 -2)
        $t = Cam (CmInfo) 'Tps'
        Add-Result cinemachine 'Third Person Follow Avoid Obstacles pulls the camera in front of the wall' ($t.rawPosition[2] -gt -1.1 -and $t.rawPosition[2] -lt -0.5) "z=$(([double]$t.rawPosition[2]).ToString('F2', $ic)) (no wall: -2, wall face -1.1, radius 0.2)"

        # Follow 따라가기 (Position Damping 1 초): 3 m/s 로 가는 Runner 보다 늦다 (정상 상태 약 3 / 4.6 = 0.65 m)
        $lags = @()
        for ($k = 0; $k -lt 3; $k++)
        {
            Wait-Sec 0.3
            $r = Invoke-NovaJson 'get Runner'
            $cr = Cam (CmInfo) 'CamRun'
            $lags += [double]$r.position[0] - [double]$cr.rawPosition[0]
        }
        $lag = ($lags | Measure-Object -Average).Average
        Add-Result cinemachine 'Follow Position Damping: the camera trails a moving target' ($lag -gt 0.3 -and $lag -lt 1.2) ("lag x {0} m" -f (($lags | ForEach-Object { $_.ToString('F2', $ic) }) -join ', '))

        # C# API
        $cs = Exec 'api' 'var cm = GameObject.Find("CamA").GetComponent<NovaEngine.Cinemachine.CinemachineCamera>(); cm.Priority = 50; cm.Lens.FieldOfView = 42f; var o = GameObject.Find("Free").GetComponent<NovaEngine.Cinemachine.CinemachineOrbitalFollow>(); o.HorizontalAxis.Value = 45f; o.HorizontalAxis.Value += 10f; return (int)cm.Priority + " " + cm.Lens.FieldOfView + " " + o.HorizontalAxis.Value + " " + cm.Follow.name;'
        Wait-Sec 0.3
        $cs2 = Exec 'api2' 'var b = Camera.main.GetComponent<NovaEngine.Cinemachine.CinemachineBrain>(); GameObject.Find("Boom").GetComponent<NovaEngine.Cinemachine.CinemachineImpulseSource>().GenerateImpulse(1f); return b.ActiveVirtualCamera.name + " " + b.ActiveVirtualCamera.IsLive + " " + b.DefaultBlend.Style;'
        $imp = (CmInfo).impulses
        Add-Result cinemachine 'C# API: Priority, Lens.FieldOfView, HorizontalAxis.Value, Follow, ActiveVirtualCamera, GenerateImpulse' ($cs -eq '50 42 55 Target' -and $cs2 -eq 'CamA True Linear' -and $imp -ge 1) "api='$cs' brain='$cs2' impulses=$imp"

        # 끄면 다음 Priority 로
        Invoke-Nova 'cinemachine enable CamA --value false' | Out-Null
        Invoke-Nova 'wait 2' | Out-Null
        $b = (CmInfo).brains[0]
        Add-Result cinemachine 'disabling the live camera hands over to the next priority' ($b.live -ne 'CamA' -and $b.live -ne '') "live=$($b.live) blending=$($b.blending)"
        Invoke-Nova ('screenshot "' + (Join-Path $dir 'game.png') + '" --view game') | Out-Null
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
}

function Suite-RenderingDebug
{
    # Rendering Debugger (Window > Analysis > Rendering Debugger · Scene 뷰 툴바 Debug · CLI nova debugview): Scene · Game 뷰를 바꿔 본다.
    #  화면 비율 영역의 평균 색으로 본다 — 카메라 (0, 3, -7) 가 상자 (0, 0.5, 0) 를 본다: 위 = 하늘, 아래 = 가까운 바닥, 가운데 = 상자 앞면
    #  Depth (하늘 흰색 · 가까울수록 어둡다 · 회색 · Range), Normals (바닥 = 위 = 연두, 상자 앞 = -z = 올리브, 하늘 검정), AO (열린 바닥 흰색 · 상자 밑이 더 어둡다),
    #  Motion Vectors (편집 중 검정, Play 에서 오른쪽으로 가는 상자만 빨강 — Scene · Game 뷰), APV (프로브 빛만 · 섞은 방법 색), None 이면 원래 그림
    Write-Host '[renderingdebug]'
    $dir = Join-Path $Out 'renderingdebug'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\RenderingDebugTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $ed = Start-TestEditor
    try
    {
        function Wait-Sec([double]$s) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Invoke-Nova 'wait 5' | Out-Null } }
        # 화면 비율 영역 (x0, y0, x1, y1) 의 평균 R, G, B
        function Region([string]$png, [double]$x0, [double]$y0, [double]$x1, [double]$y1)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($png)
            $w = $bm.Width; $h = $bm.Height; $n = 0; $r = 0.0; $g = 0.0; $b = 0.0
            for ($y = [int]($y0 * $h); $y -lt [int]($y1 * $h); $y += 2) { for ($x = [int]($x0 * $w); $x -lt [int]($x1 * $w); $x += 2) { $c = $bm.GetPixel($x, $y); $r += $c.R; $g += $c.G; $b += $c.B; $n++ } }
            $bm.Dispose()
            $n = [math]::Max(1, $n)
            [pscustomobject]@{ R = $r / $n; G = $g / $n; B = $b / $n; L = ($r + $g + $b) / (3 * $n) }
        }
        function Fmt($c) { '({0:N0}, {1:N0}, {2:N0})' -f $c.R, $c.G, $c.B }
        function Near($c, [double]$r, [double]$g, [double]$b, [double]$tol) { [math]::Abs($c.R - $r) -le $tol -and [math]::Abs($c.G - $g) -le $tol -and [math]::Abs($c.B - $b) -le $tol }
        function Shot([string]$name, [string]$view = 'scene') { $f = Join-Path $dir "$name.png"; Invoke-Nova ('screenshot "' + $f + '" --view ' + $view) | Out-Null; $f }
        # 두 그림의 평균 차 (0..255)
        function ImageDiff([string]$a, [string]$b)
        {
            $p = [System.Drawing.Bitmap]::FromFile($a); $q = [System.Drawing.Bitmap]::FromFile($b)
            $s = 0.0; $n = 0
            for ($y = 0; $y -lt [math]::Min($p.Height, $q.Height); $y += 4) { for ($x = 0; $x -lt [math]::Min($p.Width, $q.Width); $x += 4) {
                $c = $p.GetPixel($x, $y); $d = $q.GetPixel($x, $y); $s += ([math]::Abs($c.R - $d.R) + [math]::Abs($c.G - $d.G) + [math]::Abs($c.B - $d.B)) / 3.0; $n++ } }
            $p.Dispose(); $q.Dispose()
            $s / [math]::Max(1, $n)
        }
        # 진하게 색이 든 픽셀 비율 (가장 큰 채널 − 가장 작은 채널 > 80)
        function Colorful([string]$png)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($png); $n = 0; $k = 0
            for ($y = 0; $y -lt $bm.Height; $y += 4) { for ($x = 0; $x -lt $bm.Width; $x += 4) { $c = $bm.GetPixel($x, $y); $n++; if (([math]::Max($c.R, [math]::Max($c.G, $c.B)) - [math]::Min($c.R, [math]::Min($c.G, $c.B))) -gt 80) { $k++ } } }
            $bm.Dispose(); $k / [math]::Max(1, $n)
        }

        $gameDll = Join-Path $Project 'Library\ScriptAssemblies\Assembly-CSharp.dll'
        $dllBefore = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc
        Copy-Item (Join-Path $PSScriptRoot 'motion_probe.cs') (Join-Path $assetDir 'MotionProbe.cs') -Force
        $sw = [Diagnostics.Stopwatch]::StartNew()
        do { Invoke-Nova 'wait 20' | Out-Null; $inf = Invoke-NovaJson 'info'; $now = (Get-Item $gameDll -ErrorAction SilentlyContinue).LastWriteTimeUtc }
        while ($sw.Elapsed.TotalSeconds -lt 60 -and (($inf -and $inf.compiling) -or $now -eq $dllBefore))

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create plane --name Ground --scale 3,1,3' | Out-Null
        Invoke-Nova 'create cube --name Box --position 0,0.5,0' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,3,-7 --rotation 19.65,0,0' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'camera --position 0,3,-7 --target 0,0.5,0' | Out-Null
        Invoke-Nova 'debugview none' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null
        $sky = @(0.3, 0.03, 0.7, 0.1); $nearG = @(0.3, 0.88, 0.7, 0.97); $farG = @(0.1, 0.36, 0.3, 0.42); $boxF = @(0.485, 0.49, 0.515, 0.54); $open = @(0.3, 0.6, 0.4, 0.66); $base = @(0.46, 0.555, 0.54, 0.575)
        function RegionOf([string]$png, [double[]]$r) { Region $png $r[0] $r[1] $r[2] $r[3] }

        $none = Shot 'scene_none'
        $i = Invoke-NovaJson 'debugview depth'
        Invoke-Nova 'wait 3' | Out-Null
        $f = Shot 'scene_depth'
        $s = RegionOf $f $sky; $n = RegionOf $f $nearG; $fa = RegionOf $f $farG
        Add-Result renderingdebug 'Depth: sky white, gray, near ground darker than far ground' ($i.mode -eq 'depth' -and $s.L -gt 250 -and $n.L -lt $fa.L - 15 -and [math]::Abs($n.R - $n.B) -lt 2) ("sky {0:N0}, near {1}, far {2}" -f $s.L, (Fmt $n), (Fmt $fa))
        Invoke-Nova 'debugview depth --range 5' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $fa5 = RegionOf (Shot 'scene_depth5') $farG
        Invoke-Nova 'debugview depth --range 50' | Out-Null
        Add-Result renderingdebug 'Depth Range 5 m: the far ground turns white' ($fa5.L -gt 250) ("far ground {0:N0} (range 50: {1:N0})" -f $fa5.L, $fa.L)

        Invoke-Nova 'debugview normals' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $f = Shot 'scene_normals'
        $g = RegionOf $f $nearG; $bx = RegionOf $f $boxF; $s = RegionOf $f $sky
        Add-Result renderingdebug 'Normals (world): ground up = (128, 255, 128), box front -z = (128, 128, 0), sky black' ((Near $g 128 255 128 12) -and (Near $bx 128 128 0 15) -and $s.L -lt 3) ("ground {0}, box {1}, sky {2:N0}" -f (Fmt $g), (Fmt $bx), $s.L)

        Invoke-Nova 'debugview ao' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $f = Shot 'scene_ao'
        $o = RegionOf $f $open; $bs = RegionOf $f $base
        Add-Result renderingdebug 'Ambient Occlusion: open ground white, darker at the foot of the box (gray map)' ($o.L -gt 230 -and $bs.L -lt $o.L - 8 -and [math]::Abs($o.R - $o.G) -lt 2) ("open {0:N0}, box foot {1:N0}" -f $o.L, $bs.L)

        Invoke-Nova 'debugview motion' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $f = Shot 'scene_motion_edit'
        $all = Region $f 0 0 1 1
        $info = Invoke-NovaJson 'debugview info'
        Add-Result renderingdebug 'Motion Vectors in the Scene view (edit mode, nothing moves): black, Scene view motion vectors rendered' ($all.L -lt 2 -and $info.sceneMotionVectors.valid) ("mean {0:N1}, scene motion valid={1}" -f $all.L, $info.sceneMotionVectors.valid)

        Invoke-Nova 'debugview none' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $back = Shot 'scene_none2'
        $d = ImageDiff $none $back
        Add-Result renderingdebug 'None: the Scene view is the normal picture again' ($d -lt 3) ("mean difference {0:N2}" -f $d)

        # Play: 오른쪽으로 가는 상자 → Scene · Game 뷰 모두 빨강 (오른쪽 = 색상환 0)
        Invoke-Nova 'add-component Box MotionMover --values "{\"speed\":4,\"minX\":-40,\"maxX\":40}"' | Out-Null
        Invoke-Nova 'debugview motion --scale 2' | Out-Null
        Invoke-Nova 'play' | Out-Null
        Wait-Sec 0.6
        $f = Shot 'scene_motion_play'
        $red = Colorful $f
        $bm = [System.Drawing.Bitmap]::FromFile($f); $rr = 0; $k = 0
        for ($y = 0; $y -lt $bm.Height; $y += 3) { for ($x = 0; $x -lt $bm.Width; $x += 3) { $c = $bm.GetPixel($x, $y); if ($c.R -gt 60 -and $c.G -lt $c.R * 0.5 -and $c.B -lt $c.R * 0.5) { $rr++ }; if ($c.R + $c.G + $c.B -gt 30) { $k++ } } }
        $bm.Dispose()
        Add-Result renderingdebug 'Motion Vectors Play, Scene view: only the box moving right shows, in red' ($rr -gt 20 -and $rr -ge $k * 0.8) ("red pixels {0}, lit pixels {1}" -f $rr, $k)
        Invoke-Nova 'window game' | Out-Null
        Wait-Sec 0.4
        $f = Shot 'game_motion_play' 'game'
        $bm = [System.Drawing.Bitmap]::FromFile($f); $rr = 0; $k = 0
        for ($y = 0; $y -lt $bm.Height; $y += 3) { for ($x = 0; $x -lt $bm.Width; $x += 3) { $c = $bm.GetPixel($x, $y); if ($c.R -gt 60 -and $c.G -lt $c.R * 0.5 -and $c.B -lt $c.R * 0.5) { $rr++ }; if ($c.R + $c.G + $c.B -gt 30) { $k++ } } }
        $bm.Dispose()
        $info = Invoke-NovaJson 'debugview info'
        Add-Result renderingdebug 'Motion Vectors Play, Game view: the moving box in red, the rest black' ($rr -gt 20 -and $rr -ge $k * 0.8 -and $info.drawnGame -eq 'motion') ("red pixels {0}, lit pixels {1}, drawn game={2}" -f $rr, $k, $info.drawnGame)
        Invoke-Nova 'debugview normals' | Out-Null
        Wait-Sec 0.2
        $f = Shot 'game_normals' 'game'
        $g = RegionOf $f $nearG
        Add-Result renderingdebug 'Normals in the Game view (Main Camera): ground up = (128, 255, 128)' (Near $g 128 255 128 12) ("ground {0}" -f (Fmt $g))
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null

        # APV: 프로브 빛만 · 섞은 방법 (물체 셰이더의 진단 보기 — ProbeVolumes)
        Invoke-Nova 'debugview none' | Out-Null
        Invoke-Nova 'create empty --name APV --position 0,1,0' | Out-Null
        Invoke-Nova 'add-component APV AdaptiveProbeVolume' | Out-Null
        Invoke-Nova 'camera --position 0,3,-7 --target 0,0.5,0' | Out-Null
        Invoke-Nova 'wait 300' | Out-Null
        $plain = Shot 'scene_apv_none'
        $i1 = Invoke-NovaJson 'debugview apv'
        Invoke-Nova 'wait 3' | Out-Null
        $lit = Shot 'scene_apv'
        $i2 = Invoke-NovaJson 'debugview apv-sampling'
        Invoke-Nova 'wait 3' | Out-Null
        $smp = Shot 'scene_apv_sampling'
        $pv = Invoke-NovaJson 'probevolume debug'
        Invoke-Nova 'debugview none' | Out-Null
        $pv0 = Invoke-NovaJson 'probevolume debug'
        $dl = ImageDiff $plain $lit; $cs = Colorful $smp
        Add-Result renderingdebug 'APV Lighting / Sampling: the objects show probe light only, then the blend-method colors; None turns the APV view off' ($i1.probeVolumeDebugView -eq 1 -and $i2.probeVolumeDebugView -eq 2 -and $pv.view -eq 2 -and $pv0.view -eq 0 -and $dl -gt 8 -and $cs -gt 0.2) ("lighting differs by {0:N1}, sampling colored {1:P0}, views {2}/{3}, after None {4}" -f $dl, $cs, $i1.probeVolumeDebugView, $i2.probeVolumeDebugView, $pv0.view)

        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Invoke-Nova 'debugview none' | Out-Null; Write-Host "  $(Stop-TestEditor $ed)" }
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
}

function Suite-ForwardPlus
{
    # Forward+ (클러스터 조명): 그림자 있는 앞의 4 빛 밖의 점광 · 스포트광이 클러스터로 비친다 (예전에는 화면 전체에 빛 4 개).
    #  6 x 6 점광 (범위 4 m, 5 m 간격) 바닥 — 빛마다 바로 아래 바닥 밝기 (Main Camera 투영으로 자리) 를 빛이 안 닿는 바닥과 비교.
    #  Forward+ 끄면 (nova forwardplus set --enabled false) 앞의 3 개만, 켜면 36 개 · 레이어 마스크로 뺀 빛은 어둡다 · 스포트광 원뿔 (클러스터) ·
    #  Rendering Debugger 의 Additional Light Count · OpenGL · Vulkan 에서도 같다
    Write-Host '[forwardplus]'
    $dir = Join-Path $Out 'forwardplus'
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $ic = [Globalization.CultureInfo]::InvariantCulture

    function FpScene
    {
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'set "Directional Light" --component Light --values "{\"intensity\":0.05}"' | Out-Null
        Invoke-Nova 'create plane --name Ground --scale 4,1,4' | Out-Null
        $colors = @('1,0.2,0.1,1', '0.1,1,0.2,1', '0.2,0.4,1,1', '1,0.9,0.2,1', '1,0.2,1,1', '0.2,1,1,1')
        $k = 0
        for ($z = 0; $z -lt 6; $z++) { for ($x = 0; $x -lt 6; $x++) {
            $px = (($x - 2.5) * 5).ToString($ic); $pz = (($z - 2.5) * 5).ToString($ic)
            Invoke-Nova "create point-light --name L$k --position $px,1,$pz" | Out-Null
            Invoke-Nova ('set L' + $k + ' --component Light --values "{\"pointLightRange\":4,\"intensity\":3,\"pointLightDiffuse\":[' + $colors[$k % 6] + '],\"shadowType\":0}"') | Out-Null
            $k++ } }
        Invoke-Nova 'set "Main Camera" --position 0,14,-22 --rotation 38,0,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
    }
    # 월드 점 → Game 뷰 그림 픽셀 (Main Camera: (0, 14, -22), 아래로 38 도)
    function FpProject([double]$x, [double]$y, [double]$z, [int]$w, [int]$h, [double]$fovY)
    {
        $p = 38.0 * [math]::PI / 180.0
        $dx = $x; $dy = $y - 14.0; $dz = $z + 22.0
        $yv = $dy * [math]::Cos($p) + $dz * [math]::Sin($p); $zv = -$dy * [math]::Sin($p) + $dz * [math]::Cos($p)
        $t = [math]::Tan($fovY / 2.0); $aspect = $w / [double]$h
        @([int]((0.5 + 0.5 * $dx / ($zv * $t * $aspect)) * $w), [int]((0.5 - 0.5 * $yv / ($zv * $t)) * $h))
    }
    function FpMean($bm, [int]$cx, [int]$cy)
    {
        $s = 0.0; $n = 0
        for ($y = $cy - 3; $y -le $cy + 3; $y++) { for ($x = $cx - 3; $x -le $cx + 3; $x++) {
            if ($x -ge 0 -and $y -ge 0 -and $x -lt $bm.Width -and $y -lt $bm.Height) { $c = $bm.GetPixel($x, $y); $s += ($c.R + $c.G + $c.B) / 3.0; $n++ } } }
        $s / [math]::Max(1, $n)
    }
    # 빛마다 바로 아래 바닥이 빛 없는 바닥보다 20 넘게 밝은 수
    function FpLitCount([string]$png, [double]$fovY)
    {
        $bm = [System.Drawing.Bitmap]::FromFile($png)
        $refs = @((FpProject 17 0 -5 $bm.Width $bm.Height $fovY), (FpProject -17 0 -5 $bm.Width $bm.Height $fovY), (FpProject 17 0 5 $bm.Width $bm.Height $fovY), (FpProject -17 0 5 $bm.Width $bm.Height $fovY))
        $ref = ($refs | ForEach-Object { FpMean $bm $_[0] $_[1] } | Measure-Object -Average).Average
        $lit = 0; $vals = @()
        for ($z = 0; $z -lt 6; $z++) { for ($x = 0; $x -lt 6; $x++) {
            $q = FpProject (($x - 2.5) * 5) 0 (($z - 2.5) * 5) $bm.Width $bm.Height $fovY
            $v = FpMean $bm $q[0] $q[1]; $vals += $v
            if ($v -gt $ref + 20) { $lit++ } } }
        $bm.Dispose()
        [pscustomobject]@{ Lit = $lit; Ref = $ref; Vals = $vals }
    }
    function FpShot([string]$name) { $f = Join-Path $dir "$name.png"; Invoke-Nova ('screenshot "' + $f + '" --view game') | Out-Null; $f }

    $ed = Start-TestEditor
    try
    {
        FpScene
        $cam = Invoke-NovaJson 'get "Main Camera" --component Camera'
        $fov = if ($cam -and $cam.fovY) { [double]$cam.fovY } else { [math]::PI / 3 }

        # 같은 자리를 Forward+ 켬 · 끔으로: 클러스터 빛 33 개 자리는 켤 때만 밝고, 앞의 3 개 (그림자 칸) 는 둘 다 같다
        $on = FpLitCount (FpShot 'on') $fov
        $info = Invoke-NovaJson 'forwardplus info'
        Invoke-Nova 'forwardplus set --enabled false' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $off = FpLitCount (FpShot 'off') $fov
        Invoke-Nova 'forwardplus set --enabled true' | Out-Null
        $gain = 0; $same = 0
        for ($i = 0; $i -lt 36; $i++) { $d = $on.Vals[$i] - $off.Vals[$i]; if ($d -gt 8) { $gain++ } elseif ([math]::Abs($d) -lt 3) { $same++ } }
        $minOn = ($on.Vals | Measure-Object -Minimum).Minimum
        Add-Result forwardplus 'Forward+: the 33 clustered point lights light the floor (they are dark with Forward+ off); the 3 main lights are the same' ($gain -eq 33 -and $same -eq 3 -and $info.lights -eq 33 -and $info.dropped -eq 0 -and $minOn -gt $on.Ref + 5) ("brighter with Forward+ {0}, unchanged {1}, dimmest pool {2:N0} vs unlit floor {3:N0}, clustered {4}, indices {5}, max per cluster {6}, build {7:N2} ms" -f $gain, $same, $minOn, $on.Ref, $info.lights, $info.indices, $info.maxPerCluster, $info.buildMs)

        # 레이어 마스크: L14 (뒤쪽 줄 — 클러스터 빛) 가 Default 레이어를 비추지 않게
        Invoke-Nova 'set L14 --component Light --values "{\"cullingMaskBits\":4294967294}"' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $m = FpLitCount (FpShot 'mask') $fov
        Invoke-Nova 'set L14 --component Light --values "{\"cullingMaskBits\":4294967295}"' | Out-Null
        $others = 0
        for ($i = 0; $i -lt 36; $i++) { if ($i -ne 14 -and [math]::Abs($m.Vals[$i] - $on.Vals[$i]) -lt 3) { $others++ } }
        Add-Result forwardplus 'Light culling mask works for clustered lights (L14 without the Default layer goes dark, the rest unchanged)' ($on.Vals[14] - $m.Vals[14] -gt 8 -and [math]::Abs($m.Vals[14] - $off.Vals[14]) -lt 6 -and $others -eq 35) ("L14 floor {0:N0} (lit {1:N0}, Forward+ off {2:N0}), others unchanged {3}/35" -f $m.Vals[14], $on.Vals[14], $off.Vals[14], $others)

        # 스포트광 (클러스터): 격자 밖 (0, 6, 17) 에서 아래로, 원뿔 40 도 — 가운데는 밝고 3.5 m 옆은 어둡다
        Invoke-Nova 'create spot-light --name SpotA --position 0,6,17 --rotation 90,0,0' | Out-Null
        Invoke-Nova 'set SpotA --component Light --values "{\"spotLightRange\":10,\"spotLightSpot\":40,\"intensity\":6,\"shadowType\":0}"' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $f = FpShot 'spot'
        $bm = [System.Drawing.Bitmap]::FromFile($f)
        $c0 = FpProject 0 0 17 $bm.Width $bm.Height $fov; $c1 = FpProject 3.5 0 17 $bm.Width $bm.Height $fov
        $inside = FpMean $bm $c0[0] $c0[1]; $outside = FpMean $bm $c1[0] $c1[1]
        $bm.Dispose()
        $info = Invoke-NovaJson 'forwardplus info'
        Add-Result forwardplus 'Spot light in the clusters: bright inside the cone, dark 3.5 m to the side' ($inside -gt $outside + 25 -and $info.lights -eq 34) ("inside {0:N0}, outside {1:N0}, clustered {2}" -f $inside, $outside, $info.lights)

        Invoke-Nova 'debugview lights' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $f = FpShot 'light_count'
        $bm = [System.Drawing.Bitmap]::FromFile($f); $n = 0; $k = 0
        for ($y = 0; $y -lt $bm.Height; $y += 6) { for ($x = 0; $x -lt $bm.Width; $x += 6) { $c = $bm.GetPixel($x, $y); $n++; if ($c.R + $c.G + $c.B -gt 60) { $k++ } } }
        $bm.Dispose()
        Invoke-Nova 'debugview none' | Out-Null
        Add-Result forwardplus 'Rendering Debugger Additional Light Count shows the clusters that have lights' (($k / $n) -gt 0.2) ("colored share {0:P0}" -f ($k / $n))
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }

    # 다른 그래픽 API: 같은 장면에서 36 개 모두 (정수 텍스처 Load — GLSL · SPIR-V)
    foreach ($api in @('OpenGL', 'Vulkan'))
    {
        $ed = if ($api -eq 'OpenGL') { Start-TestEditor -OpenGL } else { Start-TestEditor -Vulkan }
        try
        {
            FpScene
            $cam = Invoke-NovaJson 'get "Main Camera" --component Camera'
            $fov = if ($cam -and $cam.fovY) { [double]$cam.fovY } else { [math]::PI / 3 }
            $r = FpLitCount (FpShot ("on_" + $api.ToLower())) $fov
            Invoke-Nova 'forwardplus set --enabled false' | Out-Null
            Invoke-Nova 'wait 3' | Out-Null
            $o = FpLitCount (FpShot ("off_" + $api.ToLower())) $fov
            Invoke-Nova 'forwardplus set --enabled true' | Out-Null
            $gain = 0
            for ($i = 0; $i -lt 36; $i++) { if ($r.Vals[$i] - $o.Vals[$i] -gt 8) { $gain++ } }
            $g = (Invoke-NovaJson 'info').graphicsAPI
            Add-Result forwardplus "Forward+ on $api : the 33 clustered lights light the floor" ($gain -eq 33) ("brighter with Forward+ {0}/33 (graphics: {1})" -f $gain, $g)
            Invoke-Nova 'window scene' | Out-Null
            Invoke-Nova 'scene new --force' | Out-Null
        }
        finally { Write-Host "  $(Stop-TestEditor $ed)" }
    }
}

function Suite-RenderGraph
{
    # Render Graph: Game · Scene 뷰가 패스 노드 (읽기 · 쓰기) 로 그려진다. `nova rendergraph info --view Game|Scene` 로 패스 · 빠짐을 본다.
    #  결과 (뷰 타깃 · SSR 히스토리) 에 닿지 않는 패스는 뺀다: 모션 벡터를 읽는 쪽 (SSAO 시간 누적 · TAA · Motion Blur Camera And Objects · Rendering Debugger) 이 없으면 Motion Vectors 가 빠진다
    Write-Host '[rendergraph]'
    $dir = Join-Path $Out 'rendergraph'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\RenderGraphTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    function RgProfile([string]$name, [array]$comps) { @{ nova_volume_profile = 1; components = $comps } | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $assetDir "$name.volumeprofile") }
    function RgPV($v) { @{ override = $true; value = @($v, 0, 0, 0) } }
    RgProfile 'NoTemporal' @(@{ type = 'AmbientOcclusion'; active = $true; params = @{ enabled = (RgPV 1); temporalAccumulation = (RgPV 0) } })
    RgProfile 'MbObjects' @(@{ type = 'AmbientOcclusion'; active = $true; params = @{ enabled = (RgPV 1); temporalAccumulation = (RgPV 0) } },
                            @{ type = 'MotionBlur'; active = $true; params = @{ intensity = (RgPV 1); mode = (RgPV 1) } })
    $ed = Start-TestEditor
    try
    {
        function Passes([string]$view) { $g = Invoke-NovaJson "rendergraph info --view $view"; if ($g) { @($g.passes) } else { @() } }
        function Culled($passes, [string]$name) { $p = $passes | Where-Object { $_.name -eq $name } | Select-Object -First 1; if ($p) { [bool]$p.culled } else { $null } }
        function Names($passes) { ($passes | ForEach-Object { $_.name + $(if ($_.culled) { ' (culled)' } else { '' }) }) -join ', ' }
        function Vol([string]$name) { Invoke-Nova ('set "Global Volume" --component Volume --values "{\"profile\":\"Assets/RenderGraphTest/' + $name + '.volumeprofile\"}"') | Out-Null }

        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create plane --name Ground --scale 3,1,3' | Out-Null
        Invoke-Nova 'create cube --name Box --position 0,0.5,0' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,3,-7 --rotation 19.65,0,0' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $s = Passes 'Scene'
        $order = ($s | Where-Object { -not $_.culled } | ForEach-Object { $_.name })
        Add-Result rendergraph 'Scene view is a Render Graph: Depth Prepass → Shadows → SSAO → Opaque … → UI, Motion Vectors culled (nobody reads it)' ($order.Count -ge 8 -and $order[0] -eq 'Depth Prepass' -and $order[-1] -eq 'UI' -and (Culled $s 'Motion Vectors') -eq $true -and (Culled $s 'Opaque') -eq $false) (Names $s)

        Invoke-Nova 'debugview motion' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $s2 = Passes 'Scene'
        Invoke-Nova 'debugview none' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $s3 = Passes 'Scene'
        Add-Result rendergraph 'Rendering Debugger Motion Vectors reads them → the pass runs; back to None → culled again' ((Culled $s2 'Motion Vectors') -eq $false -and ($s2 | Where-Object { $_.name -eq 'Rendering Debugger' }) -and (Culled $s3 'Motion Vectors') -eq $true) ("debug: MV culled={0}, none: MV culled={1}" -f (Culled $s2 'Motion Vectors'), (Culled $s3 'Motion Vectors'))

        Invoke-Nova 'window game' | Out-Null
        Vol 'NoTemporal'
        Invoke-Nova 'wait 10' | Out-Null
        $g1 = Passes 'Game'
        Invoke-Nova 'set "Main Camera" --component Camera --values "{\"antiAliasing\":3}"' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $g2 = Passes 'Game'
        Invoke-Nova 'set "Main Camera" --component Camera --values "{\"antiAliasing\":0}"' | Out-Null
        Vol 'MbObjects'
        Invoke-Nova 'wait 5' | Out-Null
        $g3 = Passes 'Game'
        Add-Result rendergraph 'Game view: Motion Vectors culled without readers (no SSAO temporal, no TAA), runs with TAA, runs with Motion Blur Camera And Objects' ((Culled $g1 'Motion Vectors') -eq $true -and (Culled $g2 'Motion Vectors') -eq $false -and (Culled $g3 'Motion Vectors') -eq $false) ("plain: {0} | TAA: {1} | MB objects: {2}" -f (Culled $g1 'Motion Vectors'), (Culled $g2 'Motion Vectors'), (Culled $g3 'Motion Vectors'))
        $post = $g3 | Where-Object { $_.name -eq 'Post Processing' } | Select-Object -First 1
        Add-Result rendergraph 'Pass reads are recorded (Post Processing reads Scene Color + Motion Vectors, writes View Target)' ($post -and ($post.reads -join ' ') -match 'Motion Vectors' -and ($post.reads -join ' ') -match 'Scene Color' -and ($post.writes -join ' ') -match 'View Target') ("reads: {0} writes: {1}" -f ($post.reads -join ', '), ($post.writes -join ', '))

        Invoke-Nova 'window render-graph-viewer' | Out-Null
        Invoke-Nova 'wait 5' | Out-Null
        $f = Join-Path $dir 'viewer.png'
        Invoke-Nova ('screenshot "' + $f + '" --view editor') | Out-Null
        Invoke-Nova 'window render-graph-viewer --close' | Out-Null
        Add-Result rendergraph 'Window > Analysis > Render Graph Viewer opens (editor capture)' ((Test-Path $f) -and (Get-Item $f).Length -gt 50000) ("capture {0} bytes" -f $(if (Test-Path $f) { (Get-Item $f).Length } else { 0 }))
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
}

function Suite-SSR
{
    # Screen Space Reflection (HDRP Volume): 거울 바닥 위 빨간 상자 — 바닥의 반사 자리에 상자가 비치는가
    Write-Host '[ssr]'
    $dir = Join-Path $Out 'ssr'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\SsrTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $base = Get-Content (Join-Path $Project 'Assets\Materials\Red Plastic.mat') -Raw | ConvertFrom-Json
    foreach ($m in @(@('Mirror', @(0.7, 0.7, 0.72, 1), 1.0, 1.0), @('Satin', @(0.7, 0.7, 0.72, 1), 1.0, 0.8), @('Red', @(0.9, 0.1, 0.1, 1), 0.0, 0.3)))
    {
        $mat = $base.PSObject.Copy(); $mat.BaseColor = $m[1]; $mat.Metallic = $m[2]; $mat.Smoothness = $m[3]; $mat.ResourcePath = "Assets\SsrTest\$($m[0]).mat"
        $mat | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir "$($m[0]).mat")
    }
    foreach ($p in @(@('On', 1), @('Off', 0)))
    {
        @{ nova_volume_profile = 1; components = @(@{ type = 'ScreenSpaceReflection'; active = $true; params = @{ enabled = @{ override = $true; value = @($p[1], 0, 0, 0) } } }) } | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $assetDir "$($p[0]).volumeprofile")
    }
    $ed = Start-TestEditor
    try
    {
        function Mat([string]$obj, [string]$mat) { Invoke-Nova ("set $obj --component MeshRenderer --values `"{\`"m_MaterialPaths\`":[\`"Assets/SsrTest/$mat.mat\`"]}`"") | Out-Null }
        function Vol([string]$name) { Invoke-Nova ('set "Global Volume" --component Volume --values "{\"profile\":\"Assets/SsrTest/' + $name + '.volumeprofile\"}"') | Out-Null }
        function Shot([string]$name, [string]$view = 'scene') { $p = Join-Path $dir $name; Invoke-Nova 'wait 5' | Out-Null; Invoke-Nova "screenshot $p --view $view" | Out-Null; $p }
        # 영역 안 빨간 픽셀 비율
        function RedShare([string]$p, [double]$x0, [double]$x1, [double]$y0, [double]$y1)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($p); $r = 0; $n = 0
            for ($y = [int]($bm.Height * $y0); $y -lt [int]($bm.Height * $y1); $y++) {
                for ($x = [int]($bm.Width * $x0); $x -lt [int]($bm.Width * $x1); $x++) {
                    $c = $bm.GetPixel($x, $y); $n++
                    if ($c.R -gt $c.G + 40 -and $c.R -gt $c.B + 40) { $r++ } } }
            $bm.Dispose(); $r / [math]::Max(1, $n)
        }
        $refl = @(0.36, 0.42, 0.62, 0.72)   # 바닥에 비친 빨간 상자 (Scene 뷰)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 120) { $t = Invoke-Nova 'log -n 400'; if ($t -match 'compiled 32\. InstancedBasic|cache hit 32\. InstancedBasic') { break }; Start-Sleep -Milliseconds 500 }
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name Floor --position 0,-0.5,4 --scale 12,1,14' | Out-Null; Mat 'Floor' 'Mirror'
        Invoke-Nova 'create cube --name RedBox --position -1.6,0.75,5 --scale 1,1.5,1' | Out-Null; Mat 'RedBox' 'Red'
        Invoke-Nova 'camera --position 0,1.2,-1.5 --target 0,0.4,5' | Out-Null

        Vol 'Off'; $p0 = Shot 'off.png'; $r0 = RedShare $p0 @refl
        Vol 'On'; $p1 = Shot 'on.png'; $r1 = RedShare $p1 @refl
        Add-Result ssr 'Enable: the red box shows up in the mirror floor (probe / sky reflection before)' ($r0 -lt 0.05 -and $r1 -gt 0.8) ("red in the reflection {0:P0} -> {1:P0}" -f $r0, $r1)

        # Minimum Smoothness 0.9: 매끈함 0.8 바닥은 반사 없음 (프로브 · 하늘 그대로)
        Mat 'Floor' 'Satin'
        $p2 = Shot 'satin.png'; $r2 = RedShare $p2 @refl
        Mat 'Floor' 'Mirror'
        Add-Result ssr 'Minimum Smoothness 0.9: a floor with smoothness 0.8 gets no screen space reflection' ($r2 -lt 0.05) ("red {0:P0}" -f $r2)

        # 카메라가 움직인 첫 프레임: 지난 프레임 색을 지난 ViewProj 로 되돌려 찾는다 — 반사가 제자리
        Invoke-Nova 'camera --position 0.4,1.3,-1.6 --target 0.2,0.4,5' | Out-Null
        $p3 = Join-Path $dir 'moved_first.png'; Invoke-Nova "screenshot $p3 --view scene" | Out-Null
        $p4 = Shot 'moved_settled.png'
        $bx = @(0.33, 0.47, 0.55, 0.8)
        $r3 = RedShare $p3 @bx; $r4 = RedShare $p4 @bx
        Add-Result ssr 'Camera jump: the first frame (previous frame colors reprojected) matches the settled frame' ($r4 -gt 0.1 -and [math]::Abs($r3 - $r4) -lt $r4 * 0.15) ("red first {0:P1}, settled {1:P1}" -f $r3, $r4)

        # Game 뷰도 (Main Camera)
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.2,-1.5 --rotation 9,0,0' | Out-Null
        Vol 'Off'; $g0 = Shot 'game_off.png' 'game'
        Vol 'On'; $g1 = Shot 'game_on.png' 'game'
        $ga = @(0.3, 0.48, 0.55, 0.85)
        $rg0 = RedShare $g0 @ga; $rg1 = RedShare $g1 @ga
        Invoke-Nova 'window scene' | Out-Null
        Add-Result ssr 'Game view: the reflection also appears (Main Camera)' ($rg1 -gt $rg0 + 0.05) ("red {0:P1} -> {1:P1}" -f $rg0, $rg1)
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-ModelPlace
{
    # 모델 끌어 놓기 (Project → Hierarchy · Scene 뷰 와 같은 길 = nova modelfile place): 노드마다 Mesh Renderer, _LODn → LOD Group, FBX 의 묻힌 그림 꺼내기
    Write-Host '[modelplace]'
    $dir = Join-Path $Out 'modelplace'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\ModelTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Copy-Item (Join-Path $Root 'Resources\Meshs\SmallBoat.fbx') $assetDir
    Copy-Item (Join-Path $Root 'Resources\Meshs\Factory_04_02_closed.FBX') $assetDir
    # 묻힌 그림 (PNG 둘) 이 있는 FBX — Tools/tests/make_embedded_fbx.py (Blender) 로 만든 검사 자료
    Copy-Item (Join-Path $PSScriptRoot 'data\EmbeddedTextures.fbx') $assetDir
    # LOD 노드 셋 (높이 2 · 1.4 · 0.8 상자) 짜리 glTF
    & python (Join-Path $PSScriptRoot 'make_lod_gltf.py') (Join-Path $assetDir 'LodCrate.gltf') | Out-Null
    Add-Type -AssemblyName System.Drawing
    $ed = Start-TestEditor
    try
    {
        function Place([string]$args1) { Invoke-NovaJson "modelfile place $args1" }
        function Size($b) { [math]::Max([math]::Max($b[1][0] - $b[0][0], $b[1][1] - $b[0][1]), $b[1][2] - $b[0][2]) }
        function LodGroups([string]$name) { @((Invoke-NovaJson 'lod info').groups | Where-Object { $_.name -eq $name }) }
        # 영역 안에 하늘 · 바닥 아닌 (회색 재질) 픽셀 비율
        function Solid([string]$p)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($p); $n = 0; $s = 0
            for ($y = [int]($bm.Height * 0.3); $y -lt [int]($bm.Height * 0.7); $y += 2) { for ($x = [int]($bm.Width * 0.3); $x -lt [int]($bm.Width * 0.7); $x += 2) {
                $c = $bm.GetPixel($x, $y); $n++
                if ([math]::Abs($c.R - $c.B) -lt 25 -and $c.B -lt 200) { $s++ } } }
            $bm.Dispose(); $s / [math]::Max(1, $n)
        }
        Invoke-Nova 'scene new --force' | Out-Null

        $boat = Place 'Assets/ModelTest/SmallBoat.fbx --position 0,0,0'
        $bs = Size $boat.bounds
        Invoke-Nova ('camera --position {0},{1},{2} --target 0,{3},0' -f ($bs * 0.9), ($bs * 0.6), (-$bs * 1.2), ($bs * 0.1)) | Out-Null
        $p = Join-Path $dir 'boat.png'; Invoke-Nova 'wait 5' | Out-Null; Invoke-Nova "screenshot $p --view scene" | Out-Null
        $sv = Solid $p
        Add-Result modelplace 'Static FBX: root + a GameObject per node with Mesh Filter + Mesh Renderer, file units (cm -> m)' ($boat.gameObjects -eq 4 -and $boat.meshRenderers -eq 3 -and $bs -gt 0.5 -and $bs -lt 20 -and $sv -gt 0.05) ("{0} objects, {1} renderers, size {2:N2} m, on screen {3:P0}" -f $boat.gameObjects, $boat.meshRenderers, $bs, $sv)

        Invoke-Nova 'scene new --force' | Out-Null
        $fac = Place 'Assets/ModelTest/Factory_04_02_closed.FBX'
        $inner = @(LodGroups 'Factory_04_02_closed')
        $lods = @($inner | Where-Object { $_.lods.Count -eq 4 })
        $ok = $lods.Count -eq 1 -and $inner.Count -eq 1 -and $lods[0].lods[3].renderers[0] -eq 'Factory_04_02_closed_LOD3'
        Add-Result modelplace 'FBX with _LOD0.._LOD3 nodes: a LOD Group with 4 LODs (a lone UCX_.._LOD0 collision mesh stays a plain mesh)' $ok ("{0} renderers, groups {1}, LODs {2}, size {3:N1} m" -f $fac.meshRenderers, $inner.Count, (@($inner | ForEach-Object { $_.lods.Count }) -join '/'), (Size $fac.bounds))

        Invoke-Nova 'scene new --force' | Out-Null
        $crate = Place 'Assets/ModelTest/LodCrate.gltf --position 0,0,0'
        $g = @(LodGroups 'LodCrate')
        Invoke-Nova 'camera --position 0,1,-2.5 --target 0,1,0' | Out-Null; Invoke-Nova 'wait 3' | Out-Null
        $near = @(LodGroups 'LodCrate')[0].sceneLOD
        Invoke-Nova 'camera --position 0,1,-12 --target 0,1,0' | Out-Null; Invoke-Nova 'wait 3' | Out-Null
        $far = @(LodGroups 'LodCrate')[0].sceneLOD
        Add-Result modelplace 'glTF Crate_LOD0..2: LOD Group on the root (60 / 30 / 1 %), near = LOD 0, far = LOD 2' ($g.Count -eq 1 -and $g[0].lods.Count -eq 3 -and $near -eq 0 -and $far -eq 2) ("lods {0}, near LOD {1}, far LOD {2}" -f $g[0].lods.Count, $near, $far)

        # 부모 아래 (Hierarchy 의 행에 끌어 놓기와 같은 길)
        Invoke-Nova 'create empty --name Holder --position 5,0,0' | Out-Null
        $b2 = Place 'Assets/ModelTest/SmallBoat.fbx --parent Holder'
        $w = Invoke-NovaJson 'get SmallBoat_hull'   # 모델 루트와 같은 이름 (SmallBoat) 의 노드가 있어 이름이 하나뿐인 것으로
        Add-Result modelplace 'Drop on a Hierarchy row: the model goes under that object at its origin (world x = Holder 5)' ($w -and [math]::Abs($w.worldPosition[0] - 5) -lt 0.01) ("hull world x {0}" -f $(if ($w) { $w.worldPosition[0] } else { 'n/a' }))

        # 저장 → 다시 열기: 메시 참조 (모델 경로 + 메시 번호) 가 그대로
        Invoke-Nova 'scene save --as Assets/ModelTest/ModelScene.scene' | Out-Null
        Invoke-Nova 'scene open Assets/ModelTest/ModelScene.scene --force' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $h = Invoke-Nova 'hierarchy'
        $g2 = @(LodGroups 'LodCrate')
        Add-Result modelplace 'Save -> reopen: the placed models and their LOD Group come back' ($h -match 'SmallBoat_hull' -and $h -match 'Crate_LOD2' -and $g2.Count -eq 1 -and $g2[0].lods[2].renderers[0] -eq 'Crate_LOD2') ("LOD Group {0}" -f $g2.Count)

        # Undo: 놓기 한 번 = 되돌리기 한 번
        Invoke-Nova 'scene new --force' | Out-Null
        Place 'Assets/ModelTest/SmallBoat.fbx' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        Invoke-Nova 'undo' | Out-Null; Invoke-Nova 'wait 3' | Out-Null
        $h2 = Invoke-Nova 'hierarchy'
        Add-Result modelplace 'Undo removes the placed model in one step' (-not ($h2 -match 'SmallBoat')) ($(if ($h2 -match 'SmallBoat') { 'still there' } else { 'gone' }))

        # FBX 의 묻힌 그림 → <이름>_FBX.Textures (Unity 의 Extract Textures) + 재질의 Base Map, 화면에 체커 (빨강) · 줄무늬 (파랑)
        Invoke-Nova 'scene new --force' | Out-Null
        $emb = Place 'Assets/ModelTest/EmbeddedTextures.fbx --position 0,0,0'
        $texDir = Join-Path $assetDir 'EmbeddedTextures_FBX.Textures'
        $pngs = @(Get-ChildItem $texDir -Filter *.png -ErrorAction SilentlyContinue | Where-Object { $b = [IO.File]::ReadAllBytes($_.FullName); $b.Length -gt 8 -and $b[0] -eq 0x89 -and $b[1] -eq 0x50 })
        $cm = Get-Content (Join-Path $assetDir 'EmbeddedTextures_FBX.Materials\CheckerMat.mat') -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
        $sm = Get-Content (Join-Path $assetDir 'EmbeddedTextures_FBX.Materials\StripeMat.mat') -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
        Invoke-Nova 'camera --position 0,1.5,-7 --target 0,1,0' | Out-Null; Invoke-Nova 'wait 5' | Out-Null
        $p = Join-Path $dir 'embedded.png'; Invoke-Nova "screenshot $p --view scene" | Out-Null
        $red = 0; $blue = 0; $all = 0
        if (Test-Path $p)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($p)
            for ($y = 0; $y -lt $bm.Height; $y += 3) { for ($x = 0; $x -lt $bm.Width; $x += 3) { $c = $bm.GetPixel($x, $y); $all++
                if ($c.R -gt 140 -and $c.G -lt 90 -and $c.B -lt 90) { $red++ } elseif ($c.B -gt 140 -and $c.R -lt 80 -and $c.G -lt 120) { $blue++ } } }
            $bm.Dispose()
        }
        $okEmb = $emb -and $pngs.Count -eq 2 -and $cm -and $cm.BaseMapPath -match 'EmbeddedTextures_FBX\.Textures[\\/]checker_red\.png$' -and $sm.BaseMapPath -match 'stripe_blue\.png$' -and
            $cm.BaseColor[0] -eq 1 -and $red / [math]::Max(1, $all) -gt 0.005 -and $blue / [math]::Max(1, $all) -gt 0.005
        Add-Result modelplace 'FBX embedded textures: extracted to <name>_FBX.Textures, used as Base Map, visible (red checker, blue stripes)' $okEmb ("placed {0}, {1} PNG files ({2}), checker base map '{3}' color {4}, stripe base map '{5}', red {6:P1} blue {7:P1}" -f [bool]$emb, $pngs.Count, (($pngs | ForEach-Object { $_.Name }) -join ', '), $(if ($cm) { $cm.BaseMapPath } else { 'no .mat' }), $(if ($cm) { $cm.BaseColor -join ',' } else { '' }), $(if ($sm) { $sm.BaseMapPath } else { 'no .mat' }), ($red / [math]::Max(1, $all)), ($blue / [math]::Max(1, $all)))
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-AntiAliasing
{
    # 카메라 Anti-aliasing (URP): 없음 · FXAA · SMAA · TAA — 비스듬한 상자 모서리의 중간 밝기 픽셀 (계단이 풀린 정도)
    Write-Host '[antialiasing]'
    $dir = Join-Path $Out 'antialiasing'
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $ed = Start-TestEditor
    try
    {
        function Shot([string]$name, [int]$frames = 30) { $p = Join-Path $dir $name; Invoke-Nova "wait $frames" | Out-Null; Invoke-Nova "screenshot $p --view game" | Out-Null; $p }
        function AA([int]$mode, [string]$extra = '') { Invoke-Nova ('set "Main Camera" --component Camera --values "{\"antiAliasing\":' + $mode + $extra + '}"') | Out-Null }
        # 상자 위 모서리: 열마다 위 (하늘) · 아래 (상자) 밝기 사이에서 모서리 둘레 ±3 px 안의 중간 밝기 픽셀 수 (계단이 풀린 정도)
        function Mid([string]$p)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($p)
            $x0 = [int]($bm.Width * 0.46); $x1 = [int]($bm.Width * 0.54); $y0 = [int]($bm.Height * 0.24); $y1 = [int]($bm.Height * 0.42)
            $mid = 0
            for ($x = $x0; $x -lt $x1; $x++) {
                $ls = @(); for ($y = $y0; $y -lt $y1; $y++) { $c = $bm.GetPixel($x, $y); $ls += 0.3 * $c.R + 0.59 * $c.G + 0.11 * $c.B }
                # 모서리 = 위아래 2 px 차가 가장 큰 곳, 기준 밝기 = 그 위 · 아래 4 px (상자 쪽 그늘 · 하늘 그라디언트는 빼고)
                $edge = 4; $best = 0.0
                for ($i = 4; $i -lt $ls.Count - 4; $i++) { $d = $ls[$i - 2] - $ls[$i + 2]; if ($d -gt $best) { $best = $d; $edge = $i } }
                $lt = $ls[$edge - 4]; $lb = $ls[$edge + 4]
                for ($i = $edge - 3; $i -le $edge + 3; $i++) { $t = ($ls[$i] - $lb) / [math]::Max(1.0, $lt - $lb); if ($t -gt 0.2 -and $t -lt 0.8) { $mid++ } } }
            $bm.Dispose(); $mid / [math]::Max(1, $x1 - $x0)
        }
        function ImgDiff([string]$a, [string]$b)   # (Diff 는 Compare-Object 별칭)
        {
            $ba = [System.Drawing.Bitmap]::FromFile($a); $bb = [System.Drawing.Bitmap]::FromFile($b); $s = 0.0; $n = 0
            for ($y = 0; $y -lt $ba.Height; $y += 3) { for ($x = 0; $x -lt $ba.Width; $x += 3) {
                $p = $ba.GetPixel($x, $y); $q = $bb.GetPixel($x, $y); $s += [math]::Abs($p.R - $q.R) + [math]::Abs($p.G - $q.G) + [math]::Abs($p.B - $q.B); $n++ } }
            $ba.Dispose(); $bb.Dispose(); $s / [math]::Max(1, $n)
        }
        Invoke-Nova 'scene new --force' | Out-Null
        # 어두운 상자 (하늘과 대비) 를 17 도 기울여 — 위 모서리가 화면 가운데 위쪽을 가로지른다
        Invoke-Nova 'create cube --name Slab --position 0,1.0,4 --rotation 0,0,17 --scale 1.6,1.6,0.3' | Out-Null
        $mat = Get-Content (Join-Path $Project 'Assets\Materials\Red Plastic.mat') -Raw | ConvertFrom-Json
        $matDir = Join-Path $Project 'Assets\AATest'; New-Item -ItemType Directory -Force $matDir | Out-Null
        $mat.BaseColor = @(0.05, 0.05, 0.06, 1); $mat.ResourcePath = 'Assets\AATest\Dark.mat'
        $mat | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $matDir 'Dark.mat')
        Invoke-Nova 'set Slab --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/AATest/Dark.mat\"]}"' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'set "Main Camera" --position 0,1.0,0 --rotation 0,0,0' | Out-Null   # 상자 높이 — 윗면이 안 보이게

        AA 0; $p0 = Shot 'none.png'; $m0 = Mid $p0
        AA 1; $p1 = Shot 'fxaa.png'; $m1 = Mid $p1
        AA 2 ',\"smaaQuality\":2'; $p2 = Shot 'smaa.png'; $m2 = Mid $p2
        AA 3; $p3 = Shot 'taa.png' 60; $m3 = Mid $p3
        Add-Result antialiasing 'No AA = hard steps on the slanted edge' ($m0 -lt 1.0) ("in-between pixels per column {0:N2}" -f $m0)
        Add-Result antialiasing 'FXAA softens the steps' ($m1 -gt $m0 + 0.4) ("{0:N2} -> {1:N2}" -f $m0, $m1)
        Add-Result antialiasing 'SMAA (High): steps blended along the edge (pattern + area)' ($m2 -gt $m0 + 0.4) ("{0:N2} -> {1:N2}" -f $m0, $m2)
        Add-Result antialiasing 'TAA: jittered frames resolve into a smooth edge' ($m3 -gt $m0 + 0.4) ("{0:N2} -> {1:N2}" -f $m0, $m3)

        # TAA 가 가만히 있는 화면에서 흔들리지 않음 (히스토리가 수렴) · SMAA 는 모서리 밖을 흐리지 않음
        $p4 = Shot 'taa_again.png' 10
        $d34 = ImgDiff $p3 $p4; $d02 = ImgDiff $p0 $p2
        Add-Result antialiasing 'TAA is stable on a still camera (two frames ~ same); SMAA leaves flat areas alone' ($d34 -lt 3.0 -and $d02 -lt 3.0) ("TAA frame diff {0:N2}, none vs SMAA {1:N2}" -f $d34, $d02)

        # 저장되는 값 (URP 이름)
        AA 3 ',\"taaQuality\":1,\"taaBaseBlendFactor\":0.9,\"taaContrastAdaptiveSharpening\":0.5'
        $c = Invoke-NovaJson 'get "Main Camera" --component Camera'
        Add-Result antialiasing 'Camera keeps the AA settings (TAA Quality · Base Blend Factor · Sharpening)' ($c.antiAliasing -eq 3 -and $c.taaQuality -eq 1 -and [math]::Abs($c.taaBaseBlendFactor - 0.9) -lt 1e-4 -and [math]::Abs($c.taaContrastAdaptiveSharpening - 0.5) -lt 1e-4) ("aa {0}, quality {1}, blend {2}, sharpen {3}" -f $c.antiAliasing, $c.taaQuality, $c.taaBaseBlendFactor, $c.taaContrastAdaptiveSharpening)
        $p5 = Shot 'taa_sharpen.png'
        $m5 = Mid $p5
        Add-Result antialiasing 'TAA + Contrast Adaptive Sharpening still anti-aliased' ($m5 -gt $m0 + 0.3) ("{0:N2}" -f $m5)
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item (Join-Path $Project 'Assets\AATest'), (Join-Path $Project 'Assets\AATest.meta') -Recurse -Force -ErrorAction SilentlyContinue
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

function Capture-Scenes([switch]$OpenGL, [switch]$Vulkan, [switch]$D3D12, [string]$dir)
{
    New-Item -ItemType Directory -Force $dir | Out-Null
    $ed = Start-TestEditor -OpenGL:$OpenGL -Vulkan:$Vulkan -D3D12:$D3D12
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

# ------------------------------------------------------------------ Vulkan (docs/VULKAN_BACKEND.md): 화면 없는 Vulkan 장치 ↔ DX11 엔진 장치
function Suite-Vulkan
{
    Write-Host '[vulkan]'
    $ed = Start-TestEditor
    try
    {
        foreach ($t in 'gfx-test', 'rhi-test')
        {
            $j = Invoke-NovaJson "vulkan $t"
            $vk = if ($j) { @($j.results | Where-Object { $_.api -eq 'Vulkan' })[0] } else { $null }
            $ok = $j -and $j.diff -and [double]$j.diff.max -le 2 -and $vk -and [int]$vk.validationErrors -eq 0
            $detail = if ($j -and $j.diff) { "diff max $($j.diff.max), validation errors $($vk.validationErrors) / warnings $($vk.validationWarnings)" }
                      elseif ($vk) { "Vulkan: $($vk.error)" } else { 'no result' }
            Add-Result vulkan $t $ok $detail
        }
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
    # 에디터가 Vulkan 으로: 렌더 장면들이 DX11 과 같은지 (render 묶음과 같은 기준)
    Write-Host '[vulkan] scenes DirectX 11'
    Capture-Scenes -dir (Join-Path $Out 'vulkan\dx')
    Write-Host '[vulkan] scenes Vulkan'
    Capture-Scenes -Vulkan -dir (Join-Path $Out 'vulkan\vk')
    # 로그: 검증 레이어 · 장치 오류 (로더의 다른 프로그램 레이어 JSON 줄과 옛 compute 예제는 빼고)
    $vkErrors = Get-Content $EditorLog | Where-Object { $_ -match '\[Vulkan\] (ERROR|.*failed|.*device lost)' -and $_ -notmatch 'Loader Message' }
    Add-Result vulkan 'Vulkan log clean' ($vkErrors.Count -eq 0) $(if ($vkErrors) { $vkErrors[0] } else { 'no validation errors / failures' })
    foreach ($n in $Targets.Keys)
    {
        $a = Join-Path $Out "vulkan\dx\$n.png"; $b = Join-Path $Out "vulkan\vk\$n.png"
        if (-not (Test-Path $a) -or -not (Test-Path $b)) { Add-Result vulkan "$n DX = Vulkan" $false 'capture missing'; continue }
        $c = [NovaImageCompare]::Compare($a, $b, (Join-Path $Out "vulkan\${n}_diff.png"))
        if (-not $c) { Add-Result vulkan "$n DX = Vulkan" $false 'size differs'; continue }
        # 화소 최대 차이 20 이하. 넘어도 몇 화소뿐이면 통과: 모서리에 거의 걸친 화소 중심은 정점 계산의 마지막 자리 차이
        #  (DX = fxc, Vulkan = DXC → SPIR-V) 로 이쪽 · 저쪽 면에 갈린다 — 실행마다 다른 1 ~ 3 화소 (창 크기에 따라 max 14 · 39).
        #  0.005 % (1143 x 567 에서 약 30 화소) 를 넘으면 진짜 다른 그림으로 본다
        $ok = if ($n -eq 'Trees') { $c[2] -le 6.0 } else { $c[0] -le 20 -or ($c[2] -le 0.005 -and $c[1] -le 0.05) }
        Add-Result vulkan "$n DX = Vulkan" $ok ('max {0}, mean {1:N3}, >8: {2:N4}%' -f $c[0], $c[1], $c[2])
    }
}

# ------------------------------------------------------------------ DirectX 12 (docs/DIRECTX12_BACKEND.md): 화면 없는 DX12 장치 ↔ DX11 엔진 장치, 에디터를 DX12 로
function Suite-D3D12
{
    Write-Host '[d3d12]'
    $ed = Start-TestEditor
    try
    {
        $sh = Invoke-NovaJson 'd3d12 shaders'
        $unresolved = if ($sh) { ($sh.files | Measure-Object -Property unresolvedBindings -Sum).Sum } else { -1 }
        Add-Result d3d12 'every .fx compiles to DXIL, every binding resolves by name' ($sh -and $sh.effectsOk -eq $sh.effects -and $sh.passesFailed -eq 0 -and $unresolved -eq 0) $(if ($sh) { "effects $($sh.effectsOk)/$($sh.effects), passes failed $($sh.passesFailed)/$($sh.passes), unresolved bindings $unresolved" } else { 'no result' })
        foreach ($t in 'gfx-test', 'rhi-test')
        {
            $j = Invoke-NovaJson "d3d12 $t"
            $d12 = if ($j) { @($j.results | Where-Object { $_.api -eq 'DirectX12' })[0] } else { $null }
            $ok = $j -and $j.diff -and [double]$j.diff.max -le 2
            $detail = if ($j -and $j.diff) { "diff max $($j.diff.max), mean $($j.diff.mean)" } elseif ($d12) { "DirectX12: $($d12.error)" } else { 'no result' }
            Add-Result d3d12 $t $ok $detail
        }
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
    Write-Host '[d3d12] scenes DirectX 11'
    Capture-Scenes -dir (Join-Path $Out 'd3d12\dx11')
    Write-Host '[d3d12] scenes DirectX 12'
    Capture-Scenes -D3D12 -dir (Join-Path $Out 'd3d12\dx12')
    # 로그: 디버그 층 오류 · 경고 · 실패 (Debug 빌드는 디버그 층이 켜져 있다)
    $dxErrors = Get-Content $EditorLog | Where-Object { $_ -match '\[DX12\] (\[debug layer|.*failed|.*device removed|.*device lost|.*dropped)' }
    Add-Result d3d12 'DirectX 12 log clean (debug layer)' ($dxErrors.Count -eq 0) $(if ($dxErrors) { $dxErrors[0] } else { 'no debug layer messages / failures' })
    foreach ($n in $Targets.Keys)
    {
        $a = Join-Path $Out "d3d12\dx11\$n.png"; $b = Join-Path $Out "d3d12\dx12\$n.png"
        if (-not (Test-Path $a) -or -not (Test-Path $b)) { Add-Result d3d12 "$n DX11 = DX12" $false 'capture missing'; continue }
        $c = [NovaImageCompare]::Compare($a, $b, (Join-Path $Out "d3d12\${n}_diff.png"))
        if (-not $c) { Add-Result d3d12 "$n DX11 = DX12" $false 'size differs'; continue }
        # Vulkan 과 같은 기준 (DX11 = fxc, DX12 = DXC 의 마지막 자리 차이로 모서리 몇 화소가 갈린다)
        $ok = if ($n -eq 'Trees') { $c[2] -le 6.0 } else { $c[0] -le 20 -or ($c[2] -le 0.005 -and $c[1] -le 0.05) }
        Add-Result d3d12 "$n DX11 = DX12" $ok ('max {0}, mean {1:N3}, >8: {2:N4}%' -f $c[0], $c[1], $c[2])
    }
}

# ------------------------------------------------------------------ Streaming Virtual Texturing (docs/VIRTUAL_TEXTURING.md)
function New-VTTestImage([string]$path, [int]$size)
{
    # 큰 그림: 128 px 페이지마다 다른 색 (페이지 x · y) + 8 px 체커 — 페이지가 바뀌거나 잘못된 칸이면 바로 보인다
    if (-not ('NovaVTImage' -as [type]))
    {
        Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class NovaVTImage
{
    public static void Make(string path, int size)
    {
        var b = new Bitmap(size, size, PixelFormat.Format32bppArgb);
        var d = b.LockBits(new Rectangle(0, 0, size, size), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        var row = new byte[size * 4];
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                int px = x / 128, py = y / 128;
                bool on = ((x / 8) + (y / 8)) % 2 == 0;
                int r = 40 + (px * 53) % 200, g = 40 + (py * 71) % 200, bl = 40 + ((px + py) * 29) % 200;
                if (!on) { r /= 2; g /= 2; bl /= 2; }
                if (x % 128 == 0 || y % 128 == 0) { r = g = bl = 250; }
                row[x * 4] = (byte)bl; row[x * 4 + 1] = (byte)g; row[x * 4 + 2] = (byte)r; row[x * 4 + 3] = 255;
            }
            Marshal.Copy(row, 0, d.Scan0 + y * d.Stride, row.Length);
        }
        b.UnlockBits(d);
        b.Save(path, ImageFormat.Png);
        b.Dispose();
    }
}
"@
    }
    [NovaVTImage]::Make($path, $size)
}

function Suite-VirtualTexture
{
    Write-Host '[virtualtexture]'
    $dir = Join-Path $Out 'virtualtexture'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\VTTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    New-VTTestImage (Join-Path $assetDir 'Big.png') 4096
    Copy-Item (Join-Path $assetDir 'Big.png') (Join-Path $assetDir 'Ref.png')
    foreach ($n in 'VT', 'Ref')
    {
        $m = (Get-Content (Join-Path $Project 'Assets\Materials\Red Plastic.mat') -Raw | ConvertFrom-Json).PSObject.Copy()
        $m.BaseColor = @(1, 1, 1, 1); $m.Metallic = 0; $m.Smoothness = 0.0
        $m.BaseMapPath = $(if ($n -eq 'VT') { 'Assets\VTTest\Big.png' } else { 'Assets\VTTest\Ref.png' })
        $m.ResourcePath = "Assets\VTTest\$n.mat"
        $m | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir "$n.mat")
    }
    function Mat([string]$n) { Invoke-Nova ('set Ground --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/VTTest/' + $n + '.mat\"]}"') | Out-Null }
    function Shot([string]$name) { $f = Join-Path $dir "$name.png"; Invoke-Nova ('screenshot "' + $f + '" --view game') | Out-Null; $f }
    function Settle { for ($i = 0; $i -lt 8; $i++) { Invoke-Nova 'wait 15' | Out-Null } }
    $captures = @{}
    foreach ($api in 'dx11', 'gl', 'vk', 'dx12')
    {
        $ed = Start-TestEditor -OpenGL:($api -eq 'gl') -Vulkan:($api -eq 'vk') -D3D12:($api -eq 'dx12')
        try
        {
            Invoke-Nova 'autosave discard' | Out-Null
            Invoke-Nova 'import-settings Assets/VTTest/Big.png --values "{\"virtualTextureOnly\":true,\"maxSize\":4096,\"compression\":\"None\"}"' | Out-Null
            Invoke-Nova 'import-settings Assets/VTTest/Ref.png --values "{\"maxSize\":4096,\"compression\":\"None\"}"' | Out-Null
            Invoke-Nova 'scene new --force' | Out-Null
            Invoke-Nova 'create plane --name Ground --scale 10,1,10' | Out-Null
            Invoke-Nova 'set "Main Camera" --position -40,3,-40 --rotation 30,45,0' | Out-Null
            Invoke-Nova 'window game' | Out-Null
            Mat 'Ref'
            Settle
            $ref = Shot "ref_$api"
            Mat 'VT'
            Settle
            $vtShot = Shot "vt_$api"
            $captures[$api] = $vtShot
            $i = Invoke-NovaJson 'vt info'
            $t = if ($i) { @($i.textures)[0] } else { $null }
            if ($api -eq 'dx11')
            {
                $total = if ($t) { [int]$t.pages } else { 0 }
                $resident = if ($t) { ($t.mips | Measure-Object -Property resident -Sum).Sum } else { 0 }
                $mip0 = if ($t) { [int]@($t.mips)[0].resident } else { 0 }
                Add-Result virtualtexture 'Virtual Texture Only: 4096 texture registered (6 mips, 1365 pages of 128), tile file built' ($t -and $t.size[0] -eq 4096 -and @($t.mips).Count -eq 6 -and $total -eq 1365) $(if ($t) { "size $($t.size -join 'x'), mips $(@($t.mips).Count), pages $total" } else { 'no virtual texture' })
                Add-Result virtualtexture 'streaming: feedback requested pages, only the visible ones are resident (mip 0 near the camera, a fraction of all pages)' ($mip0 -gt 0 -and $resident -lt $total * 0.5 -and [int]$i.uploads -gt 0) ("resident {0}/{1}, mip 0 resident {2}, uploads {3}, feedback objects {4}" -f $resident, $total, $mip0, $i.uploads, $i.feedbackObjects)
                $c = [NovaImageCompare]::Compare($ref, $vtShot, (Join-Path $dir 'vt_vs_ref_diff.png'))
                Add-Result virtualtexture 'picture: virtual texture looks like the same texture loaded whole (mean difference small)' ($c -and $c[1] -le 6.0 -and $c[2] -le 15.0) $(if ($c) { 'max {0}, mean {1:N2}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'capture missing' })
                # 비우기 → 피드백이 다시 채운다. 비운 직후를 보려면 피드백을 멈춘다 (CLI 호출 사이 몇 프레임에 벌써 다시 올라올 수 있다)
                Invoke-Nova 'vt set --frozen true' | Out-Null
                $fl = Invoke-NovaJson 'vt flush'
                $after = Invoke-NovaJson 'vt info'
                $mip0After = [int]@(@($after.textures)[0].mips)[0].resident
                Invoke-Nova 'vt set --frozen false' | Out-Null
                Settle
                $re = Invoke-NovaJson 'vt info'
                $mip0Re = [int]@(@($re.textures)[0].mips)[0].resident
                Add-Result virtualtexture 'vt flush evicts the streamed pages, the feedback loop loads them again' ($fl.evicted -gt 0 -and $mip0After -eq 0 -and $mip0Re -gt 0) ("evicted {0}, mip 0 after flush {1}, after frames {2}" -f $fl.evicted, $mip0After, $mip0Re)
                # 끄면 대체 (가장 거친 밉) — 그림이 흐려진다 = 가상 텍스처 길이 실제로 쓰였다
                Invoke-Nova 'vt set --enabled false' | Out-Null
                Settle
                $off = Shot 'vt_off'
                Invoke-Nova 'vt set --enabled true' | Out-Null
                $cOff = [NovaImageCompare]::Compare($ref, $off, $null)
                Add-Result virtualtexture 'vt set --enabled false falls back to the coarsest mip (blurry, far from the reference)' ($cOff -and $c -and $cOff[1] -gt $c[1] * 2) $(if ($cOff) { 'off: mean {0:N2} vs on: mean {1:N2}' -f $cOff[1], $c[1] } else { 'capture missing' })
            }
        }
        finally { Write-Host "  $api $(Stop-TestEditor $ed)" }
    }
    foreach ($api in 'gl', 'vk', 'dx12')
    {
        $c = if ($captures[$api] -and (Test-Path $captures[$api])) { [NovaImageCompare]::Compare($captures['dx11'], $captures[$api], (Join-Path $dir "vt_dx11_vs_$api.png")) } else { $null }
        Add-Result virtualtexture "$api virtual texture picture = DX11" ($c -and $c[1] -le 3.0) $(if ($c) { 'max {0}, mean {1:N2}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'capture missing' })
    }
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------------ Rendering Path = Deferred (렌더링 현대화 6 단계)
function Suite-Deferred
{
    # URP 처럼 Rendering Path (Forward · Forward+ · Deferred). 같은 장면 (Lit 상자 · 바닥 · 점광 12 개 + Unlit 상자) 을 Forward+ 와 Deferred 로 —
    #  그림이 거의 같고 (G-버퍼 → 전체 화면 조명이 포워드와 같은 ShadeLit), Render Graph 에 GBuffer · Deferred Lighting 패스가 생기고,
    #  Unlit (Forward Only) 상자도 그려진다. Forward = 클러스터 끔. OpenGL · Vulkan · DirectX 12 의 디퍼드 그림 = DX11
    Write-Host '[deferred]'
    $dir = Join-Path $Out 'deferred'
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $ic = [Globalization.CultureInfo]::InvariantCulture
    $assetDir = Join-Path $Project 'Assets\DeferredTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    $m = (Get-Content (Join-Path $Project 'Assets\Materials\Red Plastic.mat') -Raw | ConvertFrom-Json).PSObject.Copy()
    $m.Shader = 'Universal Render Pipeline/Unlit'; $m.BaseColor = @(0.1, 0.9, 0.2, 1); $m.ResourcePath = 'Assets\DeferredTest\Unlit.mat'
    $m | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $assetDir 'Unlit.mat')

    function DfScene
    {
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'set "Directional Light" --component Light --values "{\"intensity\":0.6}"' | Out-Null
        Invoke-Nova 'create plane --name Ground --scale 3,1,3' | Out-Null
        $mats = @('Red Plastic', 'Gold', 'Glossy Blue', 'Emissive', 'Rough Copper', 'Silver')
        for ($i = 0; $i -lt 6; $i++)
        {
            $px = (($i - 2.5) * 3).ToString($ic)
            Invoke-Nova "create cube --name Box$i --position $px,0.75,0 --scale 1.5,1.5,1.5" | Out-Null
            Invoke-Nova ('set Box' + $i + ' --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/Materials/' + $mats[$i] + '.mat\"]}"') | Out-Null
        }
        Invoke-Nova 'create cube --name UnlitBox --position 0,0.75,-4 --scale 1.5,1.5,1.5' | Out-Null
        Invoke-Nova 'set UnlitBox --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/DeferredTest/Unlit.mat\"]}"' | Out-Null
        $colors = @('1,0.3,0.2,1', '0.2,1,0.3,1', '0.3,0.5,1,1')
        for ($k = 0; $k -lt 12; $k++)
        {
            $px = ((($k % 6) - 2.5) * 3).ToString($ic); $pz = $(if ($k -lt 6) { '-2' } else { '2.5' })
            Invoke-Nova "create point-light --name L$k --position $px,1.2,$pz" | Out-Null
            Invoke-Nova ('set L' + $k + ' --component Light --values "{\"pointLightRange\":4,\"intensity\":2,\"pointLightDiffuse\":[' + $colors[$k % 3] + '],\"shadowType\":0}"') | Out-Null
        }
        Invoke-Nova 'set "Main Camera" --position 0,6,-13 --rotation 25,0,0' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
    }
    function DfShot([string]$name) { $f = Join-Path $dir "$name.png"; Invoke-Nova ('screenshot "' + $f + '" --view game') | Out-Null; $f }
    # Scene 뷰는 탭이 보여야 그린다 — 같은 자리에서 찍고 Game 으로 되돌린다
    function DfSceneShot([string]$name)
    {
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'camera --position 0,6,-13 --target 0,0.75,0' | Out-Null
        Invoke-Nova 'wait 6' | Out-Null
        $f = Join-Path $dir "$name.png"; Invoke-Nova ('screenshot "' + $f + '" --view scene') | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'wait 3' | Out-Null
        $f
    }
    function DfPath([string]$path) { $r = Invoke-NovaJson "renderpath set --path $path"; Invoke-Nova 'wait 4' | Out-Null; $r }
    function DfPasses([string]$view) { $g = Invoke-NovaJson "rendergraph info --view $view"; if ($g) { @($g.passes | Where-Object { -not $_.culled } | ForEach-Object { $_.name }) } else { @() } }
    function DfCmp([string]$a, [string]$b, [string]$diff) { if ((Test-Path $a) -and (Test-Path $b)) { [NovaImageCompare]::Compare($a, $b, $diff) } else { $null } }
    function DfFmt($c) { if ($c) { 'max {0}, mean {1:N2}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'capture missing' } }

    $captures = @{}
    $apis = if ($env:NOVA_DEFERRED_APIS) { $env:NOVA_DEFERRED_APIS -split ',' } else { @('dx11', 'gl', 'vk', 'dx12') }
    foreach ($api in $apis)
    {
        $ed = Start-TestEditor -OpenGL:($api -eq 'gl') -Vulkan:($api -eq 'vk') -D3D12:($api -eq 'dx12')
        try
        {
            Invoke-Nova 'autosave discard' | Out-Null
            DfPath 'Forward+' | Out-Null
            DfScene
            if ($api -eq 'dx11')
            {
                $fp = DfShot 'forwardplus'
                $fpScene = DfSceneShot 'forwardplus_scene'
                $passesFp = DfPasses 'Game'

                $r = DfPath 'Deferred'
                $df = DfShot 'deferred'
                $passes = DfPasses 'Game'
                $info = Invoke-NovaJson 'renderpath get'
                $gameView = if ($info) { @($info.deferred.views) | Where-Object { $_.view -eq 'Game' } } else { $null }
                $saved = Get-Content (Join-Path $Project 'ProjectSettings\GraphicsSettings.json') -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
                Add-Result deferred 'renderpath set --path Deferred: saved in Project Settings (Graphics), the G-buffer (4 targets) is drawn every frame' ($r.renderingPath -eq 'Deferred' -and $saved.renderingPath -eq 'Deferred' -and $info.deferred.available -and $gameView -and [int]$gameView.frames -gt 0) ("path {0}, saved {1}, available {2}, Game G-buffer {3} frames {4}" -f $r.renderingPath, $saved.renderingPath, $info.deferred.available, $(if ($gameView) { $gameView.size -join 'x' } else { '-' }), $(if ($gameView) { $gameView.frames } else { 0 }))
                $gi = [array]::IndexOf($passes, 'GBuffer'); $li = [array]::IndexOf($passes, 'Deferred Lighting'); $oi = [array]::IndexOf($passes, 'Opaque')
                Add-Result deferred 'Render Graph: GBuffer → Deferred Lighting → Opaque (Forward Only) in Deferred, no G-buffer passes in Forward+' ($gi -ge 0 -and $li -gt $gi -and $oi -gt $li -and -not ($passesFp -contains 'GBuffer')) ("Deferred: {0} | Forward+: {1}" -f ($passes -join ', '), ($passesFp -join ', '))
                $c = DfCmp $fp $df (Join-Path $dir 'deferred_vs_forwardplus_diff.png')
                Add-Result deferred 'Deferred picture = Forward+ picture (same lighting: shadows, 12 clustered lights, probes, emission; Unlit box drawn forward)' ($c -and $c[1] -le 1.5 -and $c[2] -le 1.0) (DfFmt $c)
                $dfScene = DfSceneShot 'deferred_scene'
                $info = Invoke-NovaJson 'renderpath get'
                $sceneView = if ($info) { @($info.deferred.views) | Where-Object { $_.view -eq 'Scene' } } else { $null }
                $c = DfCmp $fpScene $dfScene (Join-Path $dir 'deferred_vs_forwardplus_scene_diff.png')
                Add-Result deferred 'Scene view: Deferred (its own G-buffer) = Forward+' ($sceneView -and [int]$sceneView.frames -gt 0 -and $c -and $c[1] -le 1.5 -and $c[2] -le 1.0) ("Scene G-buffer frames {0}, {1}" -f $(if ($sceneView) { $sceneView.frames } else { 0 }), (DfFmt $c))

                # 디퍼드에서 Unlit (Forward Only) 상자를 Lit 로 바꾸면 G-버퍼로 간다 — 그래도 그림은 Forward+ 와 같아야 (G-버퍼 길이 실제로 그린다)
                Invoke-Nova 'set UnlitBox --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/Materials/Gold.mat\"]}"' | Out-Null
                Invoke-Nova 'wait 4' | Out-Null
                $dfGold = DfShot 'deferred_gold'
                DfPath 'Forward+' | Out-Null
                $fpGold = DfShot 'forwardplus_gold'
                $c = DfCmp $fpGold $dfGold $null
                $cUnlit = DfCmp $fpGold $fp $null
                Add-Result deferred 'Unlit box (Forward Only) vs Lit box (G-buffer): both match Forward+, and they differ from each other (the forward-only object really was drawn)' ($c -and $c[1] -le 1.5 -and $cUnlit -and $cUnlit[1] -gt $c[1] + 0.3) ("Lit in deferred vs forward+: {0} | unlit vs lit picture mean {1:N2}" -f (DfFmt $c), $(if ($cUnlit) { $cUnlit[1] } else { 0 }))
                Invoke-Nova 'set UnlitBox --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/DeferredTest/Unlit.mat\"]}"' | Out-Null

                # Forward = 클러스터 끔 (앞의 빛 4 개만) — 점광 12 개 중 대부분이 꺼져 그림이 달라진다
                $r = DfPath 'Forward'
                $fw = DfShot 'forward'
                $fpInfo = Invoke-NovaJson 'forwardplus info'
                $c = DfCmp $fp $fw $null
                Add-Result deferred 'Rendering Path Forward turns the Forward+ clusters off (fewer lights, picture differs)' ($r.renderingPath -eq 'Forward' -and $fpInfo -and -not $fpInfo.enabled -and $c -and $c[1] -gt 1.5) ("path {0}, clusters enabled {1}, Forward vs Forward+ mean {2:N2}" -f $r.renderingPath, $fpInfo.enabled, $(if ($c) { $c[1] } else { 0 }))
                DfPath 'Deferred' | Out-Null
            }
            else
            {
                DfPath 'Deferred' | Out-Null
            }
            Invoke-Nova 'wait 4' | Out-Null
            $captures[$api] = DfShot "deferred_$api"
            $passes = DfPasses 'Game'
            if ($api -ne 'dx11') { Add-Result deferred "$api Render Graph has the G-buffer passes" (($passes -contains 'GBuffer') -and ($passes -contains 'Deferred Lighting')) ($passes -join ', ') }
            DfPath 'Forward+' | Out-Null
            Invoke-Nova 'window scene' | Out-Null
            Invoke-Nova 'scene new --force' | Out-Null
        }
        finally { Write-Host "  $api $(Stop-TestEditor $ed)" }
    }
    foreach ($api in $apis | Where-Object { $_ -ne 'dx11' })
    {
        $c = if ($captures['dx11'] -and $captures[$api]) { DfCmp $captures['dx11'] $captures[$api] (Join-Path $dir "deferred_dx11_vs_$api.png") } else { $null }
        Add-Result deferred "$api deferred picture = DX11" ($c -and $c[1] -le 3.0) (DfFmt $c)
    }
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------------ Job System (동시성 로드맵 1 단계)
function Suite-Jobs
{
    # 작업 훔치기 Job System + 무잠금 자료 구조: 편집기 안에서 C++ 스트레스 검사 (nova jobs test) —
    #  Chase-Lev 덱 · Vyukov MPMC · SPSC · 삼중 버퍼 (정확히 한 번 · 순서 · 찢김 없음), 잡 20 만 · ParallelFor · 잡 안의 Wait (파이버) · 깊은 나무 · Background.
    #  파이버 켬 · 끔 (NOVA_JOB_FIBERS=0 — 돕기), 인라인 (일꾼 없이) 모두. ParallelFor 가 빨라지는지 · Profiler Timeline 에 일꾼 줄
    Write-Host '[jobs]'
    foreach ($mode in 'fibers', 'nofibers')
    {
        if ($mode -eq 'nofibers') { $env:NOVA_JOB_FIBERS = '0' }
        $ed = Start-TestEditor
        try
        {
            $info = Invoke-NovaJson 'jobs info'
            $hw = [Environment]::ProcessorCount
            $expectFibers = $mode -eq 'fibers'
            Add-Result jobs "${mode}: job system started (workers = cores - 1, fibers $expectFibers)" ($info -and $info.workers -eq ($hw - 1) -and [bool]$info.fibers -eq $expectFibers) $(if ($info) { "workers $($info.workers) of $hw hardware threads, fibers $($info.fibers), fiber pool $($info.fiberPool)" } else { 'no reply' })
            $t = Invoke-NovaJson 'jobs test --kind all'
            foreach ($r in @($t.tests))
            {
                Add-Result jobs ("${mode}: " + $r.name) ([bool]$r.ok) ('{0} ({1:N1} ms)' -f $r.detail, [double]$r.ms)
            }
            if (-not $t) { Add-Result jobs "${mode}: jobs test" $false 'no reply' }
            if ($mode -eq 'fibers')
            {
                $b = Invoke-NovaJson 'jobs bench'
                $need = [math]::Min(2.0, ($hw - 1) * 0.5)
                Add-Result jobs 'ParallelFor is faster than a serial loop (heavy math, 400k items)' ($b -and [double]$b.speedup -ge $need) $(if ($b) { 'serial {0:N1} ms, parallel {1:N1} ms, speedup {2:N2}x on {3} threads (need {4:N1}x), empty job {5:N0} ns' -f $b.serialMs, $b.parallelMs, $b.speedup, $b.threads, $need, $b.emptyJobNs } else { 'no reply' })

                # 인라인 (일꾼 없이 부른 자리에서): 결과는 같다
                Invoke-Nova 'jobs set --inline true' | Out-Null
                $ti = Invoke-NovaJson 'jobs test --kind jobs'
                $tp = Invoke-NovaJson 'jobs test --kind tree'
                Invoke-Nova 'jobs set --inline false' | Out-Null
                Add-Result jobs 'jobs set --inline true runs every job on the caller (same results)' ($ti -and $ti.ok -and $ti.inline -and $tp -and $tp.ok) $(if ($ti) { "$(@($ti.tests)[0].detail) | $(@($tp.tests)[0].detail)" } else { 'no reply' })

                # Profiler: 일꾼 스레드의 구간이 Timeline 에 (스레드 줄) — 프레임마다 합성 ParallelFor (jobs set --load)
                Invoke-Nova 'jobs set --load 20000' | Out-Null
                $p = Invoke-NovaJson 'perf --frames 30'
                Invoke-Nova 'jobs set --load 0' | Out-Null
                $workerRows = if ($p -and $p.threads) { @($p.threads | Where-Object { $_.name -like 'Job Worker*' -and $_.samples -gt 0 }).Count } else { 0 }
                Add-Result jobs 'Profiler records job worker threads (Timeline rows)' ($workerRows -gt 0) $(if ($p -and $p.threads) { ($p.threads | ForEach-Object { "$($_.name): $($_.samples)" }) -join ', ' } else { 'no thread data' })
            }
        }
        finally
        {
            Write-Host "  $mode $(Stop-TestEditor $ed)"
            Remove-Item Env:NOVA_JOB_FIBERS -ErrorAction SilentlyContinue
        }
    }
}

# ------------------------------------------------------------------ 물리 ↔ 렌더링 겹치기 (동시성 로드맵 3 단계)
function Suite-PhysicsAsync
{
    # Project Settings > Physics > Simulate During Rendering: 프레임의 마지막 고정 스텝을 일꾼에서 (핑퐁 버퍼), 다음 프레임 시작에 적용.
    #  같은 장면 (공 하나 · 상자 다섯 쌓기) 이 켬 · 끔에서 같은 자리에 멈추고, 상자 800 개가 떨어질 때 메인 스레드의 물리 시간이 준다
    #  (기존 물리 스위트는 NOVA_PHYSICS_ASYNC=1 로 겹치기 모드에서도 돌린다 — run_tests 바깥에서)
    Write-Host '[physicsasync]'
    $ic = [Globalization.CultureInfo]::InvariantCulture
    $ed = Start-TestEditor
    try
    {
        Invoke-Nova 'autosave discard' | Out-Null
        $r = Invoke-NovaJson 'physics --async true'
        $file = Get-Content (Join-Path $Project 'ProjectSettings\PhysicsSettings.json') -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
        Add-Result physicsasync 'nova physics --async true: Simulate During Rendering saved in Project Settings' ($r -and $r.asyncSimulation -and $file.asyncSimulation) "setting $($r.asyncSimulation), file $($file.asyncSimulation)"

        function PaScene
        {
            Invoke-Nova 'scene new --force' | Out-Null
            Invoke-Nova 'create cube --name PaGround --position 0,-0.5,0 --scale 30,1,30' | Out-Null
            Invoke-Nova 'create sphere --name PaBall --position 0,6,0' | Out-Null
            Invoke-Nova 'add-component PaBall RigidBody' | Out-Null
            for ($i = 0; $i -lt 5; $i++)
            {
                $y = (0.5 + $i * 1.0).ToString($ic)
                Invoke-Nova "create cube --name PaS$i --position 4,$y,0" | Out-Null
                Invoke-Nova "add-component PaS$i RigidBody" | Out-Null
            }
        }
        function PaRun([bool]$async)
        {
            Invoke-Nova ('physics --async ' + $(if ($async) { 'true' } else { 'false' })) | Out-Null
            PaScene
            Invoke-Nova 'play' | Out-Null
            $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 3.0) { Invoke-Nova 'wait 20' | Out-Null }
            $o = @{ Ball = [double](Invoke-NovaJson 'get PaBall').position[1]; Top = [double](Invoke-NovaJson 'get PaS4').position[1]; Info = (Invoke-NovaJson 'physics').async }
            Invoke-Nova 'stop' | Out-Null
            Invoke-Nova 'wait 5' | Out-Null
            $o
        }
        $a = PaRun $true
        $s = PaRun $false
        Add-Result physicsasync 'same rest positions with and without overlap (ball on the ground, 5-box stack standing)' ([math]::Abs($a.Ball - 0.5) -lt 0.02 -and [math]::Abs($s.Ball - 0.5) -lt 0.02 -and [math]::Abs($a.Top - 4.5) -lt 0.05 -and [math]::Abs($a.Top - $s.Top) -lt 0.02) ("ball y {0:F3} / {1:F3}, stack top y {2:F3} / {3:F3} (overlap / main thread)" -f $a.Ball, $s.Ball, $a.Top, $s.Top)
        $ai = $a.Info
        Add-Result physicsasync 'steps were simulated on a worker and applied at the next frame start' ($ai -and [int]$ai.asyncSteps -gt 30 -and [int]$ai.completedAtFrameStart -gt [int]$ai.completedEarly) $(if ($ai) { "async steps $($ai.asyncSteps), applied at frame start $($ai.completedAtFrameStart), early (physics API) $($ai.completedEarly), wait $('{0:N1}' -f [double]$ai.waitMs) ms total" } else { 'no info' })

        # 상자 800 개: 메인 스레드의 Physics.Update (+ 프레임 시작의 적용) 가 준다
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create cube --name PbGround --position 0,-0.5,0 --scale 60,1,60' | Out-Null
        for ($k = 0; $k -lt 800; $k++)
        {
            $x = ((($k % 10) - 4.5) * 1.2).ToString($ic); $z = (([math]::Floor($k / 10) % 10 - 4.5) * 1.2).ToString($ic); $y = (1 + [math]::Floor($k / 100) * 1.3).ToString($ic)
            Invoke-Nova "create cube --name Pb$k --position $x,$y,$z --scale 0.8,0.8,0.8" | Out-Null
            Invoke-Nova "add-component Pb$k RigidBody" | Out-Null
        }
        function PbMeasure([bool]$async)
        {
            Invoke-Nova ('physics --async ' + $(if ($async) { 'true' } else { 'false' })) | Out-Null
            Invoke-Nova 'wait 10' | Out-Null
            $p = Invoke-NovaJson 'perf --frames 40 --depth 1'
            $main = 0.0
            foreach ($c in @($p.cpuScopes)) { if ($c.scope.TrimStart('.') -in 'Physics.Update', 'Physics.CompleteAsync') { $main += [double]$c.ms } }
            $main
        }
        Invoke-Nova 'play' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null
        $on = @(); $off = @()
        for ($i = 0; $i -lt 2; $i++) { $on += PbMeasure $true; $off += PbMeasure $false }
        Invoke-Nova 'stop' | Out-Null
        $onM = ($on | Measure-Object -Minimum).Minimum; $offM = ($off | Measure-Object -Minimum).Minimum
        Add-Result physicsasync '800 falling boxes: less physics time on the main thread with overlap' ($onM -lt $offM * 0.8) ("main-thread physics {0:N2} ms (overlap) vs {1:N2} ms (main thread)" -f $onM, $offM)

        Invoke-Nova 'physics --async false' | Out-Null   # 설정을 되돌린다
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally
    {
        Invoke-Nova 'physics --async false' | Out-Null
        Write-Host "  $(Stop-TestEditor $ed)"
    }
}

# ------------------------------------------------------------------ 렌더 스레드 (동시성 로드맵 4 단계)
function Suite-RenderThread
{
    # Multithreaded Rendering (DirectX 11 · 12 · Vulkan). DirectX 11: 메인은 deferred context 에 기록, 렌더 스레드가 실행 · Present.
    #  같은 장면이 켬 · 끔에서 같은 그림, 읽기 (캡처 = Sync) · Profiler GPU 시간 (쿼리) 이 되고, 설정이 저장된다
    #  (다른 그래픽 스위트는 NOVA_RENDER_THREAD=1 로 렌더 스레드 모드에서도 돌린다)
    Write-Host '[renderthread]'
    $dir = Join-Path $Out 'renderthread'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $ed = Start-TestEditor -WatchSeconds 240
    try
    {
        Invoke-Nova 'autosave discard' | Out-Null
        $r = Invoke-NovaJson 'renderthread set --enabled true'
        $saved = Get-Content (Join-Path $Project 'ProjectSettings\GraphicsSettings.json') -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
        Invoke-Nova 'wait 20' | Out-Null
        $i = Invoke-NovaJson 'renderthread info'
        Add-Result renderthread 'renderthread set --enabled true: saved (Multithreaded Rendering), frames executed + presented on the render thread' ($r -and $r.enabled -and $saved.multithreadedRendering -and [int]$i.frames -ge 15 -and [int]$i.completed -ge [int]$i.frames - 1) $(if ($i) { "frames $($i.frames), command lists $($i.commandLists), completed $($i.completed), execute $('{0:N3}' -f [double]$i.executeMsPerFrame) ms, present $('{0:N2}' -f [double]$i.presentMsPerFrame) ms per frame" } else { 'no reply' })

        foreach ($s in 'Materials', 'Forest', 'CityShowcase')
        {
            Invoke-Nova "scene open Assets/Scenes/$s.scene --force" | Out-Null
            Invoke-Nova 'window game' | Out-Null
            Invoke-Nova 'renderthread set --enabled true' | Out-Null
            Invoke-Nova 'wait 40' | Out-Null
            $on = Join-Path $dir "${s}_on.png"; Invoke-Nova ('screenshot "' + $on + '" --view game') | Out-Null
            Invoke-Nova 'renderthread set --enabled false' | Out-Null
            Invoke-Nova 'wait 40' | Out-Null
            $off = Join-Path $dir "${s}_off.png"; Invoke-Nova ('screenshot "' + $off + '" --view game') | Out-Null
            $c = if ((Test-Path $on) -and (Test-Path $off)) { [NovaImageCompare]::Compare($off, $on, (Join-Path $dir "${s}_diff.png")) } else { $null }
            Add-Result renderthread "${s}: same picture with the render thread on and off" ($c -and $c[1] -le 1.0 -and $c[2] -le 1.0) $(if ($c) { 'max {0}, mean {1:N2}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'capture missing' })
        }

        # 읽기 · 쿼리: 켠 채 Profiler GPU 시간 (타임스탬프 쿼리 결과) 이 오고, 캡처가 Sync 로 된다
        Invoke-Nova 'renderthread set --enabled true' | Out-Null
        Invoke-Nova 'renderthread reset' | Out-Null
        $p = Invoke-NovaJson 'perf --frames 40'
        $shot = Join-Path $dir 'readback.png'; Invoke-Nova ('screenshot "' + $shot + '" --view game') | Out-Null
        $i = Invoke-NovaJson 'renderthread info'
        Add-Result renderthread 'GPU timestamp queries and readbacks work with the render thread (Profiler GPU ms, capture = sync)' ($p -and [double]$p.gpuMs -gt 0 -and (Test-Path $shot) -and [int]$i.syncs -ge 1) $(if ($p) { "GPU $($p.gpuMs) ms, frame $($p.frameMs) ms, syncs $($i.syncs), flushes $($i.flushes)" } else { 'no perf' })

        Invoke-Nova 'renderthread set --enabled false' | Out-Null
        $saved = Get-Content (Join-Path $Project 'ProjectSettings\GraphicsSettings.json') -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
        Add-Result renderthread 'renderthread set --enabled false: back to the immediate context, setting saved off' (-not $saved.multithreadedRendering) "saved $($saved.multithreadedRendering)"
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally
    {
        Invoke-Nova 'renderthread set --enabled false' | Out-Null
        Write-Host "  $(Stop-TestEditor $ed)"
    }

    # DirectX 12 · Vulkan: 기록은 메인, 그래픽 큐 작업 (제출 · 신호 · Present) 을 렌더 스레드가.
    #  같은 그림, 프레임마다 Present 가 렌더 스레드에서, 읽기 (캡처 · GPU 시간) · Play → Stop · 켬 · 끔 반복이 된다
    foreach ($api in 'd3d12', 'vulkan')
    {
        $name = if ($api -eq 'd3d12') { 'DirectX 12' } else { 'Vulkan' }
        $ed = Start-TestEditor -WatchSeconds 240 -D3D12:($api -eq 'd3d12') -Vulkan:($api -eq 'vulkan')
        try
        {
            Invoke-Nova 'autosave discard' | Out-Null
            $r = Invoke-NovaJson 'renderthread set --enabled true'
            Invoke-Nova 'renderthread reset' | Out-Null
            Invoke-Nova 'wait 30' | Out-Null
            $i = Invoke-NovaJson 'renderthread info'
            $q = $i.queue
            Add-Result renderthread "${name}: queue submits + present run on the render thread" ($r -and $r.enabled -and $i.mode -eq 'queue' -and [int]$q.presents -ge 25 -and [int]$q.submits -ge [int]$q.presents) $(if ($q) { "frames $($q.frames), submits $($q.submits), presents $($q.presents), per frame: submit $('{0:N3}' -f [double]$i.submitMsPerFrame) ms, present $('{0:N3}' -f [double]$i.presentMsPerFrame) ms, main waited $('{0:N3}' -f [double]$i.mainWaitMsPerFrame) ms" } else { 'no reply' })

            foreach ($s in 'Materials', 'CityShowcase')
            {
                Invoke-Nova "scene open Assets/Scenes/$s.scene --force" | Out-Null
                Invoke-Nova 'window game' | Out-Null
                Invoke-Nova 'renderthread set --enabled true' | Out-Null
                Invoke-Nova 'wait 40' | Out-Null
                $on = Join-Path $dir "${api}_${s}_on.png"; Invoke-Nova ('screenshot "' + $on + '" --view game') | Out-Null
                Invoke-Nova 'renderthread set --enabled false' | Out-Null
                Invoke-Nova 'wait 40' | Out-Null
                $off = Join-Path $dir "${api}_${s}_off.png"; Invoke-Nova ('screenshot "' + $off + '" --view game') | Out-Null
                $c = if ((Test-Path $on) -and (Test-Path $off)) { [NovaImageCompare]::Compare($off, $on, (Join-Path $dir "${api}_${s}_diff.png")) } else { $null }
                Add-Result renderthread "${name} ${s}: same picture with the render thread on and off" ($c -and $c[1] -le 1.0 -and $c[2] -le 1.0) $(if ($c) { 'max {0}, mean {1:N2}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'capture missing' })
            }

            # 읽기 · 쿼리 · Play: 켠 채 GPU 시간이 오고, 캡처가 되고, Play → Stop 뒤에도 프레임이 렌더 스레드에서 나간다
            Invoke-Nova 'renderthread set --enabled true' | Out-Null
            $p = Invoke-NovaJson 'perf --frames 40'
            $shot = Join-Path $dir "${api}_readback.png"; Invoke-Nova ('screenshot "' + $shot + '" --view game') | Out-Null
            Invoke-Nova 'play' | Out-Null
            Invoke-Nova 'wait 30' | Out-Null
            Invoke-Nova 'stop' | Out-Null
            Invoke-Nova 'renderthread reset' | Out-Null
            Invoke-Nova 'wait 20' | Out-Null
            $i = Invoke-NovaJson 'renderthread info'
            Add-Result renderthread "${name}: GPU timestamps, capture and Play -> Stop work with the render thread" ($p -and [double]$p.gpuMs -gt 0 -and (Test-Path $shot) -and $i.enabled -and [int]$i.queue.presents -ge 10) $(if ($p) { "GPU $($p.gpuMs) ms, frame $($p.frameMs) ms, presents after stop $($i.queue.presents)" } else { 'no perf' })
            Invoke-Nova 'renderthread set --enabled false' | Out-Null
            Invoke-Nova 'window scene' | Out-Null
            Invoke-Nova 'scene new --force' | Out-Null
        }
        finally
        {
            Invoke-Nova 'renderthread set --enabled false' | Out-Null
            Write-Host "  $(Stop-TestEditor $ed)"
        }
    }
}

# ------------------------------------------------------------------ 메모리 알로케이터 · 씬 힙
function Suite-Memory
{
    # Linear · Stack · Pool · ConcurrentPool · Buddy 스트레스 검사, 씬 힙 (남은 힙 → 돌려줌), 거짓 공유 측정, 속도 (nova memory test).
    Write-Host '[memory]'
    $ed = Start-TestEditor
    try
    {
        $t = Invoke-NovaJson 'memory test --kind all'
        foreach ($r in @($t.tests)) { Add-Result memory $r.name ([bool]$r.ok) ('{0} ({1:N1} ms)' -f $r.detail, [double]$r.ms) }
        if (-not $t) { Add-Result memory 'memory test' $false 'no reply' }

        # 씬 전환: 씬마다 힙 — 전환할 때 앞 씬의 힙이 한 덩어리로 합쳐져 (단편화 0) 돌려지고, 남은 힙 (누수) 이 없다
        Invoke-Nova 'autosave discard' | Out-Null
        function MemInfo { Invoke-NovaJson 'memory info' }
        $seq = @('CityShowcase', 'Forest', 'Materials', 'CityShowcase', 'Forest', 'Materials', 'CityShowcase', 'Forest')
        $badRelease = 0; $lingering = 0; $private = @(); $released0 = [int](MemInfo).released; $cityLive = 0
        foreach ($s in $seq)
        {
            Invoke-Nova "scene open Assets/Scenes/$s.scene --force" | Out-Null
            Invoke-Nova 'wait 5' | Out-Null
            $m = MemInfo
            if (-not $m.lastReleased.coalescedToOneBlock -or [double]$m.lastReleased.fragmentation -ne 0) { $badRelease++ }
            $lingering = [math]::Max($lingering, @($m.heaps | Where-Object { $_.lingering }).Count)
            $private += [double]$m.processPrivateMB
            if ($s -eq 'CityShowcase') { $cityLive = [int](@($m.heaps | Where-Object { $_.active })[0].liveAllocations) }
        }
        $m = MemInfo
        $released = [int]$m.released - $released0
        Add-Result memory 'scene transitions: every left scene heap coalesced to one block (fragmentation 0) and was returned, no lingering heaps' ($released -ge $seq.Count -and $badRelease -eq 0 -and $lingering -eq 0 -and $cityLive -gt 1000) ("{0} switches, {1} heaps released, bad releases {2}, max lingering heaps {3}, city heap live allocations {4}, fallbacks {5}" -f $seq.Count, $released, $badRelease, $lingering, $cityLive, $m.fallbacks)
        # 프로세스 메모리가 전환을 거듭해도 자라지 않는다 (첫 바퀴 뒤 = 캐시가 다 찬 뒤를 기준으로)
        $growth = $private[$private.Count - 1] - $private[2]
        Add-Result memory 'process private memory does not grow with repeated scene switches (after the first round)' ($growth -lt 40) ("private MB per switch: {0}; growth after first round {1:N1} MB" -f (($private | ForEach-Object { '{0:N0}' -f $_ }) -join ', '), $growth)

        # Play → Stop: Play 의 씬 (복제) 힙도 돌려진다
        Invoke-Nova 'scene open Assets/Scenes/CityShowcase.scene --force' | Out-Null
        $before = [int](MemInfo).released
        Invoke-Nova 'play' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $m = MemInfo
        Add-Result memory 'Play → Stop: the play-mode scene heap is returned, nothing lingers' (([int]$m.released -gt $before) -and @($m.heaps | Where-Object { $_.lingering }).Count -eq 0) ("released during play/stop {0}, lingering {1}, last '{2}' one block {3}" -f ([int]$m.released - $before), @($m.heaps | Where-Object { $_.lingering }).Count, $m.lastReleased.name, $m.lastReleased.coalescedToOneBlock)
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

# ------------------------------------------------------------------ 데이터 지향 Transform · 컬링
function Suite-Transform
{
    # Transform SoA 저장소 (늦은 계산 · 프레임마다 병렬 Flush) 와 컬링 (렌더러 목록 · 월드 번호로 건너뛰기 · 옥트리 칸 재사용 · SIMD).
    #  nova transform test: 무작위 계층 조작이 예전 방식 (즉시 계산) 과 같은 값 · 컬링이 전부 직접 검사와 같은 결과
    Write-Host '[transform]'
    $ed = Start-TestEditor -WatchSeconds 600
    try
    {
        Invoke-Nova 'autosave discard' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        $t = Invoke-NovaJson 'transform test'
        foreach ($r in @($t.tests)) { Add-Result transform $r.name ([bool]$r.ok) ('{0} ({1:N0} ms)' -f $r.detail, [double]$r.ms) }
        if (-not $t) { Add-Result transform 'transform test' $false 'no reply' }

        $b = Invoke-NovaJson 'transform bench --roots 500 --children 9 --depth 1 --frames 10'
        Add-Result transform 'bench (5000 cubes): still frames skip unchanged renderers — culling update far cheaper than with everything moving' ($b -and [double]$b.cullUpdateStaticMs * 3 -lt [double]$b.cullUpdateMovedMs -and [int]$b.visible -gt 0) $(if ($b) { 'move roots {0:N2} ms + flush {1:N2} ms, culling update moved {2:N2} / still {3:N2} ms, frustum test {4:N2} ms, visible {5}' -f [double]$b.moveRootsMs, [double]$b.flushMs, [double]$b.cullUpdateMovedMs, [double]$b.cullUpdateStaticMs, [double]$b.cullMs, $b.visible } else { 'no reply' })

        # 실제 장면: Transform 이 저장소에 있고, 프레임마다 바뀌지 않은 렌더러는 건너뛴다
        Invoke-Nova 'scene open Assets/Scenes/CityShowcase.scene --force' | Out-Null
        Invoke-Nova 'window game' | Out-Null
        Invoke-Nova 'wait 30' | Out-Null
        $i = Invoke-NovaJson 'transform info'
        Add-Result transform 'CityShowcase: transforms live in the SoA store, culling skips unchanged renderers every frame' ($i -and [int]$i.transforms -gt 100 -and [int64]$i.culling.fastSkips -gt 0 -and [int]$i.culling.tracked -gt 50) $(if ($i) { "transforms $($i.transforms), flushes $($i.flushes), culling: renderers $($i.culling.renderers), tracked $($i.culling.tracked), skipped (unchanged) $($i.culling.fastSkips), recomputed $($i.culling.updated)" } else { 'no reply' })
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

# ------------------------------------------------------------------ 씬 · 에셋 스트리밍 (LoadSceneAsync)
function Suite-Streaming
{
    # SceneManager.LoadSceneAsync 가 프레임을 멈추지 않게: 새 씬을 프레임마다 예산 (backgroundLoadingPriority) 만큼 미리 짓고 (꺼진 채 — 지금 씬에 끼어들지 않게),
    #  처음 쓸 때 만드는 캐시 (Animator Humanoid · 나무 메시 · 잎 텍스처) 를 백그라운드 잡에서, 텍스처 디코드도 미리. 바꿔 끼우는 프레임은 Awake · Start · 물리만
    Write-Host '[streaming]'
    $dir = Join-Path $Out 'streaming'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $ed = Start-TestEditor -WatchSeconds 900
    try
    {
        function WaitOp([int]$op, [double]$until = 1.0)
        {
            $st = $null
            for ($k = 0; $k -lt 600; $k++)
            {
                Invoke-Nova 'wait 2' | Out-Null
                $st = Invoke-NovaJson "scenestream status --id $op"
                if ($st -and (($until -ge 1.0 -and $st.done) -or ($until -lt 1.0 -and [double]$st.progress -ge $until))) { break }
            }
            return $st
        }
        Invoke-Nova 'autosave discard' | Out-Null
        Invoke-Nova 'window game' | Out-Null

        # 1) 텍스처 미리 디코드: 텍스처를 쓰는 씬 (Materials) 을 처음 불러올 때 — 작업 스레드가 디코드, 메인은 올리기만
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'play' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null
        $p0 = (Invoke-NovaJson 'scenestream stats').prefetch
        $r = Invoke-NovaJson 'scenestream load --path Assets/Scenes/Materials.scene'
        $st = WaitOp $r.op
        Invoke-Nova 'wait 60' | Out-Null
        $p1 = (Invoke-NovaJson 'scenestream stats').prefetch
        $ms = Join-Path $dir 'materials_streamed.png'; Invoke-Nova ('screenshot "' + $ms + '" --view game') | Out-Null
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        Invoke-Nova 'scene open Assets/Scenes/Materials.scene --force' | Out-Null
        Invoke-Nova 'play' | Out-Null
        Invoke-Nova 'wait 40' | Out-Null
        $before = Join-Path $dir 'before.png'; Invoke-Nova ('screenshot "' + $before + '" --view game') | Out-Null
        $c3 = if ((Test-Path $before) -and (Test-Path $ms)) { [NovaImageCompare]::Compare($before, $ms, (Join-Path $dir 'materials_diff.png')) } else { $null }
        $used = [int]$p1.texturesUsed - [int]$p0.texturesUsed
        Add-Result streaming 'textures referenced by the scene and its materials are decoded on worker threads (the main thread only uploads)' ($used -ge 1 -and $c3 -and $c3[1] -le 0.2) "prefetched textures used $used (requested $([int]$p1.texturesRequested - [int]$p0.texturesRequested), decoded by the main thread instead $([int]$p1.claimedByMain - [int]$p0.claimedByMain)); streamed Materials vs opened: mean $(if ($c3) { '{0:N3}' -f $c3[1] } else { '?' })"

        # 2) (Materials 를 Play 하는 중) 도시를 allowSceneActivation = false 로: 0.9 까지 미리 짓고 기다린다 — 그동안 지금 씬 (Materials) 은 그대로
        $r = Invoke-NovaJson 'scenestream load --path Assets/Scenes/CityShowcase.scene --allow false'
        $st = WaitOp $r.op 0.899
        Invoke-Nova 'wait 20' | Out-Null
        $staged = Join-Path $dir 'staged.png'; Invoke-Nova ('screenshot "' + $staged + '" --view game') | Out-Null
        $st = Invoke-NovaJson "scenestream status --id $($r.op)"
        $c = if ((Test-Path $before) -and (Test-Path $staged)) { [NovaImageCompare]::Compare($before, $staged, (Join-Path $dir 'staged_diff.png')) } else { $null }
        Add-Result streaming 'LoadSceneAsync builds the next scene over many frames and waits at 0.9 (allowSceneActivation = false)' ($st -and -not $st.done -and [math]::Abs([double]$st.progress - 0.9) -lt 0.001 -and [int]$st.stageFrames -ge 10 -and [int]$st.roots -gt 1000) $(if ($st) { "progress $($st.progress), $($st.roots) roots built in $($st.stageFrames) frames ($('{0:N0}' -f [double]$st.stageMs) ms, longest $('{0:N1}' -f [double]$st.stageMaxMs) ms)" } else { 'no reply' })
        Add-Result streaming 'while the next scene is being built the current scene is unchanged (its trees, cameras, lights stay out)' ($c -and $c[1] -le 0.1 -and $c[2] -le 0.05) $(if ($c) { 'max {0}, mean {1:N3}, >8: {2:N2}%' -f $c[0], $c[1], $c[2] } else { 'capture missing' })

        # 3) 바꿔 끼우기: 짓기는 끝나 있어 그 프레임은 Awake · Start · 물리 바디만
        Invoke-Nova "scenestream allow --id $($r.op) --allow true" | Out-Null
        $st = WaitOp $r.op
        Invoke-Nova 'wait 60' | Out-Null
        $st = Invoke-NovaJson "scenestream status --id $($r.op)"
        Add-Result streaming 'activation frame only enters the prebuilt scene (no object building, Start uses prewarmed caches)' ($st -and $st.done -and -not $st.failed -and [double]$st.buildMs -lt 1.0 -and [int]$st.objects -gt 2000 -and [double]$st.activateMs -lt 400) $(if ($st) { "activate $('{0:N1}' -f [double]$st.activateMs) ms (build $('{0:N2}' -f [double]$st.buildMs), enter $('{0:N1}' -f [double]$st.enterMs), teardown $('{0:N1}' -f [double]$st.teardownMs)), objects $($st.objects), next frame $('{0:N0}' -f [double]$st.frameAfterMs) ms" } else { 'no reply' })
        $streamed = Join-Path $dir 'city_streamed.png'; Invoke-Nova ('screenshot "' + $streamed + '" --view game') | Out-Null
        $mem = Invoke-NovaJson 'memory info'
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        Invoke-Nova 'scene open Assets/Scenes/CityShowcase.scene --force' | Out-Null
        Invoke-Nova 'play' | Out-Null
        Invoke-Nova 'wait 80' | Out-Null
        $direct = Join-Path $dir 'city_direct.png'; Invoke-Nova ('screenshot "' + $direct + '" --view game') | Out-Null
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'wait 10' | Out-Null
        $c2 = if ((Test-Path $direct) -and (Test-Path $streamed)) { [NovaImageCompare]::Compare($direct, $streamed, (Join-Path $dir 'city_diff.png')) } else { $null }
        Add-Result streaming 'streamed city looks like the city opened directly' ($c2 -and $c2[1] -le 0.2 -and $c2[2] -le 0.5) $(if ($c2) { 'max {0}, mean {1:N3}, >8: {2:N2}%' -f $c2[0], $c2[1], $c2[2] } else { 'capture missing' })
        Add-Result streaming 'streamed scene lives in its own heap (the old scene heap is returned, nothing lingers)' ($mem -and @($mem.heaps | Where-Object { $_.lingering }).Count -eq 0 -and @($mem.heaps | Where-Object { $_.name -eq 'CityShowcase' -and [int]$_.liveAllocations -gt 1000 }).Count -ge 1) $(if ($mem) { "heaps: $(($mem.heaps | ForEach-Object { "$($_.name) $($_.liveAllocations)" }) -join ', '); lingering $(@($mem.heaps | Where-Object { $_.lingering }).Count)" } else { 'no reply' })

        # 4) Application.backgroundLoadingPriority (Play 하며)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'play' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null   # (Unity ThreadPriority) High 는 적은 프레임에, Low 는 많은 프레임에 나눠 짓는다
        $pf = Join-Path $dir 'priority.cs'
        'Application.backgroundLoadingPriority = ThreadPriority.High; return ((int)Application.backgroundLoadingPriority).ToString();' | Set-Content -Encoding utf8 $pf
        $pr = Invoke-NovaJson "exec --file `"$pf`""
        $r = Invoke-NovaJson 'scenestream load --path Assets/Scenes/Forest.scene'
        $hi = WaitOp $r.op
        Invoke-Nova 'scenestream priority --value 0' | Out-Null
        $r = Invoke-NovaJson 'scenestream load --path Assets/Scenes/CityShowcase.scene'
        $lo = WaitOp $r.op
        Invoke-Nova 'scenestream priority --value 4' | Out-Null
        $r = Invoke-NovaJson 'scenestream load --path Assets/Scenes/CityShowcase.scene'
        $hi2 = WaitOp $r.op
        Add-Result streaming 'backgroundLoadingPriority: C# property reaches the engine, High (50 ms) builds in fewer frames than Low (2 ms)' ($pr -and "$($pr.result)" -eq '4' -and $lo -and $hi2 -and [int]$hi2.stageFrames -lt [int]$lo.stageFrames) "C# set High -> $($pr.result); city build frames: Low $($lo.stageFrames) (longest $('{0:N1}' -f [double]$lo.stageMaxMs) ms), High $($hi2.stageFrames) (longest $('{0:N1}' -f [double]$hi2.stageMaxMs) ms)"

        # 5) Stop 하는 중 짓던 씬은 버린다 (충돌 없음, 힙이 남지 않는다)
        Invoke-Nova 'scenestream priority --value 0' | Out-Null
        $r = Invoke-NovaJson 'scenestream load --path Assets/Scenes/CityShowcase.scene --allow false'
        Invoke-Nova 'wait 10' | Out-Null
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'wait 20' | Out-Null
        $mem = Invoke-NovaJson 'memory info'
        $alive = Test-EditorAlive $ed
        Add-Result streaming 'Stop while a scene is half built: the staged scene is dropped, nothing lingers' ($alive -and $mem -and @($mem.heaps | Where-Object { $_.lingering }).Count -eq 0) "editor alive $alive, lingering heaps $(if ($mem) { @($mem.heaps | Where-Object { $_.lingering }).Count } else { '?' })"
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
    }
    finally { Write-Host "  $(Stop-TestEditor $ed)" }
}

# ------------------------------------------------------------------ 성능 (참고용 — Release 빌드에서 의미가 있다)
function Suite-Perf
{
    Write-Host '[perf] (meaningful on a Release build)'
    $rows = @{}
    foreach ($api in 'dx', 'gl', 'vk')
    {
        $ed = Start-TestEditor -OpenGL:($api -eq 'gl') -Vulkan:($api -eq 'vk')
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
            # 그리기가 많은 장면: 캐릭터 64 (Skinned Mesh Renderer 는 낱개로 그린다 — 그리기마다의 CPU 비용, 상수가 그리기마다 다르다)
            Invoke-Nova 'scene new --force' | Out-Null
            Invoke-Nova 'create plane --name Floor --position 0,0,0 --scale 10,1,10' | Out-Null
            for ($i = 0; $i -lt 64; $i++)
            {
                Invoke-Nova "create character --name C$i" | Out-Null
                Invoke-Nova ("set C$i --position {0},0,{1}" -f (($i % 8) * 1.5 - 5.25), ([math]::Floor($i / 8) * 1.5)) | Out-Null
            }
            Invoke-Nova 'camera --position 0,6,-10 --target 0,0,5' | Out-Null
            Invoke-Nova 'wait 200' | Out-Null
            $rows["$api/Characters"] = Invoke-NovaJson 'perf --frames 300 --depth 3'
        }
        finally { Write-Host "  $(Stop-TestEditor $ed)" }
    }
    # 씬 뷰 그리기의 CPU 시간 (SceneView render 구간 — Present 의 GPU 대기가 섞이지 않는다)
    function RenderCpu($p) { $sc = @($p.cpuScopes | Where-Object { $_.scope -eq '..SceneView render' }); if ($sc.Count) { [double]$sc[0].ms } else { -1 } }
    $d = $rows['dx/Characters']
    foreach ($o in @(@('gl', 'OpenGL'), @('vk', 'Vulkan')))
    {
        $g = $rows["$($o[0])/Characters"]
        if (-not $d -or -not $g) { Add-Result perf "Characters $($o[1]) vs DX11 (draw CPU)" $false 'no perf result'; continue }
        $dc = RenderCpu $d; $gc = RenderCpu $g
        $ratio = $gc / [math]::Max(0.001, $dc)
        Add-Result perf "Characters $($o[1]) vs DX11 (draw CPU)" ($dc -gt 0 -and $gc -gt 0 -and $ratio -le 1.5) ('SceneView render CPU ms DX {0} / {2} {1} (×{3:N2}); frame ms DX {4} / {2} {5}' -f $dc, $gc, $o[1], $ratio, $d.frameMs, $g.frameMs)
    }
    foreach ($n in 'Materials', 'Trees')
    {
        $d = $rows["dx/$n"]
        foreach ($o in @(@('gl', 'OpenGL'), @('vk', 'Vulkan')))
        {
            $g = $rows["$($o[0])/$n"]
            if (-not $d -or -not $g) { Add-Result perf "$n $($o[1]) vs DX11" $false 'no perf result'; continue }
            $ratio = [double]$g.frameMs / [math]::Max(0.001, [double]$d.frameMs)
            Add-Result perf "$n $($o[1]) vs DX11" ($ratio -le 2.0) ('frame ms DX {0} / {5} {1} (×{2:N2}), GPU ms DX {3} / {5} {4}' -f $d.frameMs, $g.frameMs, $ratio, $d.gpuMs, $g.gpuMs, $o[1])
        }
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

function Suite-Weather
{
    # 날씨 (com.nova.weather): Weather Controller — 프로필 (먹구름 · 해 · 안개 · 바람), 비 · 눈 입자, 전환, 번개, .weather 에셋, C# API, 소리 (Play)
    Write-Host '[weather]'
    $dir = Join-Path $Out 'weather'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $assetDir = Join-Path $Project 'Assets\WeatherTest'
    Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $ed = Start-TestEditor
    try
    {
        function Wait-Sec([double]$sec) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $sec) { Invoke-Nova 'wait 10' | Out-Null } }
        function W([string]$line) { Invoke-NovaJson "weather $line" }
        function SetCtl([string]$values) { Invoke-Nova ('set Weather --component WeatherController --values "' + $values.Replace('"', '\"') + '"') | Out-Null }
        function Exec([string]$code) { $f = Join-Path $dir 'exec.cs'; $code | Set-Content -Encoding utf8 $f; $r = Invoke-NovaJson "exec --file $f"; if ($r) { "$($r.result)" } else { '' } }
        # 화면 전체의 평균 밝기 · 아주 밝은 화소 수
        function Shot([string]$name)
        {
            $p = Join-Path $dir $name
            Invoke-Nova "screenshot $p --view scene" | Out-Null
            if (-not (Test-Path $p)) { return $null }
            $bm = [System.Drawing.Bitmap]::FromFile($p)
            $sum = 0.0; $n = 0; $bright = 0
            for ($y = 0; $y -lt $bm.Height; $y += 2) { for ($x = 0; $x -lt $bm.Width; $x += 2) {
                $c = $bm.GetPixel($x, $y); $l = 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B
                $sum += $l; $n++; if ($c.R -gt 235 -and $c.G -gt 235 -and $c.B -gt 235) { $bright++ } } }
            $bm.Dispose()
            [pscustomobject]@{ Path = $p; Mean = $sum / [math]::Max(1, $n); Bright = $bright }
        }
        # 두 그림에서 눈에 띄게 다른 화소 비율 (비 · 눈 입자)
        function DiffRatio([string]$a, [string]$b)
        {
            $x1 = [System.Drawing.Bitmap]::FromFile($a); $x2 = [System.Drawing.Bitmap]::FromFile($b)
            $d = 0; $n = 0
            for ($y = 0; $y -lt $x1.Height; $y += 2) { for ($x = 0; $x -lt $x1.Width; $x += 2) {
                $c1 = $x1.GetPixel($x, $y); $c2 = $x2.GetPixel($x, $y); $n++
                if ([math]::Abs([int]$c1.R - $c2.R) + [math]::Abs([int]$c1.G - $c2.G) + [math]::Abs([int]$c1.B - $c2.B) -gt 30) { $d++ } } }
            $x1.Dispose(); $x2.Dispose()
            $d / [math]::Max(1, $n)
        }
        function F($v) { ([double]$v).ToString('0.###', [Globalization.CultureInfo]::InvariantCulture) }

        Invoke-Nova 'autosave discard' | Out-Null
        $pa = Invoke-NovaJson 'package add com.nova.weather'
        Wait-Compile   # Runtime/Weather.cs (C# API)
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'create cube --name Ground --position 0,-0.5,0 --scale 400,1,400' | Out-Null
        Invoke-Nova 'camera --position 0,2,-10 --target 0,1,10' | Out-Null
        Wait-Sec 0.5
        $s0 = Shot 'none.png'

        # ---- 붙이기: 맑음 = 장면 그대로
        Invoke-Nova 'create empty --name Weather' | Out-Null
        Invoke-Nova 'add-component Weather WeatherController' | Out-Null
        SetCtl '{"lightning":false}'
        Wait-Sec 0.5
        $st = W 'status'
        $sc = Shot 'clear.png'
        Add-Result weather 'package loads, Weather Controller starts Clear (scene unchanged)' ($pa -and $st.profile -eq 'Clear' -and [double]$st.sunIntensity -eq 1 -and $s0 -and $sc -and [math]::Abs($sc.Mean - $s0.Mean) -lt 3) ("profile {0}, sun {1}, brightness {2:N1} → {3:N1}" -f $st.profile, (F $st.sunIntensity), $s0.Mean, $sc.Mean)

        # ---- 폭풍: 어두워지고 바람이 세지고, 비가 화면에 보인다 (Density 0 과 비교)
        W 'set --profile Storm --seconds 0' | Out-Null
        SetCtl '{"density":0}'
        Wait-Sec 1.0
        $sd0 = Shot 'storm_norain.png'
        SetCtl '{"density":1}'
        Wait-Sec 2.5
        $ss = Shot 'storm.png'
        $st = W 'status'
        $rainDiff = if ($sd0 -and $ss) { DiffRatio $sd0.Path $ss.Path } else { 0 }
        Add-Result weather 'Storm: sun · sky · ambient darken, fog, wind x2+' ($st.profile -eq 'Storm' -and [double]$st.sunIntensity -lt 0.2 -and [double]$st.windStrength -gt 2 -and $ss -and $ss.Mean -lt $sc.Mean * 0.8) ("sun {0}, wind strength {1}, brightness {2:N1} → {3:N1}" -f (F $st.sunIntensity), (F $st.windStrength), $sc.Mean, $ss.Mean)
        Add-Result weather 'Storm: GPU rain streaks + splashes cover the view (vs Density 0)' ($rainDiff -gt 0.005) ("{0:P1} pixels changed by rain" -f $rainDiff)

        # ---- 전환: Transition 동안 섞인다
        W 'set --profile Clear --seconds 3' | Out-Null
        Wait-Sec 1.2
        $mid = W 'status'
        Wait-Sec 2.6
        $end = W 'status'
        Add-Result weather 'transition Storm → Clear over 3 s (values blend, then settle)' ([double]$mid.progress -gt 0.1 -and [double]$mid.progress -lt 0.95 -and [double]$mid.rain -gt 0.02 -and [double]$mid.rain -lt 0.98 -and [double]$end.progress -eq 1 -and [double]$end.rain -eq 0) ("mid: progress {0} rain {1}; end: progress {2} rain {3}" -f (F $mid.progress), (F $mid.rain), (F $end.progress), (F $end.rain))

        # ---- 번개: 보는 쪽에 빛줄기 (아주 밝은 화소) + 번쩍임
        W 'set --profile Storm --seconds 0' | Out-Null
        Wait-Sec 1.0
        $b0 = Shot 'before_strike.png'
        $k = W 'strike'
        $b1 = Shot 'strike.png'
        Add-Result weather 'lightning strike: bolt in front of the camera (bright pixels), strike counted' ($k -and [int]$k.strikes -ge 1 -and $b0 -and $b1 -and $b1.Bright -gt $b0.Bright + 30) ("strikes {0}, bright pixels {1} → {2}" -f $k.strikes, $b0.Bright, $b1.Bright)

        # ---- 눈: 흰 눈송이 (Density 0 과 비교), 눈보라는 안개가 짙다
        W 'set --profile Snow --seconds 0' | Out-Null
        SetCtl '{"density":0}'
        Wait-Sec 1.0
        $n0 = Shot 'snow_none.png'
        SetCtl '{"density":1}'
        Wait-Sec 4.0
        $n1 = Shot 'snow.png'
        $snowDiff = if ($n0 -and $n1) { DiffRatio $n0.Path $n1.Path } else { 0 }
        W 'set --profile Blizzard --seconds 0' | Out-Null
        Wait-Sec 3.0
        $bz = W 'status'
        Shot 'blizzard.png' | Out-Null
        Add-Result weather 'Snow: flakes drift down (vs Density 0); Blizzard: snow 1, wind 15 m/s' ($snowDiff -gt 0.004 -and [double]$bz.snow -eq 1 -and [double]$bz.wind -eq 15) ("{0:P2} pixels changed by snow, blizzard snow {1} wind {2}" -f $snowDiff, (F $bz.snow), (F $bz.wind))

        # ---- .weather 에셋: 지금 날씨 저장 → 다른 날씨 → 파일로 되돌리기, list 에 보인다
        $sv = W 'save --path Assets/WeatherTest/Mine.weather'
        W 'set --profile Clear --seconds 0' | Out-Null
        $set = W 'set --profile Assets/WeatherTest/Mine.weather --seconds 0'
        $ls = W 'list'
        $bad = Invoke-Nova 'weather set --profile Nope --seconds 0'
        Add-Result weather '.weather asset: save, load by path, listed; unknown profile refused' ($sv -and $set -and [double]$set.snow -eq 1 -and [double]$set.wind -eq 15 -and (@($ls.assets) -contains 'Assets\WeatherTest\Mine.weather' -or @($ls.assets) -contains 'Assets/WeatherTest/Mine.weather') -and $bad -match 'no weather profile') ("snow {0}, wind {1}, assets [{2}], bad '{3}'" -f (F $set.snow), (F $set.wind), (@($ls.assets) -join ', '), (($bad -split "`n")[0] -replace '^nova(\.exe)? : ', '').Trim())

        # ---- 젖은 표면 (2 단계): 같은 빛 (젖음만 다른 폭풍 프로필 둘) 에서 하늘 아래 바닥만 어두워지고, 지붕 아래는 그대로 (덮개 맵)
        '{"rain":1,"wind":2,"clouds":1,"fog":0,"wetness":0}' | Set-Content -Encoding utf8 (Join-Path $assetDir 'Dry.weather')
        '{"rain":1,"wind":2,"clouds":1,"fog":0,"wetness":1}' | Set-Content -Encoding utf8 (Join-Path $assetDir 'Wet.weather')
        Invoke-Nova 'create cube --name Roof --position 0,3,8 --scale 6,0.3,6' | Out-Null
        Invoke-Nova 'camera --position 0,1.2,0 --target 0,0.8,8' | Out-Null
        SetCtl '{"density":0}'
        function Band([string]$path, [double]$y0, [double]$y1)
        {
            $bm = [System.Drawing.Bitmap]::FromFile($path); $sum = 0.0; $n = 0
            for ($y = [int]($bm.Height * $y0); $y -lt [int]($bm.Height * $y1); $y += 2) { for ($x = [int]($bm.Width * 0.42); $x -lt [int]($bm.Width * 0.58); $x += 2) {
                $c = $bm.GetPixel($x, $y); $sum += 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B; $n++ } }
            $bm.Dispose(); $sum / [math]::Max(1, $n)
        }
        W 'set --profile Assets/WeatherTest/Dry.weather --seconds 0' | Out-Null
        Wait-Sec 1.0
        $dry = Shot 'surface_dry.png'
        W 'set --profile Assets/WeatherTest/Wet.weather --seconds 0' | Out-Null
        Wait-Sec 1.0
        $wetShot = Shot 'surface_wet.png'
        $ws = W 'status'
        if ($dry -and $wetShot)
        {
            $underDry = Band $dry.Path 0.57 0.63; $underWet = Band $wetShot.Path 0.57 0.63
            $openDry = Band $dry.Path 0.82 0.92; $openWet = Band $wetShot.Path 0.82 0.92
            Add-Result weather 'wet surfaces: open ground darkens, ground under a roof stays dry (cover map)' ($openWet -lt $openDry * 0.85 -and [math]::Abs($underWet - $underDry) -lt $underDry * 0.06 -and [double]$ws.surfaceWetness -eq 1) ("open {0:N1} → {1:N1}, under roof {2:N1} → {3:N1}, wetness {4}" -f $openDry, $openWet, $underDry, $underWet, (F $ws.surfaceWetness))
        }
        else { Add-Result weather 'wet surfaces: open ground darkens, ground under a roof stays dry (cover map)' $false 'no capture' }
        # 비가 오면 천천히 젖는다 (바로 바꾸지 않으면)
        W 'set --profile Clear --seconds 0' | Out-Null
        W 'set --profile Rain --seconds 1' | Out-Null
        Wait-Sec 3.0
        $gr = W 'status'
        Add-Result weather 'surfaces get wet gradually while it rains (then puddles)' ([double]$gr.surfaceWetness -gt 0.02 -and [double]$gr.surfaceWetness -lt 0.75 -and [double]$gr.puddles -lt [double]$gr.surfaceWetness) ("after 3 s of Rain: wetness {0}, puddles {1}" -f (F $gr.surfaceWetness), (F $gr.puddles))
        # ---- 쌓인 눈 (3 단계): 같은 빛에서 눈 덮임만 다른 프로필 → 하늘 아래 바닥이 하얘지고, 지붕 아래는 그대로
        '{"snow":1,"wind":1,"clouds":0,"fog":0,"snowCover":0}' | Set-Content -Encoding utf8 (Join-Path $assetDir 'Bare.weather')
        '{"snow":1,"wind":1,"clouds":0,"fog":0,"snowCover":1}' | Set-Content -Encoding utf8 (Join-Path $assetDir 'Snowy.weather')
        W 'set --profile Assets/WeatherTest/Bare.weather --seconds 0' | Out-Null
        Wait-Sec 1.0
        $bare = Shot 'snow_bare.png'
        W 'set --profile Assets/WeatherTest/Snowy.weather --seconds 0' | Out-Null
        Wait-Sec 1.0
        $snowy = Shot 'snow_cover.png'
        $sn = W 'status'
        if ($bare -and $snowy)
        {
            $underB = Band $bare.Path 0.57 0.63; $underS = Band $snowy.Path 0.57 0.63
            $openB = Band $bare.Path 0.82 0.92; $openS = Band $snowy.Path 0.82 0.92
            Add-Result weather 'snow cover: open ground turns white, ground under a roof stays bare' ($openS -gt $openB + 8 -and [math]::Abs($underS - $underB) -lt $underB * 0.06 -and [double]$sn.snowAmount -eq 1) ("open {0:N1} → {1:N1}, under roof {2:N1} → {3:N1}, snow {4}" -f $openB, $openS, $underB, $underS, (F $sn.snowAmount))
        }
        else { Add-Result weather 'snow cover: open ground turns white, ground under a roof stays bare' $false 'no capture' }
        # 발자국: RigidBody 공이 눈 위를 지나갔다 제자리로 → 지나간 자리만 다르다 (눌린 눈)
        Invoke-Nova 'create sphere --name Roller --position 0,0.5,3' | Out-Null
        Invoke-Nova 'add-component Roller RigidBody' | Out-Null
        Invoke-Nova 'camera --position 0,4,-2 --target 0,0,4' | Out-Null
        Wait-Sec 1.0
        $fp0 = Shot 'footprint_before.png'
        for ($i = 0; $i -le 24; $i++) { Invoke-Nova ("set Roller --position {0},0.5,{1}" -f ((-2.4 + $i * 0.2).ToString([Globalization.CultureInfo]::InvariantCulture)), '4') | Out-Null; Invoke-Nova 'wait 3' | Out-Null }
        Invoke-Nova 'set Roller --position 0,0.5,3' | Out-Null
        Wait-Sec 1.0
        $fp1 = Shot 'footprint_after.png'
        $trail = if ($fp0 -and $fp1) { DiffRatio $fp0.Path $fp1.Path } else { 0 }
        Add-Result weather 'footprints: a RigidBody rolling over the snow leaves a pressed trail (depth map from below)' ($trail -gt 0.01) ("{0:P2} pixels changed by the trail" -f $trail)
        Invoke-Nova 'delete Roller' | Out-Null

        Invoke-Nova 'delete Roof' | Out-Null
        Invoke-Nova 'camera --position 0,2,-10 --target 0,1,10' | Out-Null
        SetCtl '{"density":1}'

        # ---- C# API
        $cs = Exec 'NovaEngine.Weather.Set("Rain", 0f); return NovaEngine.Weather.profile;'
        Wait-Sec 0.3
        $cs2 = Exec 'return NovaEngine.Weather.exists + " " + NovaEngine.Weather.rain.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture);'
        Add-Result weather 'C# Weather.Set / profile / rain / exists' ($cs -eq 'Rain' -and $cs2 -eq 'True 0.55') "Set → '$cs', state '$cs2'"

        # ---- 소리 (Play): 폭풍이면 센 비 · 바람 소리가 돈다
        SetCtl '{"profile":"Storm","transitionTime":0}'
        Invoke-Nova 'play' | Out-Null
        Wait-Sec 2.0
        $pl = W 'status'
        Invoke-Nova 'stop' | Out-Null
        Invoke-Nova 'window scene' | Out-Null   # Play 뒤에는 Game 탭이 앞 → Scene 뷰를 다시 그리게
        Invoke-Nova 'wait 10' | Out-Null
        Add-Result weather 'Play: rain + wind loops play with Storm volumes' ($pl -and $pl.sound.playing -and [double]$pl.sound.rainHeavy -gt 0.5 -and [double]$pl.sound.wind -gt 0.2) ("playing {0}, heavy {1}, light {2}, wind {3}" -f $pl.sound.playing, (F $pl.sound.rainHeavy), (F $pl.sound.rainLight), (F $pl.sound.wind))

        # ---- 끄면 장면 그대로 (엔진의 날씨 값이 처음으로)
        Invoke-Nova 'set Weather --active false' | Out-Null
        Wait-Sec 0.5
        $off = W 'status'
        $so = Shot 'off.png'
        Add-Result weather 'inactive Weather Controller → lighting back to the scene' ($off -and [double]$off.sunIntensity -eq 1 -and $so -and [math]::Abs($so.Mean - $s0.Mean) -lt 3) ("sun {0}, brightness {1:N1} (no weather {2:N1})" -f (F $off.sunIntensity), $so.Mean, $s0.Mean)
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item $assetDir, "$assetDir.meta" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Tessellation([string]$Api = 'dx')
{
    # 재질 테셀레이션 (docs/TESSELLATION.md): 돌 벽 · 자갈 바닥 (Displacement Mode = Tessellation, Tools/tests/make_tess_textures.py)
    #  켬 · 끔 비교 (모양 · 윤곽), 깊이 프리패스와 본 패스가 같은 자리 (검은 얼룩 없음), 멀면 나누지 않는다, 쌓인 눈의 지형 (dx)
    # Api = dx · gl · vk (스위트 이름 tessellation · tessellationgl · tessellationvk)
    $sn = if ($Api -eq 'dx') { 'tessellation' } else { 'tessellation' + $Api }
    Write-Host "[$sn]"
    $dir = Join-Path $Out $sn
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Type -AssemblyName System.Drawing
    $tessDir = Join-Path $Project 'Assets\TessTest'
    & python (Join-Path $PSScriptRoot 'make_tess_textures.py') $tessDir | Out-Null
    foreach ($m in 'StoneWall', 'Cobble')
    {
        # 끔 사본 (같은 텍스처, Displacement Mode = None)
        $j = Get-Content -Raw (Join-Path $tessDir "$m.mat") | ConvertFrom-Json
        $j.DisplacementMode = 'None'
        $j.ResourcePath = "Assets\TessTest\${m}Flat.mat"
        $j | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $tessDir "${m}Flat.mat")
    }
    $ed = Start-TestEditor -OpenGL:($Api -eq 'gl') -Vulkan:($Api -eq 'vk') -D3D12:($Api -eq '12')
    try
    {
        function Shot([string]$name, [int]$frames = 10) { $p = Join-Path $dir $name; Invoke-Nova "wait $frames" | Out-Null; Invoke-Nova "screenshot $p --view scene" | Out-Null; $p }
        # 재질을 바꾸면 (set) Scene 뷰가 그 물체로 옮겨 갈 수 있다 → 바꾼 뒤 카메라를 다시 놓는다
        $script:tessCam = $null
        function Cam([string]$pos, [string]$target) { $script:tessCam = "camera --position $pos --target $target"; Invoke-Nova $script:tessCam | Out-Null }
        function Mats([string]$suffix)
        {
            Invoke-Nova ('set Floor --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/TessTest/Cobble' + $suffix + '.mat\"]}"') | Out-Null
            Invoke-Nova ('set Wall --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/TessTest/StoneWall' + $suffix + '.mat\"]}"') | Out-Null
            if ($script:tessCam) { Invoke-Nova $script:tessCam | Out-Null }
        }
        # 그림 하나: 아주 어두운 화소 (깊이가 어긋나 본 패스가 못 그린 자리 = 검정) · 하늘 화소 비율 (2 px 마다)
        function Stats([string]$p, [double]$top = 1.0)
        {
            # top = 위에서부터 이 비율만 (하늘 셈 — 아래의 푸른 격자 바닥을 빼고)
            $bm = [System.Drawing.Bitmap]::FromFile($p); $dark = 0; $sky = 0; $n = 0
            for ($y = 0; $y -lt $bm.Height * $top; $y += 2) { for ($x = 0; $x -lt $bm.Width; $x += 2) {
                $c = $bm.GetPixel($x, $y); $n++
                if ($c.R + $c.G + $c.B -lt 30) { $dark++ }
                if ($c.B -gt $c.R + 25) { $sky++ } } }
            $bm.Dispose()
            [pscustomobject]@{ Dark = $dark / [math]::Max(1, $n); Sky = $sky / [math]::Max(1, $n) }
        }
        function DiffRatio([string]$a, [string]$b)
        {
            $x1 = [System.Drawing.Bitmap]::FromFile($a); $x2 = [System.Drawing.Bitmap]::FromFile($b); $d = 0; $n = 0
            for ($y = 0; $y -lt $x1.Height; $y += 2) { for ($x = 0; $x -lt $x1.Width; $x += 2) {
                $c1 = $x1.GetPixel($x, $y); $c2 = $x2.GetPixel($x, $y); $n++
                if ([math]::Abs([int]$c1.R - $c2.R) + [math]::Abs([int]$c1.G - $c2.G) + [math]::Abs([int]$c1.B - $c2.B) -gt 30) { $d++ } } }
            $x1.Dispose(); $x2.Dispose()
            $d / [math]::Max(1, $n)
        }
        function Tris($perf) { $p = @($perf.gpuPasses | Where-Object { $_.pass -match '^\.Opaque$' })[0]; if ($p) { [double]$p.primitives } else { 0 } }

        Invoke-Nova 'autosave discard' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'create plane --name Floor --position 0,0,0 --scale 0.6,1,0.6' | Out-Null
        Invoke-Nova 'create plane --name Wall --position 0,1.5,2 --rotation -90,0,0 --scale 0.4,1,0.3' | Out-Null

        # 1. 앞에서: 켬은 벽돌 · 자갈의 입체가 끔 (평면 + 색) 과 다르고, 검은 얼룩이 없다 (깊이 프리패스 = 본 패스, OpenGL 은 invariant)
        Cam '-1.6,1.4,-1.2' '0.3,0.6,1.8'
        Mats ''
        $on = Shot 'front_on.png' 30
        Mats 'Flat'
        $off = Shot 'front_off.png'
        $d = DiffRatio $on $off; $sOn = Stats $on; $sOff = Stats $off
        Add-Result $sn 'Displacement changes the surface (relief vs the flat material with the same textures)' ($d -gt 0.05) ("{0:P1} pixels differ" -f $d)
        Add-Result $sn 'No depth mismatch speckles (depth prepass and main pass displace identically)' ($sOn.Dark -lt 0.002 -and $sOn.Dark -le $sOff.Dark + 0.001) ("near-black {0:P3} (flat {1:P3})" -f $sOn.Dark, $sOff.Dark)

        # 2. 벽면 안에서 (카메라가 벽의 평면 위): 평평한 벽은 두께가 없어 안 보이고, 민 벽돌은 그 평면 밖으로 나와 하늘을 가린다
        Cam '3.5,1.5,2.0' '-2,1.5,2.0'
        Mats ''
        $sideOn = Stats (Shot 'edge_on.png') 0.45
        Mats 'Flat'
        $sideOff = Stats (Shot 'edge_off.png') 0.45
        Add-Result $sn 'Silhouette: seen edge-on, the displaced bricks stick out of the wall plane (less sky above the horizon)' ($sideOn.Sky -lt $sideOff.Sky - 0.003) ("sky in the upper image {0:P2} (flat) → {1:P2}" -f $sideOff.Sky, $sideOn.Sky)

        # 3. 삼각형: 가까우면 잘게 나눈다, Fade Distance (50 m) 너머는 나누지 않는다
        Cam '-1.6,1.4,-1.2' '0.3,0.6,1.8'
        Mats ''; Invoke-Nova 'wait 10' | Out-Null
        $nearOn = Tris (Invoke-NovaJson 'perf --frames 20')
        Mats 'Flat'; Invoke-Nova 'wait 10' | Out-Null
        $nearOff = Tris (Invoke-NovaJson 'perf --frames 20')
        Cam '0,8,-70' '0,1,2'
        Mats ''; Invoke-Nova 'wait 10' | Out-Null
        $farOn = Tris (Invoke-NovaJson 'perf --frames 20')
        Mats 'Flat'; Invoke-Nova 'wait 10' | Out-Null
        $farOff = Tris (Invoke-NovaJson 'perf --frames 20')
        Add-Result $sn 'Near: subdivided (many more triangles); beyond Fade Distance: not subdivided' ($nearOn -gt $nearOff * 10 -and $farOn -le $farOff * 1.2 + 50) ("near {0:N0} vs flat {1:N0}; far {2:N0} vs flat {3:N0} triangles" -f $nearOn, $nearOff, $farOn, $farOff)

        # 5. POM (테셀레이션이 없는 기기 · 나눔 거리 너머): 테셀레이션을 끄면 시차 가림 + 픽셀 높이 법선 — 평면과 다르고 깊이는 그대로 (검은 얼룩 없음)
        Cam '-1.6,1.4,-1.2' '0.3,0.6,1.8'
        Invoke-Nova 'tessellation set --enabled false' | Out-Null
        Mats ''
        $pom = Shot 'pom.png'
        Invoke-Nova 'tessellation set --enabled true' | Out-Null
        Mats 'Flat'
        $flat = Shot 'pom_flat.png'
        Mats ''
        $dp = DiffRatio $pom $flat; $sp = Stats $pom
        Add-Result $sn 'Without tessellation (nova tessellation set --enabled false): POM + per-pixel height normals still show the relief' ($dp -gt 0.05 -and $sp.Dark -lt 0.002) ("{0:P1} pixels differ from the flat material; near-black {1:P3}" -f $dp, $sp.Dark)

        # 6. Shader Graph: Graph Settings 의 Tessellation + Vertex 블록 Displacement (docs/examples/shadergraph_tessellation.txt — 사인 물결)
        Remove-Item (Join-Path $Project 'Assets\SGTess') -Recurse -Force -ErrorAction SilentlyContinue
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-NovaJson 'shadergraph new Assets/SGTess/TessWave.shadergraph --timeout 240' | Out-Null
        Invoke-NovaJson "shadergraph batch $(Join-Path $Root 'docs\examples\shadergraph_tessellation.txt')" | Out-Null
        $sgs = Invoke-NovaJson 'shadergraph save --timeout 240'
        Invoke-NovaJson 'shadergraph material' | Out-Null
        $sgh = Invoke-NovaJson 'shadergraph compile --hlsl'
        Invoke-Nova 'window scene' | Out-Null
        Invoke-Nova 'create plane --name Sea --position 0,0,0 --scale 0.6,1,0.6' | Out-Null
        Invoke-Nova 'set Sea --component MeshRenderer --values "{\"m_MaterialPaths\":[\"Assets/SGTess/TessWave.mat\"]}"' | Out-Null
        Invoke-Nova 'camera --position 0,1.6,-3.5 --target 0,0,0.5' | Out-Null
        $wOn = Shot 'graph_on.png' 30
        Invoke-Nova 'tessellation set --enabled false' | Out-Null
        $wOff = Shot 'graph_off.png'
        Invoke-Nova 'tessellation set --enabled true' | Out-Null
        $dw = DiffRatio $wOn $wOff; $sw2 = Stats $wOn
        Add-Result $sn 'Shader Graph Tessellation: Displacement (Vertex block) makes waves on a plane, depth prepass matches' ($sgs.built -and $sgh.hlsl -match 'GraphTessBatchTech' -and $dw -gt 0.05 -and $sw2.Dark -lt 0.002) ("built {0}, tess techniques {1}; {2:P1} pixels differ from tessellation off; near-black {3:P3}" -f $sgs.built, ($sgh.hlsl -match 'GraphTessBatchTech'), $dw, $sw2.Dark)

        # 7. Terrain Layer 의 Height Map (흙에 박힌 돌, 0.25 m): 가까운 지형이 잘게 나뉘어 돌이 솟는다 — 끈 것과 다르고 검은 얼룩 없음
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create terrain --name Ground --position -500,0,-500' | Out-Null
        Invoke-NovaJson 'terrain-layer Ground --add Assets\TessTest\Rocks.terrainlayer' | Out-Null
        Invoke-Nova 'camera --position 0,0.9,-2 --target 0,0.1,4' | Out-Null
        $tOn = Shot 'terrain_on.png' 30
        $trOn = Tris (Invoke-NovaJson 'perf --frames 20')
        Invoke-Nova 'tessellation set --enabled false' | Out-Null
        $tOff = Shot 'terrain_off.png'
        $trOff = Tris (Invoke-NovaJson 'perf --frames 20')
        Invoke-Nova 'tessellation set --enabled true' | Out-Null
        $dt = DiffRatio $tOn $tOff; $st2 = Stats $tOn
        Add-Result $sn 'Terrain Layer height map: near terrain subdivided and displaced (rocks), no black speckles' ($trOn -gt $trOff * 3 -and $dt -gt 0.05 -and $st2.Dark -lt 0.002) ("terrain triangles {0:N0} vs off {1:N0}; {2:P1} pixels differ; near-black {3:P3}" -f $trOn, $trOff, $dt, $st2.Dark)

        # 8. 높이 기반 섞기 (Terrain 의 Height-Based Blend): 흙 + 돌 (3 m 부드러운 경계) — 켜면 경계에서 돌이 또렷이 드러난다
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create terrain --name Ground --position -500,0,-500' | Out-Null
        Invoke-NovaJson 'terrain-layer Ground --add Assets\TessTest\Soil.terrainlayer' | Out-Null
        Invoke-NovaJson 'terrain-layer Ground --add Assets\TessTest\Rocks.terrainlayer' | Out-Null
        Invoke-NovaJson 'terrain-layer Ground --fill 1 --center 0,5 --radius 5 --soft 3' | Out-Null
        Invoke-Nova 'camera --position 0,2.2,-1.5 --target 0,0,4' | Out-Null
        $bOff = Shot 'blend_off.png' 30
        Invoke-Nova 'set Ground --component Terrain --values "{\"heightBasedBlend\":true,\"heightTransition\":0.25}"' | Out-Null
        Invoke-Nova 'camera --position 0,2.2,-1.5 --target 0,0,4' | Out-Null
        $bOn = Shot 'blend_on.png' 30
        $db = DiffRatio $bOff $bOn; $sb = Stats $bOn
        Add-Result $sn 'Height-Based Blend: at the soil / rock border the rocks show through first (differs from linear blend), no black speckles' ($db -gt 0.02 -and $sb.Dark -lt 0.002) ("{0:P1} pixels differ from linear blend; near-black {1:P3}" -f $db, $sb.Dark)

        # 9. 테셀레이션 없이 (POM + 높이 범프, 높이 배열 하나 — 샘플러 한도 안): 같은 돌의 Amplitude 0 과 다르다
        $flatLayer = Join-Path $Project 'Assets\TessTest\RocksFlat.terrainlayer'
        $rl = Get-Content -Raw (Join-Path $Project 'Assets\TessTest\Rocks.terrainlayer') | ConvertFrom-Json
        $rl.heightAmplitude = 0
        $rl | ConvertTo-Json | Set-Content -Encoding utf8 $flatLayer
        Invoke-Nova 'tessellation set --enabled false' | Out-Null
        Invoke-Nova 'camera --position 0,2.2,-1.5 --target 0,0,4' | Out-Null
        $pOn = Shot 'terrain_pom.png' 30
        Invoke-NovaJson 'terrain-layer Ground --set 1 --layer Assets\TessTest\RocksFlat.terrainlayer' | Out-Null
        Invoke-Nova 'camera --position 0,2.2,-1.5 --target 0,0,4' | Out-Null
        $pFlat = Shot 'terrain_pom_flat.png' 30
        Invoke-Nova 'tessellation set --enabled true' | Out-Null
        Remove-Item $flatLayer -ErrorAction SilentlyContinue
        $dpo = DiffRatio $pOn $pFlat; $spo = Stats $pOn
        Add-Result $sn 'Terrain without tessellation: POM + height bump from the layer height array show the rocks (vs Amplitude 0)' ($dpo -gt 0.02 -and $spo.Dark -lt 0.002) ("{0:P1} pixels differ; near-black {1:P3}" -f $dpo, $spo.Dark)

        # 10 · 11. 절벽 (6 m 단 — terrain-height): Terrain Layer 의 Normal Map 이 음영을 바꾸고, 테셀레이션 없이도 절벽 면에 POM (옆 투영)
        $td = Join-Path $Project 'Assets\TessTest'
        $rj = Get-Content -Raw (Join-Path $td 'Rocks.terrainlayer') | ConvertFrom-Json
        $rj.normalScale = 0; $rj | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $td 'RocksNoNormal.terrainlayer')
        $rj = Get-Content -Raw (Join-Path $td 'Rocks.terrainlayer') | ConvertFrom-Json
        $rj.heightAmplitude = 0; $rj | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $td 'RocksFlat.terrainlayer')
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'create terrain --name Ground --position -500,0,-500' | Out-Null
        Invoke-NovaJson 'terrain-layer Ground --add Assets\TessTest\Soil.terrainlayer' | Out-Null
        Invoke-NovaJson 'terrain-layer Ground --add Assets\TessTest\Rocks.terrainlayer' | Out-Null
        Invoke-NovaJson 'terrain-layer Ground --fill 1 --center 0,15 --radius 30 --soft 4' | Out-Null
        Invoke-NovaJson 'terrain-height Ground --height 6 --center 0,18 --radius 8 --soft 1.5' | Out-Null
        function CliffShot([string]$name) { Invoke-Nova 'camera --position 0,2.2,3 --target 0,2.5,10' | Out-Null; Shot $name 30 }
        $nOn = CliffShot 'cliff_normal.png'
        Invoke-NovaJson 'terrain-layer Ground --set 1 --layer Assets\TessTest\RocksNoNormal.terrainlayer' | Out-Null
        $nOff = CliffShot 'cliff_nonormal.png'
        $dn = DiffRatio $nOn $nOff; $sn2 = Stats $nOn
        Add-Result $sn 'Terrain Layer Normal Map (packed with the heights — no extra sampler) changes the shading of rocks on the ground and the cliff' ($dn -gt 0.03 -and $sn2.Dark -lt 0.002) ("{0:P1} pixels differ from Normal Scale 0; near-black {1:P3}" -f $dn, $sn2.Dark)
        Invoke-NovaJson 'terrain-layer Ground --set 1 --layer Assets\TessTest\Rocks.terrainlayer' | Out-Null
        Invoke-Nova 'tessellation set --enabled false' | Out-Null
        $cOn = CliffShot 'cliff_pom.png'
        Invoke-NovaJson 'terrain-layer Ground --set 1 --layer Assets\TessTest\RocksFlat.terrainlayer' | Out-Null
        $cOff = CliffShot 'cliff_pom_flat.png'
        Invoke-Nova 'tessellation set --enabled true' | Out-Null
        Remove-Item (Join-Path $td 'RocksNoNormal.terrainlayer'), (Join-Path $td 'RocksFlat.terrainlayer') -ErrorAction SilentlyContinue
        $dc = DiffRatio $cOn $cOff; $sc2 = Stats $cOn
        Add-Result $sn 'Cliff without tessellation: POM along the side projection + height bump show the rocks on the cliff face (vs Amplitude 0)' ($dc -gt 0.02 -and $sc2.Dark -lt 0.002) ("{0:P1} pixels differ; near-black {1:P3}" -f $dc, $sc2.Dark)

        # 4. 쌓인 눈의 지형 (날씨): 지형을 실제로 올린다 → 같은 눈 덮임에서 삼각형이 늘고, 공이 지나간 자국이 파인다 (검은 얼룩 없음)
        if ($Api -eq 'dx')
        {
            $wdir = Join-Path $Project 'Assets\WeatherTest'
            New-Item -ItemType Directory -Force $wdir | Out-Null
            '{"snow":0.3,"wind":1,"clouds":0,"fog":0,"snowCover":1}' | Set-Content -Encoding utf8 (Join-Path $wdir 'Snowy.weather')
            '{"snow":0,"wind":1,"clouds":0,"fog":0,"snowCover":0}' | Set-Content -Encoding utf8 (Join-Path $wdir 'Bare.weather')
            Invoke-NovaJson 'package add com.nova.weather' | Out-Null
            Wait-Compile
            Invoke-Nova 'scene new --force' | Out-Null
            Invoke-Nova 'create terrain --name Ground --position -50,0,-50' | Out-Null
            Invoke-Nova 'create empty --name Weather' | Out-Null
            Invoke-Nova 'add-component Weather WeatherController' | Out-Null
            Invoke-Nova 'set Weather --component WeatherController --values "{\"lightning\":false,\"density\":0}"' | Out-Null
            Invoke-Nova 'camera --position 0,3,-2 --target 0,0,4' | Out-Null
            Invoke-NovaJson 'weather set --profile Assets/WeatherTest/Bare.weather --seconds 0' | Out-Null
            Invoke-Nova 'wait 20' | Out-Null
            $bare = Tris (Invoke-NovaJson 'perf --frames 20')
            Invoke-NovaJson 'weather set --profile Assets/WeatherTest/Snowy.weather --seconds 0' | Out-Null
            Invoke-Nova 'wait 20' | Out-Null
            $snowy = Tris (Invoke-NovaJson 'perf --frames 20')
            Invoke-Nova 'create sphere --name Roller --position 0,0.5,3' | Out-Null
            Invoke-Nova 'add-component Roller RigidBody' | Out-Null
            Invoke-Nova 'camera --position 1.6,0.6,2.6 --target -0.5,0.1,4' | Out-Null
            $before = Shot 'snow_before.png' 30
            for ($i = 0; $i -le 24; $i++) { Invoke-Nova ("set Roller --position {0},0.5,4" -f ((-2.4 + $i * 0.2).ToString([Globalization.CultureInfo]::InvariantCulture))) | Out-Null; Invoke-Nova 'wait 3' | Out-Null }
            Invoke-Nova 'set Roller --position 3,0.5,8' | Out-Null
            Invoke-Nova 'camera --position 1.6,0.6,2.6 --target -0.5,0.1,4' | Out-Null
            $after = Shot 'snow_after.png' 30
            $trail = DiffRatio $before $after; $st = Stats $after
            Add-Result $sn 'Snow on terrain: tessellated and raised (more triangles), a rolled ball carves a trail, no black speckles' ($snowy -gt $bare * 1.5 -and $trail -gt 0.02 -and $st.Dark -lt 0.002) ("terrain triangles {0:N0} → {1:N0}; trail {2:P1}; near-black {3:P3}" -f $bare, $snowy, $trail, $st.Dark)
        }
        $errs = Invoke-Nova 'log --errors -n 20' | Out-String
        $tessErr = @($errs -split "`n" | Where-Object { $_ -match 'Tess|60\. Tessellation' })
        Add-Result $sn 'No tessellation shader errors in the log' ($tessErr.Count -eq 0) $(if ($tessErr.Count) { $tessErr[0].Trim() } else { 'clean' })
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
        Remove-Item (Join-Path $tessDir 'StoneWallFlat.mat'), (Join-Path $tessDir 'CobbleFlat.mat') -Force -ErrorAction SilentlyContinue
    }
}

function Suite-Vfx([string]$Api = 'dx')
{
    # Visual Effect Graph (Unity VFX Graph): .vfx 편집 (nova vfx), GPU 시뮬레이션 (58. VFX.fx — Spawn · Update · GPU Event), 이벤트 · 속성 덮어쓰기, C# API, 그리기
    $suite = if ($Api -eq 'dx') { 'vfx' } else { "vfx$Api" }   # vfx · vfxgl · vfxvk · vfx12 (DirectX 12 — 시뮬레이션이 비동기 컴퓨트 큐에서)
    Write-Host "[$suite]"
    $dir = Join-Path $Out $suite
    New-Item -ItemType Directory -Force $dir | Out-Null
    $ed = Start-TestEditor -OpenGL:($Api -eq 'gl') -Vulkan:($Api -eq 'vk') -D3D12:($Api -eq '12')
    try
    {
        function Stats { (Invoke-NovaJson 'vfx stats') }
        function Sys($s, [string]$obj, [string]$name) { ($s.effects | Where-Object { $_.object -eq $obj } | Select-Object -First 1).systems | Where-Object { $_.name -eq $name } | Select-Object -First 1 }
        # 조건이 맞을 때까지 (백그라운드 에디터는 프레임이 빠르다 — 시간으로 기다린다)
        function WaitFor([scriptblock]$cond, [int]$seconds = 20) { $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $seconds) { Invoke-Nova 'wait 30' | Out-Null; $s = Stats; if (& $cond $s) { return $s }; Start-Sleep -Milliseconds 250 }; Stats }
        Invoke-Nova 'autosave discard' | Out-Null
        Invoke-Nova 'scene new --force' | Out-Null
        Invoke-Nova 'window scene' | Out-Null
        $root = 'Assets/VFX/Test'

        if ($Api -eq 'dx')
        {
            # ---- 에셋 편집 (CLI)
            $a = Invoke-NovaJson "vfx new $root/Edit.vfx --overwrite"
            $okNew = $a -and $a.systems.Count -eq 1 -and $a.issues.Count -eq 0
            Invoke-Nova "vfx property.add $root/Edit.vfx --name Power --type Float --value 3" | Out-Null
            Invoke-Nova "vfx block.add $root/Edit.vfx --system Particles --context update --type Gravity --params {\`"Force\`":[0,-2,0]}" | Out-Null
            Invoke-Nova "vfx block.add $root/Edit.vfx --system Particles --context update --type Vortex --bind {\`"Speed\`":\`"Power\`"}" | Out-Null
            Invoke-Nova "vfx property.set $root/Edit.vfx --name Power --rename Strength" | Out-Null
            Invoke-Nova "vfx system.set $root/Edit.vfx --system Particles --data {\`"capacity\`":777,\`"output\`":{\`"shape\`":\`"Star\`"}}" | Out-Null
            $bad = Invoke-Nova "vfx block.add $root/Edit.vfx --system Particles --context initialize --type Gravity"
            $i = Invoke-NovaJson "vfx info $root/Edit.vfx"
            $sys = $i.systems[0]
            $vortex = $sys.update | Where-Object { $_.type -eq 'Vortex' }
            $okEdit = $okNew -and $sys.capacity -eq 777 -and $sys.output.shape -eq 'Star' -and ($sys.update | Where-Object { $_.type -eq 'Gravity' }).params.Force[1] -eq -2 -and
                $vortex.bind.Speed -eq 'Strength' -and $i.properties[0].name -eq 'Strength' -and $i.issues.Count -eq 0 -and $bad -match 'belongs to update'
            Add-Result $suite 'nova vfx: new, property.add/rename (binds follow), block.add (+params/bind), system.set merge, wrong context refused' $okEdit ("capacity {0}, shape {1}, vortex bind {2}, issues {3}, bad '{4}'" -f $sys.capacity, $sys.output.shape, $vortex.bind.Speed, $i.issues.Count, $bad)
            $badParent = Invoke-NovaJson "vfx system.set $root/Edit.vfx --system Particles --data {\`"spawn\`":{\`"parent\`":\`"Nope\`"}}"
            Add-Result $suite 'validate: missing GPU event parent is reported' ([bool](@($badParent.issues) -match 'does not exist')) (@($badParent.issues) -join '; ')
        }

        # ---- GPU 시뮬레이션: 견본 두 개 (마법진 = 5 시스템 · Orbit, 불꽃놀이 = GPU Event 사슬)
        Invoke-Nova "vfx new $root/Circle_$Api.vfx --template `"Magic Circle`" --overwrite" | Out-Null
        Invoke-Nova "vfx new $root/Fireworks_$Api.vfx --template Fireworks --overwrite" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/Circle_$Api.vfx --name Circle --position 0,0,0" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/Fireworks_$Api.vfx --name Fireworks --position 30,0,0" | Out-Null
        # 둘 다 화면에 (화면 밖이면 Unity 처럼 시뮬레이션을 쉰다 — 컬링 검사는 아래에서 따로)
        Invoke-Nova 'camera --position 15,10,-34 --target 15,5,0' | Out-Null
        $s = WaitFor { param($s) (Sys $s 'Circle' 'Outer Ring').alive -gt 1000 -and (Sys $s 'Fireworks' 'Crackle').alive -gt 0 } 30
        $outer = (Sys $s 'Circle' 'Outer Ring').alive; $pillar = (Sys $s 'Circle' 'Pillar').alive
        Add-Result $suite "${Api}: GPU spawn + update (Magic Circle systems alive)" ($s.gpu -and $outer -gt 1000 -and $pillar -gt 100) ("gpu {0}, Outer Ring {1}, Pillar {2}" -f $s.gpu, $outer, $pillar)
        $rocket = (Sys $s 'Fireworks' 'Rocket').alive; $boom = (Sys $s 'Fireworks' 'Explosion').alive; $crackle = (Sys $s 'Fireworks' 'Crackle').alive
        Add-Result $suite "${Api}: GPU events chain (Rocket dies -> Explosion -> Crackle)" ($boom -gt 100 -and $crackle -gt 0) ("Rocket {0}, Explosion {1}, Crackle {2}" -f $rocket, $boom, $crackle)
        if ($Api -eq '12' -or $Api -eq 'vk')
        {
            # 비동기 컴퓨트 (렌더링 현대화 4 단계): Render Graph 의 VFX Simulation 패스가 컴퓨트 큐에서, Particles 가 그 결과를 기다린다
            $all = Invoke-NovaJson 'rendergraph info'
            $sim = $null; $par = $null
            foreach ($g in $all.PSObject.Properties) { if ($g.Value.passes) { $x = @($g.Value.passes | Where-Object { $_.name -eq 'VFX Simulation' })[0]; if ($x) { $sim = $x; $par = @($g.Value.passes | Where-Object { $_.name -eq 'Particles' })[0] } } }
            Add-Result $suite "${Api}: VFX Simulation runs on the async compute queue, Particles waits for it" ($sim -and $sim.queue -eq 'compute' -and $par -and $par.waitsAsync) ("simulation queue {0}, particles waits {1}" -f $sim.queue, $par.waitsAsync)
            # 같은 큐로 바꿔도 같은 결과 (살아 있는 수가 0 이 아니다) — 끄고 켜기
            Invoke-Nova 'rendergraph set --async false' | Out-Null
            $s2 = WaitFor { param($s) (Sys $s 'Circle' 'Outer Ring').alive -gt 1000 } 10
            $all2 = Invoke-NovaJson 'rendergraph info'
            $q2 = $null
            foreach ($g in $all2.PSObject.Properties) { if ($g.Value.passes) { $x = @($g.Value.passes | Where-Object { $_.name -eq 'VFX Simulation' })[0]; if ($x) { $q2 = $x.queue } } }
            Invoke-Nova 'rendergraph set --async true' | Out-Null
            Add-Result $suite "${Api}: rendergraph set --async false runs it on the graphics queue (same simulation)" ($q2 -eq 'graphics' -and (Sys $s2 'Circle' 'Outer Ring').alive -gt 1000) ("queue {0}, Outer Ring {1}" -f $q2, (Sys $s2 'Circle' 'Outer Ring').alive)
        }

        # ---- 이벤트 · 속성: OnStop 이면 마법진이 사라진다, OnPlay 로 다시
        Invoke-Nova 'vfx event --object Circle --name OnStop' | Out-Null
        $s = WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Circle' }).alive -eq 0 } 15
        $stopped = ($s.effects | Where-Object { $_.object -eq 'Circle' }).alive
        Invoke-Nova 'vfx event --object Circle --name OnPlay' | Out-Null
        $s = WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Circle' }).alive -gt 1000 } 15
        $again = ($s.effects | Where-Object { $_.object -eq 'Circle' }).alive
        Add-Result $suite "${Api}: OnStop stops spawning (particles die out), OnPlay starts again" ($stopped -eq 0 -and $again -gt 1000) "after OnStop $stopped, after OnPlay $again"
        # Launch Rate 0 (Spawn rate 에 연결된 속성) → 로켓이 더 오르지 않는다
        Invoke-Nova 'vfx override --object Fireworks --name "Launch Rate" --value 0' | Out-Null
        $s = WaitFor { param($s) (Sys $s 'Fireworks' 'Rocket').alive -eq 0 } 15
        Add-Result $suite "${Api}: exposed property override (Launch Rate 0 -> no rockets)" ((Sys $s 'Fireworks' 'Rocket').alive -eq 0) ("Rocket {0}" -f (Sys $s 'Fireworks' 'Rocket').alive)

        # ---- 그리기: 마법진 앞 화면이 끈 것과 다르다 (밝은 고리)
        Invoke-Nova 'camera --position 0,4.5,-7.5 --target 0,0.8,0' | Out-Null
        $onPng = Join-Path $dir 'circle_on.png'; $offPng = Join-Path $dir 'circle_off.png'
        Invoke-Nova 'wait 60' | Out-Null; Invoke-Nova "screenshot $onPng --view scene" | Out-Null
        Invoke-Nova 'set Circle --active false' | Out-Null; Invoke-Nova 'wait 30' | Out-Null; Invoke-Nova "screenshot $offPng --view scene" | Out-Null
        Invoke-Nova 'set Circle --active true' | Out-Null
        $c = [NovaImageCompare]::Compare($offPng, $onPng, $null)
        Add-Result $suite "${Api}: Visual Effect is drawn (on vs off)" ($c -and $c[2] -gt 1.0) $(if ($c) { 'pixels >8 different: {0:N2}%' -f $c[2] } else { 'capture missing' })

        # ---- 연산 노드 + 정렬: 색 = Lerp(빨강, 파랑, Remap(Position.z)) — 가까운 (z = -2) 것이 빨강. 정렬하면 가운데가 빨강, 안 하면 섞인다
        if (-not ('NovaCenterColor' -as [type]))
        {
            Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.Drawing;
public static class NovaCenterColor
{
    // 가운데 (가로 · 세로 30 %) 의 평균 빨강 · 파랑 비율 r / (r + b)
    public static double RedShare(string path)
    {
        using (var b = new Bitmap(path))
        {
            double r = 0, bl = 0;
            for (int y = (int)(b.Height * 0.35); y < (int)(b.Height * 0.65); y += 3)
                for (int x = (int)(b.Width * 0.35); x < (int)(b.Width * 0.65); x += 3) { var c = b.GetPixel(x, y); r += c.R; bl += c.B; }
            return r / Math.Max(1.0, r + bl);
        }
    }
}
"@
        }
        function SortAsset([string]$sort)
        {
            $j = @{
                systems = @(@{ name = 'Layers'; capacity = 4096
                    spawn = @{ rate = 0; loop = $false; duration = 0; bursts = @(@{ time = 0; count = 4000; cycles = 1; interval = 1 }) }
                    initialize = @(
                        @{ type = 'SetPosition'; params = @{ Shape = 'Box'; Size = @(1.2, 1.2, 4); Center = @(0, 1, 0) } },
                        @{ type = 'SetLifetime'; params = @{ Min = 100; Max = 100 } },
                        @{ type = 'SetSize'; params = @{ Min = 0.35; Max = 0.35 } },
                        @{ type = 'SetColor'; params = @{ ColorA = @(1, 1, 1, 1) }; links = @{ ColorA = 4 } })
                    update = @()
                    output = @{ blend = 'Alpha'; shape = 'Square'; sort = $sort; softDistance = 0 } })
                operators = @(
                    @{ id = 1; type = 'Position' },
                    @{ id = 2; type = 'Split'; params = @{ Component = 'Z' }; inputs = @{ X = 1 } },
                    @{ id = 3; type = 'Remap'; params = @{ InMin = -2; InMax = 2; OutMin = 0; OutMax = 1 }; inputs = @{ X = 2 } },
                    @{ id = 4; type = 'Lerp'; params = @{ A = @(1, 0, 0, 1); B = @(0, 0, 1, 1) }; inputs = @{ T = 3 } })
            }
            $f = Join-Path $dir "sort_$sort.json"
            ($j | ConvertTo-Json -Depth 10) | Set-Content -Encoding utf8 $f
            $f
        }
        $shares = @{}
        foreach ($mode in 'On', 'Off')
        {
            $f = SortAsset $mode
            Invoke-Nova "vfx set $root/Sort_$Api.vfx --file $f" | Out-Null
            if ($mode -eq 'On') { Invoke-Nova "create visual-effect --asset $root/Sort_$Api.vfx --name Sorter --position 0,0,-60" | Out-Null } else { Invoke-Nova 'vfx restart --object Sorter' | Out-Null }
            Invoke-Nova 'camera --position 0,1,-63.5 --target 0,1,-60' | Out-Null
            WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Sorter' }).alive -ge 3990 } 15 | Out-Null
            $png = Join-Path $dir "sort_$mode.png"
            Invoke-Nova 'wait 10' | Out-Null
            Invoke-Nova "screenshot $png --view scene" | Out-Null
            $shares[$mode] = [NovaCenterColor]::RedShare($png)
        }
        Add-Result $suite "${Api}: operators (Position -> Split -> Remap -> Lerp color) + GPU sort (near red on top only when sorted)" ($shares['On'] -gt 0.85 -and $shares['On'] - $shares['Off'] -gt 0.15) ("red share sorted {0:N2}, unsorted {1:N2}" -f $shares['On'], $shares['Off'])
        Invoke-Nova 'delete Sorter' | Out-Null

        # ---- 연산 노드가 블록 값을 바꾼다: 수명 = Float 0.05 → 살아 있는 수가 크게 준다
        Invoke-Nova "vfx new $root/OpLife_$Api.vfx --overwrite" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/OpLife_$Api.vfx --name OpLife --position 0,0,-60" | Out-Null
        Invoke-Nova 'camera --position 0,2,-66 --target 0,1,-60' | Out-Null
        $before = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'OpLife' }).alive -gt 60 } 15).effects | Where-Object { $_.object -eq 'OpLife' }
        $op = Invoke-NovaJson "vfx op.add $root/OpLife_$Api.vfx --type Float --params {\`"Value\`":0.05}"
        Invoke-Nova "vfx block.link $root/OpLife_$Api.vfx --system Particles --context initialize --index 2 --param Max --from $($op.id)" | Out-Null
        Invoke-Nova "vfx block.link $root/OpLife_$Api.vfx --system Particles --context initialize --index 2 --param Min --from $($op.id)" | Out-Null
        $after = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'OpLife' }).alive -lt 15 } 15).effects | Where-Object { $_.object -eq 'OpLife' }
        $info = Invoke-NovaJson "vfx info $root/OpLife_$Api.vfx"
        Add-Result $suite "${Api}: operator linked to Set Lifetime (Float 0.05) shortens lives" ($before.alive -gt 60 -and $after.alive -lt 15 -and $info.issues.Count -eq 0) ("alive {0} -> {1}, issues {2}" -f $before.alive, $after.alive, $info.issues.Count)
        Invoke-Nova 'delete OpLife' | Out-Null

        # ---- 꼬리 (Trail Only): 띠만 그린다
        Invoke-Nova "vfx new $root/Swirl_$Api.vfx --template `"Energy Swirl`" --overwrite" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/Swirl_$Api.vfx --name Swirl --position 0,0,-60" | Out-Null
        Invoke-Nova 'camera --position 0,2.5,-67 --target 0,1.5,-60' | Out-Null
        WaitFor { param($s) (Sys $s 'Swirl' 'Ribbons').alive -gt 100 } 15 | Out-Null
        $on = Join-Path $dir 'trail_on.png'; $off = Join-Path $dir 'trail_off.png'
        Invoke-Nova 'wait 20' | Out-Null; Invoke-Nova "screenshot $on --view scene" | Out-Null
        Invoke-Nova "vfx system.set $root/Swirl_$Api.vfx --system Ribbons --data {\`"output\`":{\`"trail\`":{\`"enabled\`":false},\`"shape\`":\`"Glow\`",\`"intensity\`":0}}" | Out-Null
        Invoke-Nova 'wait 20' | Out-Null; Invoke-Nova "screenshot $off --view scene" | Out-Null
        $c = [NovaImageCompare]::Compare($off, $on, $null)
        Add-Result $suite "${Api}: particle trails (Energy Swirl ribbons, trail only) are drawn" ($c -and $c[2] -gt 2.0) $(if ($c) { 'pixels >8 different: {0:N2}%' -f $c[2] } else { 'capture missing' })
        Invoke-Nova 'delete Swirl' | Out-Null

        # ---- 화면 밖 컬링: 등 뒤의 Visual Effect 는 그리지도 시뮬레이션하지도 않는다, 돌아보면 다시
        Invoke-Nova "create visual-effect --asset $root/Circle_$Api.vfx --name Behind --position 0,0,40" | Out-Null
        Invoke-Nova 'camera --position 0,2,30 --target 0,2,40' | Out-Null
        WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Behind' }).alive -gt 1000 } 15 | Out-Null
        Invoke-Nova 'camera --position 0,2,30 --target 0,2,0' | Out-Null
        $s1 = WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Behind' }).culled } 15
        $c1 = $s1.effects | Where-Object { $_.object -eq 'Behind' }
        Invoke-Nova 'wait 60' | Out-Null
        $c2 = (Stats).effects | Where-Object { $_.object -eq 'Behind' }
        Invoke-Nova 'camera --position 0,2,30 --target 0,2,40' | Out-Null
        $s3 = WaitFor { param($s) -not ($s.effects | Where-Object { $_.object -eq 'Behind' }).culled } 15
        $c3 = $s3.effects | Where-Object { $_.object -eq 'Behind' }
        Add-Result $suite "${Api}: off-screen culling (behind the camera: culled + paused, visible again: resumes)" ($c1.culled -and $c2.culled -and $c2.alive -eq $c1.alive -and -not $c3.culled) ("behind: culled {0} alive {1} -> {2}; back in view: culled {3}" -f $c1.culled, $c1.alive, $c2.alive, $c3.culled)
        Invoke-Nova 'delete Behind' | Out-Null

        # ---- 깊이 버퍼 충돌: 상자 (윗면 y = 1) 위에서 떨어진 파티클이 윗면에 멈춘다, 블록을 끄면 지나 떨어진다
        Invoke-Nova 'create cube --name DepthBox --position 0,0.5,-80 --scale 3,1,3' | Out-Null
        $dj = @{
            systems = @(@{ name = 'Drops'; capacity = 512
                spawn = @{ rate = 0; loop = $false; duration = 0; bursts = @(@{ time = 0; count = 300; cycles = 1; interval = 1 }) }
                initialize = @(
                    @{ type = 'SetPosition'; params = @{ Shape = 'Sphere'; Radius = 0.4; Center = @(0, 1, 0) } },
                    @{ type = 'SetLifetime'; params = @{ Min = 100; Max = 100 } },
                    @{ type = 'SetSize'; params = @{ Min = 0.1; Max = 0.1 } })
                update = @(@{ type = 'Gravity' }, @{ type = 'CollideDepth'; params = @{ Bounce = 0.2; Friction = 0.6; Thickness = 1 } })
                output = @{ blend = 'Additive'; shape = 'Glow' } })
        }
        $df = Join-Path $dir 'depth.json'
        ($dj | ConvertTo-Json -Depth 10) | Set-Content -Encoding utf8 $df
        Invoke-Nova "vfx set $root/Depth_$Api.vfx --file $df" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/Depth_$Api.vfx --name Drops --position 0,3,-80" | Out-Null
        Invoke-Nova 'camera --position 0,4,-87 --target 0,1,-80' | Out-Null
        function DropsMinY { $e = (Stats).effects | Where-Object { $_.object -eq 'Drops' }; if ($e -and $e.bounds) { [double]$e.bounds[0][1] } else { [double]::NaN } }
        WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Drops' }).alive -ge 300 } 10 | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 3) { Invoke-Nova 'wait 20' | Out-Null }
        $restY = DropsMinY
        Invoke-Nova "vfx block.set $root/Depth_$Api.vfx --system Drops --context update --index 1 --enabled false" | Out-Null
        Invoke-Nova 'vfx restart --object Drops' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 3) { Invoke-Nova 'wait 20' | Out-Null }
        $fallY = DropsMinY
        # 경계 = 파티클 자리 - (크기 + 0.5) 여유 (Visual Effect 자리 y 3 둘레 1 m 도 들어간다) → 윗면 (1) 에 멈추면 0.4 쯤
        Add-Result $suite "${Api}: Collide with Depth Buffer (particles rest on a box seen by the camera, fall through when the block is off)" ($restY -gt 0.2 -and $fallY -lt -5) ("lowest y with collision {0:N2}, without {1:N2}" -f $restY, $fallY)
        Invoke-Nova 'delete Drops' | Out-Null
        Invoke-Nova 'delete DepthBox' | Out-Null

        # ---- 사용자 속성: Set Attribute (Life = 0.05) → Get Attribute 를 Set Lifetime 에 → 살아 있는 수가 준다
        Invoke-Nova "vfx new $root/Attr_$Api.vfx --overwrite" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/Attr_$Api.vfx --name Attr --position 0,0,-60" | Out-Null
        Invoke-Nova 'camera --position 0,2,-66 --target 0,1,-60' | Out-Null
        $before = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Attr' }).alive -gt 60 } 15).effects | Where-Object { $_.object -eq 'Attr' }
        Invoke-Nova "vfx attribute.add $root/Attr_$Api.vfx --name Life --type Float" | Out-Null
        Invoke-Nova "vfx block.add $root/Attr_$Api.vfx --system Particles --context initialize --type SetAttribute --index 0 --params {\`"Attribute\`":\`"Life\`",\`"Value\`":0.05}" | Out-Null
        $op = Invoke-NovaJson "vfx op.add $root/Attr_$Api.vfx --type GetAttribute --params {\`"Attribute\`":\`"Life\`"}"
        Invoke-Nova "vfx block.link $root/Attr_$Api.vfx --system Particles --context initialize --index 3 --param Min --from $($op.id)" | Out-Null
        Invoke-Nova "vfx block.link $root/Attr_$Api.vfx --system Particles --context initialize --index 3 --param Max --from $($op.id)" | Out-Null
        $after = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Attr' }).alive -lt 15 } 15).effects | Where-Object { $_.object -eq 'Attr' }
        $info = Invoke-NovaJson "vfx info $root/Attr_$Api.vfx"
        Add-Result $suite "${Api}: custom attribute (Set Attribute Life 0.05 -> Get Attribute -> Set Lifetime) shortens lives" ($before.alive -gt 60 -and $after.alive -lt 15 -and $info.issues.Count -eq 0) ("alive {0} -> {1}, issues {2}" -f $before.alive, $after.alive, $info.issues.Count)
        Invoke-Nova 'delete Attr' | Out-Null

        # ---- Sub Graph: Float 0.025 → Sub Graph (In × 2) → Set Lifetime = 0.05 → 적다. Sub Graph 파일의 배율을 200 으로 → 다시 많다 (파일이 바뀌면 다시 만든다)
        Invoke-Nova "vfx subgraph.new $root/Scale_$Api.vfxoperator --overwrite" | Out-Null
        Invoke-Nova "vfx new $root/Sub_$Api.vfx --overwrite" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/Sub_$Api.vfx --name Sub --position 0,0,-60" | Out-Null
        $f1 = Invoke-NovaJson "vfx op.add $root/Sub_$Api.vfx --type Float --params {\`"Value\`":0.025}"
        $sg = Invoke-NovaJson "vfx op.add $root/Sub_$Api.vfx --type SubGraph --params {\`"Path\`":\`"$root/Scale_$Api.vfxoperator\`"}"
        $con = Invoke-Nova "vfx op.connect $root/Sub_$Api.vfx --from $($f1.id) --to $($sg.id) --input In"
        Invoke-Nova "vfx block.link $root/Sub_$Api.vfx --system Particles --context initialize --index 2 --param Min --from $($sg.id)" | Out-Null
        Invoke-Nova "vfx block.link $root/Sub_$Api.vfx --system Particles --context initialize --index 2 --param Max --from $($sg.id)" | Out-Null
        $short = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Sub' }).alive -lt 15 } 15).effects | Where-Object { $_.object -eq 'Sub' }
        Invoke-Nova "vfx op.set $root/Scale_$Api.vfxoperator --id 2 --params {\`"B\`":200}" | Out-Null
        $long = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Sub' }).alive -gt 60 } 15).effects | Where-Object { $_.object -eq 'Sub' }
        $info = Invoke-NovaJson "vfx info $root/Sub_$Api.vfx"
        Add-Result $suite "${Api}: Sub Graph operator (file inputs, edits to the .vfxoperator re-encode users)" ($short.alive -lt 15 -and $long.alive -gt 60 -and $info.issues.Count -eq 0 -and $con -notmatch '"error"') ("alive x2 -> {0}, x200 -> {1}, issues {2}" -f $short.alive, $long.alive, $info.issues.Count)
        Invoke-Nova 'delete Sub' | Out-Null

        # ---- Output Mesh (Debris: Crystal 메시 · 불투명 · 빛): 켬 · 끔 화면 차이
        Invoke-Nova "vfx new $root/Debris_$Api.vfx --template Debris --overwrite" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/Debris_$Api.vfx --name Debris --position 0,0,-100" | Out-Null
        # 가까이 (파편이 작아 Release 처럼 빠른 빌드에서는 화면에 남는 시간이 짧다)
        Invoke-Nova 'camera --position 0,2.2,-104 --target 0,0.6,-100' | Out-Null
        WaitFor { param($s) (Sys $s 'Debris' 'Shards').alive -gt 100 } 15 | Out-Null
        $on = Join-Path $dir 'mesh_on.png'; $off = Join-Path $dir 'mesh_off.png'
        Invoke-Nova 'wait 10' | Out-Null; Invoke-Nova "screenshot $on --view scene" | Out-Null
        Invoke-Nova 'set Debris --active false' | Out-Null; Invoke-Nova 'wait 20' | Out-Null; Invoke-Nova "screenshot $off --view scene" | Out-Null
        $c = [NovaImageCompare]::Compare($off, $on, $null)
        Add-Result $suite "${Api}: Output Particle Mesh (Debris: lit Crystal shards, opaque) is drawn" ($c -and $c[2] -gt 1.0) $(if ($c) { 'pixels >8 different: {0:N2}%' -f $c[2] } else { 'capture missing' })
        Invoke-Nova 'delete Debris' | Out-Null

        # ---- Compare · Branch: 수명 = Branch(Compare(1 ? 0), 0.05, 5) — Greater 면 짧고, Less 로 바꾸면 길다
        Invoke-Nova "vfx new $root/Logic_$Api.vfx --overwrite" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/Logic_$Api.vfx --name Logic --position 0,0,-60" | Out-Null
        Invoke-Nova 'camera --position 0,2,-66 --target 0,1,-60' | Out-Null
        $cmp = Invoke-NovaJson "vfx op.add $root/Logic_$Api.vfx --type Compare --params {\`"A\`":1,\`"B\`":0,\`"Condition\`":\`"Greater\`"}"
        $br = Invoke-NovaJson "vfx op.add $root/Logic_$Api.vfx --type Branch --params {\`"True\`":0.05,\`"False\`":5}"
        Invoke-Nova "vfx op.connect $root/Logic_$Api.vfx --from $($cmp.id) --to $($br.id) --input Predicate" | Out-Null
        Invoke-Nova "vfx block.link $root/Logic_$Api.vfx --system Particles --context initialize --index 2 --param Min --from $($br.id)" | Out-Null
        Invoke-Nova "vfx block.link $root/Logic_$Api.vfx --system Particles --context initialize --index 2 --param Max --from $($br.id)" | Out-Null
        $short = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Logic' }).alive -lt 15 } 15).effects | Where-Object { $_.object -eq 'Logic' }
        Invoke-Nova "vfx op.set $root/Logic_$Api.vfx --id $($cmp.id) --params {\`"Condition\`":\`"Less\`"}" | Out-Null
        $long = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'Logic' }).alive -gt 60 } 15).effects | Where-Object { $_.object -eq 'Logic' }
        Add-Result $suite "${Api}: Compare + Branch operators pick the lifetime (1 > 0 -> 0.05 s, 1 < 0 -> 5 s)" ($short.alive -lt 15 -and $long.alive -gt 60) ("Greater: alive {0}, Less: alive {1}" -f $short.alive, $long.alive)
        Invoke-Nova 'delete Logic' | Out-Null

        # ---- 모델 파일 메시 (glTF — 2 x 1 x 2 상자, 윗면 y 1, 면마다 평평한 법선): Collide with SDF 에 멈춘다 · Output Mesh 로 그린다
        $objDir = Join-Path $Project 'Assets\VFX\Test'
        New-Item -ItemType Directory -Force $objDir | Out-Null
        # 버퍼 (자리 24 · 법선 24 · 인덱스 36, 바깥에서 보아 반시계) — 만든 스크립트: 상자 면마다 네 모서리
        $slabB64 = 'AACAvwAAgD8AAIC/AACAvwAAgD8AAIA/AACAPwAAgD8AAIA/AACAPwAAgD8AAIC/AACAPwAAAAAAAIC/AACAPwAAAAAAAIA/AACAvwAAAAAAAIA/AACAvwAAAAAAAIC/AACAPwAAAAAAAIC/AACAPwAAgD8AAIC/AACAPwAAgD8AAIA/AACAPwAAAAAAAIA/AACAvwAAAAAAAIA/AACAvwAAgD8AAIA/AACAvwAAgD8AAIC/AACAvwAAAAAAAIC/AACAvwAAAAAAAIA/AACAPwAAAAAAAIA/AACAPwAAgD8AAIA/AACAvwAAgD8AAIA/AACAvwAAgD8AAIC/AACAPwAAgD8AAIC/AACAPwAAAAAAAIC/AACAvwAAAAAAAIC/AAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAvwAAAAAAAAAAAACAvwAAAAAAAAAAAACAvwAAAAAAAAAAAACAvwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAABAAIAAAACAAMABAAFAAYABAAGAAcACAAJAAoACAAKAAsADAANAA4ADAAOAA8AEAARABIAEAASABMAFAAVABYAFAAWABcA'
        $gltf = @{
            asset = @{ version = '2.0' }; scene = 0; scenes = @(@{ nodes = @(0) }); nodes = @(@{ mesh = 0; name = 'Slab' })
            meshes = @(@{ primitives = @(@{ attributes = @{ POSITION = 0; NORMAL = 1 }; indices = 2 }) })
            buffers = @(@{ byteLength = 648; uri = 'data:application/octet-stream;base64,' + $slabB64 })
            bufferViews = @(@{ buffer = 0; byteOffset = 0; byteLength = 288; target = 34962 }, @{ buffer = 0; byteOffset = 288; byteLength = 288; target = 34962 }, @{ buffer = 0; byteOffset = 576; byteLength = 72; target = 34963 })
            accessors = @(@{ bufferView = 0; componentType = 5126; count = 24; type = 'VEC3'; min = @(-1, 0, -1); max = @(1, 1, 1) }, @{ bufferView = 1; componentType = 5126; count = 24; type = 'VEC3' }, @{ bufferView = 2; componentType = 5123; count = 36; type = 'SCALAR' })
        }
        ($gltf | ConvertTo-Json -Depth 10 -Compress) | Set-Content -Encoding ascii (Join-Path $objDir "Slab_$Api.gltf")
        $sj = @{
            systems = @(@{ name = 'Drops'; capacity = 512
                spawn = @{ rate = 0; loop = $false; duration = 0; bursts = @(@{ time = 0; count = 300; cycles = 1; interval = 1 }) }
                initialize = @(
                    @{ type = 'SetPosition'; params = @{ Shape = 'Sphere'; Radius = 0.4; Center = @(0, 1, 0) } },
                    @{ type = 'SetLifetime'; params = @{ Min = 100; Max = 100 } },
                    @{ type = 'SetSize'; params = @{ Min = 0.1; Max = 0.1 } })
                update = @(@{ type = 'Gravity' }, @{ type = 'CollideSDF'; params = @{ Mesh = "$root/Slab_$Api.gltf"; Position = @(0, -3, 0); Radius = 0.05; Bounce = 0.2; Friction = 0.6 } })
                output = @{ blend = 'Additive'; shape = 'Glow' } })
        }
        $sf = Join-Path $dir 'sdf.json'
        ($sj | ConvertTo-Json -Depth 10) | Set-Content -Encoding utf8 $sf
        Invoke-Nova "vfx set $root/Sdf_$Api.vfx --file $sf" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/Sdf_$Api.vfx --name SdfDrops --position 0,3,-120" | Out-Null
        Invoke-Nova 'camera --position 0,4,-127 --target 0,1,-120' | Out-Null
        function SdfMinY { $e = (Stats).effects | Where-Object { $_.object -eq 'SdfDrops' }; if ($e -and $e.bounds) { [double]$e.bounds[0][1] } else { [double]::NaN } }
        WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'SdfDrops' }).alive -ge 300 } 10 | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 3) { Invoke-Nova 'wait 20' | Out-Null }
        $restY = SdfMinY
        $sdfInfo = Invoke-NovaJson "vfx info $root/Sdf_$Api.vfx"
        Invoke-Nova "vfx block.set $root/Sdf_$Api.vfx --system Drops --context update --index 1 --enabled false" | Out-Null
        Invoke-Nova 'vfx restart --object SdfDrops' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 3) { Invoke-Nova 'wait 20' | Out-Null }
        $fallY = SdfMinY
        # 상자 윗면 (월드 y 1) 에 멈추면 경계 = 1 - (크기 0.1 + 0.5) 쯤
        Add-Result $suite "${Api}: Collide with SDF (baked from a glTF model: particles rest on the slab top, fall through when off)" ($restY -gt 0.2 -and $restY -lt 1.0 -and $fallY -lt -5 -and @($sdfInfo.issues).Count -eq 0) ("lowest y with SDF {0:N2}, without {1:N2}, issues {2}" -f $restY, $fallY, @($sdfInfo.issues).Count)
        # 같은 glTF 를 Output Mesh 로 (파티클마다 상자): 켬 · 끔 화면 차이
        Invoke-Nova "vfx block.set $root/Sdf_$Api.vfx --system Drops --context update --index 1 --enabled true" | Out-Null
        Invoke-Nova "vfx system.set $root/Sdf_$Api.vfx --system Drops --data {\`"output\`":{\`"shape\`":\`"Mesh\`",\`"mesh\`":\`"$root/Slab_$Api.gltf\`",\`"blend\`":\`"Opaque\`"}}" | Out-Null
        Invoke-Nova 'vfx restart --object SdfDrops' | Out-Null
        $sw = [Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt 1.5) { Invoke-Nova 'wait 20' | Out-Null }
        $on = Join-Path $dir 'objmesh_on.png'; $off = Join-Path $dir 'objmesh_off.png'
        Invoke-Nova "screenshot $on --view scene" | Out-Null
        Invoke-Nova 'set SdfDrops --active false' | Out-Null; Invoke-Nova 'wait 20' | Out-Null; Invoke-Nova "screenshot $off --view scene" | Out-Null
        $c = [NovaImageCompare]::Compare($off, $on, $null)
        # 0.1 m 크기 판 300 개가 보이지 않는 거리장 판 위에 쌓인 더미 — 화면에서 작다 (0.1 % 넘으면 그린 것)
        Add-Result $suite "${Api}: Output Mesh from a model file (glTF slab per particle) is drawn" ($c -and $c[2] -gt 0.1) $(if ($c) { 'pixels >8 different: {0:N2}%' -f $c[2] } else { 'capture missing' })
        Invoke-Nova 'delete SdfDrops' | Out-Null

        # ---- Block Sub Graph: Life.vfxblock (입력 Life → Set Lifetime) 을 Initialize 에 — Life 0.05 면 적고, 5 면 많다
        $bj = @{
            version = 1
            properties = @(@{ name = 'Life'; type = 'Float'; value = 2 })
            systems = @(@{ name = 'Block'; capacity = 1; spawn = @{ rate = 0 }
                initialize = @(@{ type = 'SetLifetime'; bind = @{ Min = 'Life'; Max = 'Life' } }); update = @(); output = @{ shape = 'Glow' } })
        }
        $bf = Join-Path $dir 'lifeblock.json'
        ($bj | ConvertTo-Json -Depth 10) | Set-Content -Encoding utf8 $bf
        $bnew = Invoke-Nova "vfx blockgraph.new $root/Life_$Api.vfxblock --overwrite"
        Invoke-Nova "vfx set $root/Life_$Api.vfxblock --file $bf" | Out-Null
        Invoke-Nova "vfx new $root/BlockSub_$Api.vfx --overwrite" | Out-Null
        Invoke-Nova "create visual-effect --asset $root/BlockSub_$Api.vfx --name BlockSub --position 0,0,-60" | Out-Null
        Invoke-Nova 'camera --position 0,2,-66 --target 0,1,-60' | Out-Null
        Invoke-Nova "vfx block.add $root/BlockSub_$Api.vfx --system Particles --context initialize --type SubgraphBlock --params {\`"Path\`":\`"$root/Life_$Api.vfxblock\`",\`"Life\`":0.05}" | Out-Null
        $short = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'BlockSub' }).alive -lt 15 } 15).effects | Where-Object { $_.object -eq 'BlockSub' }
        Invoke-Nova "vfx block.set $root/BlockSub_$Api.vfx --system Particles --context initialize --index 5 --params {\`"Life\`":5}" | Out-Null
        $long = (WaitFor { param($s) ($s.effects | Where-Object { $_.object -eq 'BlockSub' }).alive -gt 60 } 15).effects | Where-Object { $_.object -eq 'BlockSub' }
        $info = Invoke-NovaJson "vfx info $root/BlockSub_$Api.vfx"
        Add-Result $suite "${Api}: Block Sub Graph (.vfxblock input Life -> Set Lifetime inside)" ($short.alive -lt 15 -and $long.alive -gt 60 -and @($info.issues).Count -eq 0 -and $bnew -notmatch '"error"') ("Life 0.05: alive {0}, Life 5: alive {1}, issues {2}" -f $short.alive, $long.alive, @($info.issues).Count)
        Invoke-Nova 'delete BlockSub' | Out-Null

        if ($Api -eq 'dx')
        {
            # ---- C# API (Unity 의 UnityEngine.VFX.VisualEffect)
            $cs = Join-Path $dir 'vfx.cs'
            @'
var v = GameObject.Find("Circle").GetComponent<NovaEngine.VFX.VisualEffect>();
v.SetFloat("Spin", 123f);
v.SetVector4("Main Color", new Vector4(1f, 0.2f, 0.1f, 1f));
return (v != null) + " " + v.GetFloat("Spin") + " " + v.HasFloat("Spin") + " " + v.HasVector4("Main Color") + " " + v.HasFloat("Nope") + " " + (v.aliveParticleCount > 0) + " " + v.visualEffectAsset;
'@ | Set-Content -Encoding utf8 $cs
            $r = (Invoke-NovaJson "exec --file $cs").result
            Add-Result $suite 'C# VisualEffect: SetFloat/GetFloat, HasFloat/HasVector4, aliveParticleCount, visualEffectAsset' ($r -match '^True 123 True True False True Assets/VFX/Test/Circle_dx\.vfx$') "$r"
            # ---- 컴포넌트 JSON (씬에 저장되는 값): 에셋 · 덮어쓰기
            $comp = Invoke-NovaJson 'get Circle --component VisualEffect'
            $ov = $comp.overrides | Where-Object { $_.name -eq 'Spin' }
            Add-Result $suite 'component JSON: asset + overrides saved' ($comp.asset -eq "$root/Circle_dx.vfx" -and $ov.value[0] -eq 123) ("asset {0}, Spin {1}" -f $comp.asset, $ov.value[0])
            # ---- VFX Assistant: 이 PC 의 Claude Code 를 찾는다 (실제 대화는 로그인이 필요해 자동 검사에서 보내지 않는다)
            Invoke-Nova 'vfx assistant' | Out-Null; Invoke-Nova 'wait 10' | Out-Null
            $st = Invoke-NovaJson 'vfx assistant.status'
            Add-Result $suite 'VFX Assistant window + status (local Claude Code, no API)' ($null -ne $st -and $st.running -eq $false) ("running {0}, log {1}" -f $st.running, $st.log.Count)
        }
        Invoke-Nova 'log --errors -n 5' | Out-Null
    }
    finally
    {
        Write-Host "  $(Stop-TestEditor $ed)"
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
                'tilemap' { Suite-Tilemap }
                'web' { Suite-Web }
                'scenes' { Suite-Scenes }
                'tween' { Suite-Tween }
                'light2d' { Suite-Light2D }
                'nav2d' { Suite-Nav2D }
                'ragdoll' { Suite-Ragdoll }
                'wheel' { Suite-Wheel }
                'daynight' { Suite-DayNight }
                'cloth' { Suite-Cloth }
                'clothskin' { Suite-ClothSkin }
                'starter' { Suite-Starter }
                'behaviour' { Suite-Behaviour }
                'layers' { Suite-Layers }
                'sprites' { Suite-Sprites }
                'physics2d' { Suite-Physics2D }
                'shadergraph' { Suite-ShaderGraph }
                'decal' { Suite-Decal }
                'reflectionprobe' { Suite-ReflectionProbe }
                'probevolume' { Suite-ProbeVolume }
                'depthoffield' { Suite-DepthOfField }
                'lodgroup' { Suite-LODGroup }
                'occlusion' { Suite-Occlusion }
                'occlusiongl' { Suite-OcclusionGL }
                'material' { Suite-Material }
                'materialgl' { Suite-Material -Api gl }
                'materialvk' { Suite-Material -Api vk }
                'occlusionvk' { Suite-OcclusionVK }
                'linetrail' { Suite-LineTrail }
                'ssr' { Suite-SSR }
                'ssao' { Suite-SSAO }
                'motionvectors' { Suite-MotionVectors }
                'cinemachine' { Suite-Cinemachine }
                'renderingdebug' { Suite-RenderingDebug }
                'forwardplus' { Suite-ForwardPlus }
                'rendergraph' { Suite-RenderGraph }
                'modelplace' { Suite-ModelPlace }
                'antialiasing' { Suite-AntiAliasing }
                'audio' { Suite-Audio }
                'recovery' { Suite-Recovery }
                'render' { Suite-Render }
                'gfx' { Suite-Gfx }
                'vulkan' { Suite-Vulkan }
                'perf' { Suite-Perf }
                'particles' { Suite-Particles }
                'vfx' { Suite-Vfx }
                'vfxgl' { Suite-Vfx -Api gl }
                'vfxvk' { Suite-Vfx -Api vk }
                'vfx12' { Suite-Vfx -Api 12 }
                'd3d12' { Suite-D3D12 }
                'virtualtexture' { Suite-VirtualTexture }
                'deferred' { Suite-Deferred }
                'jobs' { Suite-Jobs }
                'physicsasync' { Suite-PhysicsAsync }
                'renderthread' { Suite-RenderThread }
                'memory' { Suite-Memory }
                'transform' { Suite-Transform }
                'streaming' { Suite-Streaming }
                'weather' { Suite-Weather }
                'tessellation' { Suite-Tessellation }
                'tessellationgl' { Suite-Tessellation -Api gl }
                'tessellationvk' { Suite-Tessellation -Api vk }
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
