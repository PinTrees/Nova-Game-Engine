# 안드로이드 C# 런타임 (Microsoft 의 Mono — .NET 8 의 모바일 런타임, MIT) 을 nuget.org 에서 받아 ThirdParty/MonoAndroid 에 푼다.
#   powershell -File Tools/fetch_android_mono.ps1 [-Version 8.0.31] [-Abi x86_64]
#  - 패키지: Microsoft.NETCore.App.Runtime.Mono.android-<x64|arm64> (libmonosgen-2.0.so · System.Private.CoreLib.dll · BCL · 헤더)
#  - 버전은 PC 의 .NET 런타임 (ScriptCore 가 net8.0) 과 같은 8.0.x
#  - git 에는 넣지 않는다 (.gitignore) — 엔진 배포판은 Tools/package_release.ps1 가 Android/Player/<ABI>/mono 로 넣는다
param([string]$Version = '8.0.31', [string]$Abi = 'x86_64')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$rid = if ($Abi -eq 'arm64-v8a') { 'android-arm64' } else { 'android-x64' }
$id = "microsoft.netcore.app.runtime.mono.$rid"
$dest = Join-Path $root "ThirdParty\MonoAndroid\$Abi"
if (Test-Path (Join-Path $dest "runtimes\$rid\native\libmonosgen-2.0.so"))
{
    $have = (Get-Content (Join-Path $dest 'version.txt') -ErrorAction SilentlyContinue)
    if ($have -eq $Version) { Write-Host "already there: $dest ($Version)"; exit 0 }
}
$url = "https://api.nuget.org/v3-flatcontainer/$id/$Version/$id.$Version.nupkg"
$tmp = Join-Path $env:TEMP "$id.$Version.nupkg"
Write-Host "download $url"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
# 무결성: nuget.org 가 알려 주는 SHA512 (카탈로그) 와 비교
$reg = Invoke-RestMethod -Uri "https://api.nuget.org/v3/registration5-semver1/$id/$Version.json" -UseBasicParsing
$catalog = Invoke-RestMethod -Uri $reg.catalogEntry -UseBasicParsing
$sha = [Convert]::ToBase64String([Security.Cryptography.SHA512]::Create().ComputeHash([IO.File]::ReadAllBytes($tmp)))
if ($catalog.packageHash -and $catalog.packageHash -ne $sha) { Remove-Item $tmp; throw "SHA512 mismatch ($sha vs $($catalog.packageHash))" }
Write-Host "SHA512 ok ($([math]::Round((Get-Item $tmp).Length / 1MB, 1)) MB)"
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force $dest | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($tmp, $dest)
Set-Content -Path (Join-Path $dest 'version.txt') -Value $Version
Remove-Item $tmp
Write-Host "extracted to $dest"
