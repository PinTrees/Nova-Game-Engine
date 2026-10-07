# Starter Assets 데모 장면 (차에 타고 내리기 · 래그돌 표적 쓰러뜨리고 일어나기) 을 CLI 로 만든다 — docs/STARTER_ASSETS.md
#  Play: WASD 로 걷고 차 옆에서 E = 타기 (차 뒤 카메라, W/S/A/D · Space 손 브레이크), 다시 E = 운전석 쪽에 내리기.
#  표적을 클릭하면 래그돌로 쓰러지고 3 초 뒤 누운 방향 (등 · 배) 의 일어나기 클립으로 쓰러진 자세에서 섞여 일어난다. 상자 더미는 차로 들이받는다.
#  에디터를 프로젝트로 연 뒤: powershell build_starter_demo.ps1 -Project <프로젝트 폴더> [-Nova <nova.exe 경로>]
#  (패키지 com.nova.starter-assets 를 넣는다 — 이미 있으면 그대로)
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Nova = 'nova', [string]$Folder = 'StarterDemo')
$nova = $Nova
# 그 프로젝트를 연 에디터에만 보낸다 (--project)
function N { param([Parameter(ValueFromRemainingArguments = $true)][string[]]$a) $o = & $nova @a --project $Project 2>&1 | Out-String; if ($o -match 'error|unknown|usage') { Write-Host "!! $($a -join ' ') -> $($o.Trim())" }; return $o }
$matDir = Join-Path $Project "Assets\$Folder"
New-Item -ItemType Directory -Force $matDir | Out-Null
function Mat([string]$name, [double[]]$c, [double]$smooth)
{
    $m = [ordered]@{ Shader = 'Universal Render Pipeline/Lit'; ResourcePath = "Assets\$Folder\$name.mat"; BaseMapPath = ''; NormalMapPath = ''; MetallicMapPath = ''; OcclusionMapPath = ''; EmissionMapPath = ''
        BaseColor = @($c[0], $c[1], $c[2], 1); Metallic = 0.0; Smoothness = $smooth; SmoothnessSource = 0; NormalScale = 1.0; OcclusionStrength = 1.0; Tiling = @(1, 1); Offset = @(0, 0)
        AlphaClipping = 0; Cutoff = 0.5; ReceiveShadows = 1; SpecularHighlights = 1; EnvironmentReflections = 1; Emission = $false; EmissionColor = @(0, 0, 0)
        EmissionIntensity = 1.0; Priority = 0; UseShadowMap = 1 }
    $m | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $matDir "$name.mat")
}
function Paint([string]$name, [string]$mat) { N set $name --component MeshRenderer --values ('{"m_MaterialPaths":["Assets/' + $Folder + '/' + $mat + '.mat"]}').Replace('"', '\"') | Out-Null }
function Box([string]$name, [string]$pos, [string]$scale, [string]$mat, [string]$rot = '0,0,0')
{
    N create cube --name $name --position $pos --scale $scale --rotation $rot | Out-Null
    Paint $name $mat
}

Mat 'Grass' @(0.33, 0.45, 0.28) 0.1
Mat 'Road' @(0.2, 0.2, 0.22) 0.3
Mat 'Line' @(0.9, 0.85, 0.6) 0.3
Mat 'Crate' @(0.72, 0.52, 0.3) 0.2
Mat 'Ramp' @(0.55, 0.57, 0.6) 0.3

N scene new --force | Out-Null
Box 'Ground' '0,-0.5,60' '160,1,200' 'Grass'
Box 'Road' '0,0.01,60' '8,0.02,150' 'Road'
for ($z = 0; $z -lt 130; $z += 6) { Box "Line $z" "0,0.025,$z" '0.2,0.01,2.5' 'Line' }
Box 'Ramp' '-12,0.6,30' '5,0.3,8' 'Ramp' '-12,0,0'

# 차로 들이받는 상자 더미 (3 단)
$i = 0
foreach ($row in @(@(0, 0.45), @(1, 1.35), @(2, 2.25)))
{
    for ($k = 0; $k -lt 3 - $row[0]; $k++)
    {
        $x = 1.5 + $k * 0.9 + $row[0] * 0.45   # 차 (x 2.4) 가 곧장 달리면 맞는 자리
        $name = "Crate $i"; $i++
        Box $name "$x,$($row[1]),38" '0.85,0.85,0.85' 'Crate'
        N add-component $name RigidBody | Out-Null
    }
}

# 플레이어 (3인칭 + 차 타기) · 차 · 표적 3 (3 초 뒤 일어남)
N create car --name Car --position '2.4,0,1' | Out-Null   # 플레이어 (원점) 가 E 로 탈 수 있는 거리 (3.5 m 안)
N create player | Out-Null
foreach ($d in @(@('Dummy A', '-3,0,8'), @('Dummy B', '-4.5,0,11'), @('Dummy C', '-2,0,13')))
{
    N create ragdoll-target --name $d[0] --position $d[1] --rotation '0,180,0' | Out-Null
    N set $d[0] --component RagdollTarget --values '{\"recoverAfter\":3}' | Out-Null
}
N scene save --as "Assets/$Folder/StarterDemo.scene" | Out-Null
Write-Host "Assets/$Folder/StarterDemo.scene — Play: WASD, E near the car, click a dummy"
