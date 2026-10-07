# 낮 · 밤 + 날씨 데모 장면 (거리 · 건물 창문 · 가로등 · 거울 구 · 반사 프로브 · 비) 를 CLI 로 만든다 — docs/DAY_NIGHT.md
#  해가 지면 가로등 (NightLight) 이 하나씩 켜지고 창문이 밝아진다. 반사 프로브는 30 게임 분마다 다시 찍혀 젖은 길 · 거울 구에 밤 하늘과 등불이 비친다.
#  에디터를 프로젝트로 연 뒤: powershell build_street_scene.ps1 -Project <프로젝트 폴더> [-Nova <nova.exe 경로>]
#  (패키지 com.nova.daynight · com.nova.weather 를 넣는다 — 이미 있으면 그대로)
param([string]$Project = 'E:\NovaTest\ScriptTest', [string]$Nova = 'nova', [string]$Folder = 'DayNightStreet')
$nova = $Nova
# 그 프로젝트를 연 에디터에만 보낸다 (--project)
function N { param([Parameter(ValueFromRemainingArguments = $true)][string[]]$a) $o = & $nova @a --project $Project 2>&1 | Out-String; if ($o -match 'error|unknown|usage') { Write-Host "!! $($a -join ' ') -> $($o.Trim())" }; return $o }
$matDir = Join-Path $Project "Assets\$Folder"
New-Item -ItemType Directory -Force $matDir | Out-Null
function Mat([string]$name, [double[]]$c, [double]$smooth, [double]$metal = 0.0, [double[]]$emis = $null)
{
    $m = [ordered]@{ Shader = 'Universal Render Pipeline/Lit'; ResourcePath = "Assets\$Folder\$name.mat"; BaseMapPath = ''; NormalMapPath = ''; MetallicMapPath = ''; OcclusionMapPath = ''; EmissionMapPath = ''
        BaseColor = @($c[0], $c[1], $c[2], 1); Metallic = $metal; Smoothness = $smooth; SmoothnessSource = 0; NormalScale = 1.0; OcclusionStrength = 1.0; Tiling = @(1, 1); Offset = @(0, 0)
        AlphaClipping = 0; Cutoff = 0.5; ReceiveShadows = 1; SpecularHighlights = 1; EnvironmentReflections = 1; Emission = [bool]$emis; EmissionColor = $(if ($emis) { @($emis[0], $emis[1], $emis[2]) } else { @(0, 0, 0) })
        EmissionIntensity = 1.0; Priority = 0; UseShadowMap = 1 }
    $m | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $matDir "$name.mat")
}
function Box([string]$name, [string]$pos, [string]$scale, [string]$mat, [string]$parent = '')
{
    if ($parent) { N create cube --name $name --position $pos --scale $scale --parent $parent | Out-Null } else { N create cube --name $name --position $pos --scale $scale | Out-Null }
    N set $name --component MeshRenderer --values ('{"m_MaterialPaths":["Assets/' + $Folder + '/' + $mat + '.mat"]}').Replace('"', '\"') | Out-Null
}

Mat 'Asphalt' @(0.16, 0.16, 0.17) 0.35
Mat 'Sidewalk' @(0.48, 0.47, 0.45) 0.25
Mat 'WallA' @(0.62, 0.52, 0.42) 0.15
Mat 'WallB' @(0.42, 0.46, 0.52) 0.15
Mat 'WallC' @(0.55, 0.36, 0.30) 0.15
Mat 'Window' @(0.10, 0.12, 0.16) 0.9                   # 낮 = 어두운 유리, 밤 = NightLight 가 발광
Mat 'Pole' @(0.12, 0.13, 0.14) 0.5 0.6
Mat 'Bulb' @(0.95, 0.9, 0.8) 0.6
Mat 'Mirror' @(0.95, 0.95, 0.95) 1.0 1.0

N package add com.nova.daynight | Out-Null
N package add com.nova.weather | Out-Null
# 패키지 C# (NightLight) 컴파일이 끝날 때까지
for ($t = 0; $t -lt 60; $t++)
{
    & $nova wait 30 --project $Project 2>&1 | Out-Null
    $info = try { (& $nova info --json --project $Project 2>&1 | Out-String) | ConvertFrom-Json } catch { $null }
    if ($info -and -not $info.compiling) { break }
}
N scene new --force | Out-Null

# 길 (x 방향) · 양쪽 보도
Box 'Road' '0,-0.05,0' '60,0.1,8' 'Asphalt'
Box 'Sidewalk N' '0,0.05,6' '60,0.3,4' 'Sidewalk'
Box 'Sidewalk S' '0,0.05,-6' '60,0.3,4' 'Sidewalk'

# 건물 (북쪽 줄) — 창문 (NightLight: 밤에 하나씩 켜진다)
$walls = @('WallA', 'WallB', 'WallC', 'WallA', 'WallB')
$i = 0
foreach ($x in @(-20, -10, 0, 10, 20))
{
    $h = 8 + ($i % 3) * 3
    $b = "Building $i"
    Box $b "$x,$($h / 2),12" "9,$h,6" $walls[$i]
    $w = 0
    for ($fy = 2.0; $fy -lt $h - 1; $fy += 2.6)
    {
        foreach ($fx in @(-3, -1, 1, 3))
        {
            $wn = "Window $i-$w"
            Box $wn "$($x + $fx),$fy,8.96" '1.1,1.4,0.1' 'Window'
            N add-component $wn NightLight --values '{\"randomDelay\":6,\"turnOnBelow\":0}' | Out-Null
            $w++
        }
    }
    $i++
}

# 가로등 (남쪽 보도, 10 m 마다): 기둥 · 팔 · 전구 (NightLight) + 전구 아래 Point Light
$k = 0
foreach ($x in @(-25, -15, -5, 5, 15, 25))
{
    Box "Pole $k" "$x,2.4,-4.6" '0.14,4.8,0.14' 'Pole'
    Box "Arm $k" "$x,4.75,-4.0" '0.1,0.1,1.3' 'Pole'
    N create sphere --name "Lamp $k" --position "$x,4.55,-3.4" --scale '0.42,0.3,0.42' | Out-Null
    N set "Lamp $k" --component MeshRenderer --values ('{"m_MaterialPaths":["Assets/' + $Folder + '/Bulb.mat"]}').Replace('"', '\"') | Out-Null
    N add-component "Lamp $k" NightLight --values '{\"randomDelay\":2.5}' | Out-Null
    N create point-light --name "Lamp Light $k" --parent "Lamp $k" --position '0,-0.6,0' | Out-Null
    N set "Lamp Light $k" --component Light --values '{\"pointLightRange\":14.0,\"intensity\":7.0,\"pointLightDiffuse\":[1.0,0.78,0.5,1]}' | Out-Null
    $k++
}

# 거울 구 (반사를 보기 좋게) · 반사 프로브 (구워 두면 낮 · 밤이 시각마다 다시 찍는다)
N create sphere --name 'Mirror Ball' --position '2,1.0,-1' --scale '1.6,1.6,1.6' | Out-Null
N set 'Mirror Ball' --component MeshRenderer --values ('{"m_MaterialPaths":["Assets/' + $Folder + '/Mirror.mat"]}').Replace('"', '\"') | Out-Null
N create empty --name 'Street Probe' --position '0,2,0' | Out-Null
N add-component 'Street Probe' ReflectionProbe --values '{\"size\":[64,20,30],\"resolution\":256,\"boxProjection\":false}' | Out-Null

# 낮 · 밤 (3 분에 하루, 17:30 부터) · 날씨 (비)
N create empty --name 'Time Of Day' | Out-Null
N add-component 'Time Of Day' DayNightCycle --values '{\"timeOfDay\":17.5,\"dayLengthMinutes\":3,\"probeRefreshMinutes\":30}' | Out-Null
N create empty --name 'Weather' | Out-Null
N add-component 'Weather' WeatherController | Out-Null
N weather set --profile Rain --seconds 0 | Out-Null

N set 'Main Camera' --position '-9,2.2,-9' --rotation '6,38,0' | Out-Null
N scene save --as "Assets/$Folder/Street.scene" | Out-Null
N daynight set --time 12 | Out-Null
N probe bake | Out-Null
N daynight set --time 17.5 | Out-Null
N scene save | Out-Null
Write-Host "street scene: Assets/$Folder/Street.scene (Play: 3 minutes a day, rain)"
