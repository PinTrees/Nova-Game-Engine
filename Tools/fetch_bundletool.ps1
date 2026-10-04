# Google bundletool (Apache-2.0) 을 받아 ThirdParty/bundletool 에 둔다 — 엔진이 만든 .aab 를 검사할 때 (build-apks · install-apks) 만 쓴다.
#   powershell -File Tools/fetch_bundletool.ps1 [-Version 1.18.3]
#  - GitHub 의 릴리스 자산 (bundletool-all-<버전>.jar), GitHub API 가 알려 주는 SHA-256 과 비교
#  - git 에는 넣지 않는다 (.gitignore)
param([string]$Version = '1.18.3')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$dest = Join-Path $root 'ThirdParty\bundletool'
$jar = Join-Path $dest "bundletool-all-$Version.jar"
if (Test-Path $jar) { Write-Host "already there: $jar"; exit 0 }
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/google/bundletool/releases/tags/$Version" -UseBasicParsing -Headers @{ 'User-Agent' = 'NOVA' }
$asset = $release.assets | Where-Object { $_.name -eq "bundletool-all-$Version.jar" } | Select-Object -First 1
if (-not $asset) { throw "bundletool-all-$Version.jar not in the release" }
$url = [Uri]$asset.browser_download_url
if ($url.Host -ne 'github.com' -or -not $url.AbsolutePath.StartsWith('/google/bundletool/releases/download/')) { throw "unexpected download url $url" }
New-Item -ItemType Directory -Force $dest | Out-Null
$tmp = "$jar.download"
Write-Host "download $url"
Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
$sha = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLowerInvariant()
if ($asset.digest)
{
    if ($asset.digest -ne "sha256:$sha") { Remove-Item $tmp; throw "SHA-256 mismatch ($sha vs $($asset.digest))" }
    Write-Host "SHA-256 ok"
}
elseif ((Get-Item $tmp).Length -ne $asset.size) { Remove-Item $tmp; throw 'size mismatch' }
Move-Item $tmp $jar
Write-Host "saved $jar ($([math]::Round((Get-Item $jar).Length / 1MB, 1)) MB)"
