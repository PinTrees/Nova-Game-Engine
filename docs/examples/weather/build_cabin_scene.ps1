# 날씨 데모 장면 (오두막 · 돌 마당 · 처마 · 숲 · 걷는 사람 · Weather Controller · WeatherDirector) 을 CLI 로 만든다 — docs/WEATHER.md
#  미리: 프로젝트 Packages/manifest.json 에 com.nova.weather · com.nova.animation · com.nova.cameras · com.nova.starter-assets,
#        Assets/Scripts 에 WeatherDirector.cs · Walker.cs (이 폴더), 장면에 Terrain (높낮이 있는 .terraindata — 바닥 높이 16 m 근처, 가운데 (200, 90))
#  에디터를 그 프로젝트로 연 뒤: powershell build_cabin_scene.ps1 -Project <프로젝트 폴더> [-Nova <nova.exe 경로>]
param([string]$Project = 'E:\NovaTest\WeatherDemo', [string]$Nova = 'nova')
$nova = $Nova
function N { param([Parameter(ValueFromRemainingArguments = $true)][string[]]$a) $o = & $nova @a 2>&1 | Out-String; if ($o -match 'error|unknown|usage') { Write-Host "!! $($a -join ' ') -> $($o.Trim())" } }
function Mat([string]$name, [double[]]$c, [double]$smooth, [double[]]$emis = $null)
{
    $m = [ordered]@{ Shader = 'Universal Render Pipeline/Lit'; ResourcePath = "Assets\Materials\$name.mat"; BaseMapPath = ''; NormalMapPath = ''; MetallicMapPath = ''; OcclusionMapPath = ''; EmissionMapPath = ''
        BaseColor = @($c[0], $c[1], $c[2], 1); Metallic = 0.0; Smoothness = $smooth; SmoothnessSource = 0; NormalScale = 1.0; OcclusionStrength = 1.0; Tiling = @(1, 1); Offset = @(0, 0)
        AlphaClipping = 0; Cutoff = 0.5; ReceiveShadows = 1; SpecularHighlights = 1; EnvironmentReflections = 1; Emission = [bool]$emis; EmissionColor = $(if ($emis) { @($emis[0], $emis[1], $emis[2]) } else { @(0, 0, 0) })
        EmissionIntensity = 1.0; Priority = 0; UseShadowMap = 1 }
    New-Item -ItemType Directory -Force "$Project\Assets\Materials" | Out-Null
    $m | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 "$Project\Assets\Materials\$name.mat"
}
function Box([string]$name, [string]$pos, [string]$scale, [string]$mat, [string]$rot = '0,0,0')
{
    N create cube --name $name --position $pos --scale $scale --rotation $rot
    N set $name --component MeshRenderer --values ('{"m_MaterialPaths":["Assets/Materials/' + $mat + '.mat"]}').Replace('"', '\"')
}

Mat 'Wood' @(0.40, 0.26, 0.16) 0.2
Mat 'DarkWood' @(0.18, 0.11, 0.07) 0.25
Mat 'Roof' @(0.22, 0.13, 0.11) 0.3
Mat 'Stone' @(0.46, 0.45, 0.43) 0.3
Mat 'Window' @(1.0, 0.75, 0.45) 0.6 @(4.0, 2.4, 1.0)

# 돌 마당 (웅덩이 · 발자국이 잘 보인다)
Box 'Patio' '200,16.25,85.5' '11,0.4,9' 'Stone'
# 오두막 (바닥 16.3)
Box 'Wall Back' '200,17.8,94' '6,3,0.25' 'Wood'
Box 'Wall Front' '200,17.8,90' '6,3,0.25' 'Wood'
Box 'Wall Left' '197,17.8,92' '0.25,3,4' 'Wood'
Box 'Wall Right' '203,17.8,92' '0.25,3,4' 'Wood'
Box 'Door' '200,17.3,89.85' '1,2,0.06' 'DarkWood'
Box 'Window L' '198.4,18,89.85' '1,0.8,0.05' 'Window'
Box 'Window R' '201.6,18,89.85' '1,0.8,0.05' 'Window'
Box 'Roof L' '198.3,20.28,92' '3.93,0.2,5.4' 'Roof' '0,0,30'
Box 'Roof R' '201.7,20.28,92' '3.93,0.2,5.4' 'Roof' '0,0,-30'
# 처마 (현관 지붕) — 아래는 마르고 눈이 없다
Box 'Porch Roof' '200,18.95,88.4' '6.2,0.15,3' 'Roof'
Box 'Porch Post L' '197.2,17.6,87.1' '0.2,2.6,0.2' 'DarkWood'
Box 'Porch Post R' '202.8,17.6,87.1' '0.2,2.6,0.2' 'DarkWood'
Box 'Bench' '198.2,16.55,88.6' '1.6,0.1,0.45' 'DarkWood'
Box 'Barrel' '203.9,16.85,89' '0.8,1.1,0.8' 'DarkWood'
# 현관 등불
N create point-light --name 'Porch Light' --position '200,18.6,88.6'
N set 'Porch Light' --component Light --values '{\"color\":[1.0,0.7,0.4,1.0],\"intensity\":2.5,\"range\":9}'

# 걷는 사람 (눈 위 발자국) — C# 스크립트의 값은 CSharpScript 컴포넌트의 fields
N create character --name Walker --position '195,16.3,84'
N add-component Walker Walker
N set Walker --component CSharpScript --values '{\"fields\":{\"center\":[200,0,83],\"radius\":5.5,\"speed\":1.3}}'

# 날씨 · 연출 (Play: 맑음 → 흐림 → 비 → 폭풍 → 눈 → 눈보라 → 맑음, 카메라가 돈다)
N create empty --name Weather
N add-component Weather WeatherController
N create empty --name Director
N add-component Director WeatherDirector
N set Director --component CSharpScript --values '{\"fields\":{\"orbitCenter\":[200,16.3,88],\"orbitRadius\":16,\"orbitHeight\":3.5,\"orbitSpeed\":4}}'

# 숲 (지형에 900 그루 — 오두막 둘레가 트인다)
N terrain-trees Terrain --clear --count 900
N scene save --as Assets/Scenes/WeatherDemo.scene
