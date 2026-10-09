# Prototype 메시 (GLB) → 프리팹 (재질 · 그림자). make_prototype.py 다음에 돌린다:
#   python Tools/prototype/make_prototype.py ; powershell -File Tools/prototype/bake_prefabs.ps1
#  창 없는 검사 에디터 (E:\NovaTest\ScriptTest) 를 띄워 CLI 로: 놓기 → 재질 · 그림자 → 프리팹 저장 → 지우기
param([string]$Project = 'E:\NovaTest\ScriptTest')
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot '..\tests\common.ps1')
$script:Project = $Project
$root = Join-Path $PSScriptRoot '..\..\Resources\Packages\Prototype'
# 갈래마다 색 (흰색 ~ 어두운 회색): 벽은 밝게, 지붕 · 소품은 어둡게 — 블록아웃에서 덩어리가 구분되게
$tone = @{ Shapes = 'Light'; Floor = 'Gray'; Walls = 'Light'; Structure = 'DarkGray'; Stairs = 'Gray'; Roof = 'Charcoal'; Castle = 'LightGray'; Props = 'DarkGray'; Nature = 'Gray' }
Backup-Layout
$ed = Start-TestEditor
$made = 0; $failed = @()
try
{
    Invoke-Nova 'autosave discard' | Out-Null
    Invoke-Nova 'scene new --force' | Out-Null
    foreach ($glb in Get-ChildItem (Join-Path $root 'Meshes') -Recurse -Filter *.glb)
    {
        $cat = $glb.Directory.Name
        $name = $glb.BaseName
        $mesh = "Resources/Packages/Prototype/Meshes/$cat/$($glb.Name)"
        $p = Invoke-NovaJson "modelfile place $mesh"
        if (-not $p) { $failed += $name; continue }
        $mat = "Resources\\Packages\\Prototype\\Materials\\Prototype_$($tone[$cat]).mat"
        Invoke-Nova ('set "' + $p.name + '" --component MeshRenderer --values "{\"m_MaterialPaths\":[\"' + $mat + '\"],\"castShadows\":1}"') | Out-Null
        $s = Invoke-NovaJson "prefab save --target `"$($p.name)`" --path Resources/Packages/Prototype/Prefabs/$cat/$name.prefab"
        if ($s) { $made++ } else { $failed += $name }
        Invoke-Nova "delete `"$($p.name)`"" | Out-Null
    }
    Invoke-Nova 'scene new --force' | Out-Null
}
finally { "stop: $(Stop-TestEditor $ed)"; Restore-Layout }
"prefabs $made, failed $($failed.Count) $($failed -join ', ')"
