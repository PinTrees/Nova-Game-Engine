# Package the EXISTING native Hub UI from a separate, verified Release build.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineRoot,
    [string]$Out = '',
    [string]$Version = '0.1.0',
    [string]$CertificateThumbprint = $env:NOVA_SIGNING_CERT_SHA1,
    [ValidateSet('CurrentUser','LocalMachine')][string]$CertificateStore = 'CurrentUser',
    [string]$SigningDlib = $env:NOVA_SIGNING_DLIB,
    [string]$SigningMetadata = $env:NOVA_SIGNING_METADATA,
    [string]$ExpectedPublisher = $env:NOVA_SIGNING_PUBLISHER,
    [string]$TimestampUrl,
    [string]$SignToolPath,
    [switch]$AllowUnsigned
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'HubSigning.ps1')
# Validate the identity before creating output or starting a build.
$signing = New-NovaSigningContext -CertificateThumbprint $CertificateThumbprint -CertificateStore $CertificateStore `
    -SigningDlib $SigningDlib -SigningMetadata $SigningMetadata -ExpectedPublisher $ExpectedPublisher `
    -TimestampUrl $TimestampUrl -SignToolPath $SignToolPath -AllowUnsigned:$AllowUnsigned
if ($AllowUnsigned) { Write-Warning 'DEVELOPMENT ONLY: unsigned Hub output will still trigger Windows reputation warnings.' }
$signatures = [Collections.Generic.List[object]]::new()
$repoRoot = Split-Path -Parent $PSScriptRoot
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
if (-not $Out) { $Out = Join-Path $repoRoot ('dist/Hub-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$Out = [IO.Path]::GetFullPath($Out)
if (Test-Path -LiteralPath $Out) { throw 'Use a new output directory.' }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Use a numeric x.y.z Hub version.' }
foreach ($name in 'HubApp.cpp','HubApp.h','HubProject.cpp','HubProject.h','HubEngineInstaller.cpp','HubEngineInstaller.h','CliInstaller.cpp') {
    $source = Join-Path $repoRoot ('Source/Hub/' + $name)
    $built = Join-Path $EngineRoot ('Source/Hub/' + $name)
    if (!(Test-Path $built) -or (Get-FileHash $source).Hash -ne (Get-FileHash $built).Hash) { throw "Native Hub source mismatch: $name" }
    if ((Get-Item (Join-Path $EngineRoot 'Binaries/NovaCore.dll')).LastWriteTimeUtc -lt (Get-Item $built).LastWriteTimeUtc) { throw 'Build the native Hub changes in Release first.' }
}
New-Item -ItemType Directory -Path $Out | Out-Null
$publish = Join-Path $Out 'service'
& dotnet publish (Join-Path $repoRoot 'Tools/NovaHub/Service/NovaHub.Service.csproj') -c Release -r win-x64 --self-contained true -p:Version=$Version -o $publish --nologo -nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw 'Hub service publish failed.' }
$stage = Join-Path $Out 'NOVA-Hub'
$bin = Join-Path $stage 'Binaries'
New-Item -ItemType Directory -Path $bin | Out-Null
Copy-Item -LiteralPath (Join-Path $EngineRoot 'Binaries/NovaEngine.exe') -Destination (Join-Path $bin 'NovaHub.exe')
Copy-Item -LiteralPath (Join-Path $publish 'NovaHubService.exe') -Destination $bin
foreach ($name in 'NovaCore.dll','assimp-vc143-mt.dll','dxcompiler.dll','dxil.dll','nova.exe') {
    Copy-Item -LiteralPath (Join-Path $EngineRoot ('Binaries/' + $name)) -Destination $bin
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ build tools are required.' }
$devCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
$redist = Get-ChildItem (Join-Path $vsPath 'VC/Redist/MSVC') -Directory | Where-Object Name -Match '^\d' | Sort-Object Name -Descending | Select-Object -First 1
foreach ($pattern in 'Microsoft.VC14*.CRT','Microsoft.VC14*.OpenMP') {
    Get-ChildItem (Join-Path $redist.FullName 'x64') -Directory -Filter $pattern | Select-Object -First 1 | ForEach-Object { Copy-Item (Join-Path $_.FullName '*.dll') $bin }
}
$assets = Join-Path $stage 'ProjectSetting'
New-Item -ItemType Directory -Path $assets | Out-Null
foreach ($folder in 'fonts','logo') { Copy-Item -LiteralPath (Join-Path $EngineRoot ('ProjectSetting/' + $folder)) -Destination $assets -Recurse }
Copy-Item -LiteralPath (Join-Path $EngineRoot 'ProjectSetting/icon.ico') -Destination $assets
# Sign staged NOVA-owned files before embedding. Preserve third-party signatures.
$ownedFiles = @('NovaHub.exe','NovaHubService.exe','NovaCore.dll','nova.exe') | ForEach-Object { Join-Path $bin $_ }
Invoke-NovaCodeSigning -Paths $ownedFiles -Context $signing | ForEach-Object { $signatures.Add($_) }
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)
$rcLines = [Collections.Generic.List[string]]::new()
$header = [Collections.Generic.List[string]]::new()
$header.Add('#pragma once')
$header.Add('struct HubPayload { int id; const wchar_t* path; };')
$header.Add('static constexpr HubPayload hubPayload[] = {')
$id = 101
foreach ($file in $files) {
    $relative = $file.FullName.Substring($stage.Length + 1).Replace('\', '\\')
    $rcLines.Add(('{0} RCDATA "{1}"' -f $id, $file.FullName.Replace('\', '\\')))
    $header.Add(('    {{{0}, L"{1}"}},' -f $id, $relative))
    $id++
}
$header.Add('};')
[IO.File]::WriteAllLines((Join-Path $Out 'HubPayload.h'), $header, [Text.Encoding]::UTF8)
$resource = Join-Path $Out 'HubSetup.rc'
[IO.File]::WriteAllLines($resource, $rcLines, [Text.Encoding]::Unicode)
$cpp = Join-Path $repoRoot 'Tools/NovaHubSetup/main.cpp'
$script = Join-Path $Out 'compile-setup.cmd'
$compile = @"
@echo off
call "$devCmd" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "$Out"
rc /nologo /fo HubSetup.res "$resource"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /utf-8 /I"$Out" "$cpp" HubSetup.res /Fe:NovaHubSetup.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'"
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($script, $compile, [Text.Encoding]::ASCII)
& $env:ComSpec /d /c $script
if ($LASTEXITCODE -ne 0) { throw 'Native Hub installer build failed.' }
Invoke-NovaCodeSigning -Paths (Join-Path $Out 'NovaHubSetup.exe') -Context $signing | ForEach-Object { $signatures.Add($_) }
# Only completed, verified signing produces a release report. Hashes follow signing.
$signingReport = [ordered]@{
    Mode = $signing.Mode
    ReleaseReady = ($signing.Mode -ne 'Unsigned')
    ExpectedPublisher = $signing.ExpectedPublisher
    Files = @($signatures.ToArray())
}
[IO.File]::WriteAllText((Join-Path $Out 'hub-signing.json'), ($signingReport | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
Compress-Archive -LiteralPath $stage -DestinationPath (Join-Path $Out ('NOVA-Hub-' + $Version + '-win64.zip'))
foreach ($file in @('NovaHubSetup.exe', ('NOVA-Hub-' + $Version + '-win64.zip'))) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $Out $file) -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $Out ($file + '.sha256')), ($hash + '  ' + $file + "`n"), [Text.Encoding]::ASCII)
    [pscustomobject]@{ File = (Join-Path $Out $file); MB = [Math]::Round((Get-Item -LiteralPath (Join-Path $Out $file)).Length / 1MB, 1); SHA256 = $hash }
}
