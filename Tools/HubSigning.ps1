# Signing helpers shared by Hub packaging and its independent verification.
function Resolve-NovaSignTool {
    param([string]$Path)
    if ($Path) {
        $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
        if (!(Test-Path -LiteralPath $resolved -PathType Leaf)) { throw 'SignTool must be a file.' }
        return $resolved
    }
    $sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    $candidate = Get-ChildItem -LiteralPath $sdk -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64/signtool.exe' } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (!$candidate) { throw 'Install the Windows SDK x64 SignTool, or supply -SignToolPath.' }
    return $candidate
}

function Get-NovaSigningCertificate {
    param([string]$Store, [string]$Thumbprint)
    return Get-Item -LiteralPath ("Cert:\$Store\My\$Thumbprint") -ErrorAction Stop
}

function New-NovaSigningContext {
    param(
        [string]$CertificateThumbprint,
        [ValidateSet('CurrentUser','LocalMachine')][string]$CertificateStore = 'CurrentUser',
        [string]$SigningDlib,
        [string]$SigningMetadata,
        [string]$ExpectedPublisher,
        [string]$TimestampUrl,
        [string]$SignToolPath,
        [switch]$AllowUnsigned
    )
    $cloud = [bool]($SigningDlib -or $SigningMetadata)
    if ($AllowUnsigned) {
        if ($CertificateThumbprint -or $cloud -or $ExpectedPublisher -or $TimestampUrl -or $SignToolPath) {
            throw 'Do not combine -AllowUnsigned with signing settings.'
        }
        return [pscustomobject]@{ Mode = 'Unsigned'; ExpectedPublisher = ''; SignTool = '' }
    }
    if (!$CertificateThumbprint -and !$cloud) {
        throw 'A trusted signing identity is required. Supply -CertificateThumbprint, or -SigningDlib/-SigningMetadata/-ExpectedPublisher. Development only: -AllowUnsigned.'
    }
    if ($CertificateThumbprint -and $cloud) { throw 'Choose certificate-store signing OR Artifact Signing.' }
    $identityArgs = @()
    if ($cloud) {
        if (!$SigningDlib -or !$SigningMetadata -or !$ExpectedPublisher) {
            throw 'Artifact Signing requires -SigningDlib, -SigningMetadata and the verified -ExpectedPublisher.'
        }
        $dlib = (Resolve-Path -LiteralPath $SigningDlib -ErrorAction Stop).Path
        $metadataPath = (Resolve-Path -LiteralPath $SigningMetadata -ErrorAction Stop).Path
        if (!(Test-Path -LiteralPath $dlib -PathType Leaf) -or !(Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
            throw 'Artifact Signing Dlib and metadata must be files.'
        }
        $metadata = [IO.File]::ReadAllText($metadataPath) | ConvertFrom-Json
        $endpoint = $null
        if (![Uri]::TryCreate([string]$metadata.Endpoint, [UriKind]::Absolute, [ref]$endpoint) -or
            $endpoint.Scheme -ne 'https' -or $endpoint.Host -notmatch '^[a-z]+\.codesigning\.azure\.net$' -or
            $endpoint.UserInfo -or !$endpoint.IsDefaultPort -or $endpoint.Query -or $endpoint.Fragment -or $endpoint.AbsolutePath -ne '/') {
            throw 'Metadata Endpoint must be an official regional HTTPS Artifact Signing endpoint.'
        }
        if ([string]::IsNullOrWhiteSpace([string]$metadata.CodeSigningAccountName) -or
            [string]::IsNullOrWhiteSpace([string]$metadata.CertificateProfileName)) {
            throw 'Metadata requires CodeSigningAccountName and CertificateProfileName.'
        }
        $identityArgs = @('/dlib', $dlib, '/dmdf', $metadataPath)
        if (!$TimestampUrl) { $TimestampUrl = 'http://timestamp.acs.microsoft.com' }
        $mode = 'ArtifactSigning'
    } else {
        $thumbprint = $CertificateThumbprint.Replace(' ', '')
        if ($thumbprint -notmatch '^[0-9a-fA-F]{40}$') { throw 'Certificate thumbprint must be 40 hexadecimal characters.' }
        $certificate = Get-NovaSigningCertificate -Store $CertificateStore -Thumbprint $thumbprint
        if (!$certificate.HasPrivateKey) { throw 'The signing certificate has no accessible private key.' }
        $now = Get-Date
        if ($certificate.NotBefore -gt $now -or $certificate.NotAfter -le $now) { throw 'The signing certificate is outside its validity period.' }
        if ($certificate.EnhancedKeyUsageList.ObjectId -notcontains '1.3.6.1.5.5.7.3.3') { throw 'The certificate is not a code signing certificate.' }
        if ($certificate.Subject -eq $certificate.Issuer) { throw 'A self-signed certificate cannot be used for public Hub releases.' }
        $publisher = $certificate.GetNameInfo([Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false)
        if ($ExpectedPublisher -and $ExpectedPublisher -cne $publisher) { throw 'Certificate publisher does not match -ExpectedPublisher.' }
        $ExpectedPublisher = $publisher
        $identityArgs = @('/sha1', $thumbprint, '/s', 'My')
        if ($CertificateStore -eq 'LocalMachine') { $identityArgs += '/sm' }
        if (!$TimestampUrl) { $TimestampUrl = 'http://timestamp.digicert.com' }
        $mode = 'CertificateStore'
    }
    $timestampUri = $null
    if (![Uri]::TryCreate($TimestampUrl, [UriKind]::Absolute, [ref]$timestampUri) -or
        $timestampUri.Scheme -notin @('http','https') -or $timestampUri.UserInfo) { throw 'Use an HTTP(S) RFC3161 timestamp server without credentials in its URL.' }
    return [pscustomobject]@{
        Mode = $mode
        ExpectedPublisher = $ExpectedPublisher
        SignTool = (Resolve-NovaSignTool -Path $SignToolPath)
        TimestampUrl = $TimestampUrl
        IdentityArguments = $identityArgs
        ExpectedThumbprint = $(if ($cloud) { '' } else { $thumbprint })
    }
}

function Invoke-NovaSignTool {
    param([string]$Tool, [string[]]$ToolArguments)
    & $Tool @ToolArguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed or warned (exit $LASTEXITCODE). Release packaging stopped." }
}

function Assert-NovaCodeSignature {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$Context)
    $signature = Get-AuthenticodeSignature -LiteralPath $Path -ErrorAction Stop
    if ($signature.Status -ne 'Valid' -or !$signature.SignerCertificate) { throw "Invalid Authenticode signature: $Path ($($signature.Status))." }
    if (!$signature.TimeStamperCertificate) { throw "Missing trusted timestamp: $Path" }
    $publisher = $signature.SignerCertificate.GetNameInfo([Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false)
    if ($publisher -cne $Context.ExpectedPublisher) { throw "Unexpected publisher: $Path" }
    if ($Context.ExpectedThumbprint -and $signature.SignerCertificate.Thumbprint -ne $Context.ExpectedThumbprint) {
        throw "Unexpected signing certificate: $Path"
    }
    Invoke-NovaSignTool -Tool $Context.SignTool -ToolArguments @('verify', '/pa', '/all', '/tw', $Path)
    return [pscustomobject]@{
        File = [IO.Path]::GetFileName($Path)
        Publisher = $publisher
        CertificateThumbprint = $signature.SignerCertificate.Thumbprint
        TimestampAuthority = $signature.TimeStamperCertificate.Subject
        SHA256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        Status = 'Valid'
    }
}

function Invoke-NovaCodeSigning {
    param([Parameter(Mandatory)][string[]]$Paths, [Parameter(Mandatory)]$Context)
    if ($Context.Mode -eq 'Unsigned') { return }
    foreach ($path in $Paths) {
        $resolved = (Resolve-Path -LiteralPath $path -ErrorAction Stop).Path
        $arguments = @('sign', '/fd', 'SHA256', '/tr', $Context.TimestampUrl, '/td', 'SHA256', '/d', 'NOVA Hub', '/du', 'https://nova-game-engine.web.app')
        $arguments += $Context.IdentityArguments
        $arguments += $resolved
        Invoke-NovaSignTool -Tool $Context.SignTool -ToolArguments $arguments
        Assert-NovaCodeSignature -Path $resolved -Context $Context
    }
}
