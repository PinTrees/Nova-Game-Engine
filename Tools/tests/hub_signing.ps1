[CmdletBinding()]
param([string]$Out = '', [string]$UnsignedFile = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot 'Tools/HubSigning.ps1')
if (!$Out) { $Out = Join-Path $env:TEMP ('NovaHubSigning-' + [Guid]::NewGuid().ToString('N')) }
if (Test-Path -LiteralPath $Out) { throw 'Use a new test output folder.' }
New-Item -ItemType Directory -Path $Out | Out-Null
$results = [Collections.Generic.List[object]]::new()
function Check {
    param([string]$Name, [scriptblock]$Action)
    try { & $Action; $results.Add([pscustomobject]@{ Name = $Name; Passed = $true }) }
    catch { $results.Add([pscustomobject]@{ Name = $Name; Passed = $false; Error = $_.Exception.Message }) }
}
function Require { param([bool]$Value, [string]$Message) if (!$Value) { throw $Message } }
function Reject {
    param([scriptblock]$Action, [string]$MessagePattern)
    $caught = $false
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notmatch $MessagePattern) { throw }
        $caught = $true
    }
    Require $caught 'Expected rejection did not happen.'
}
Check 'Missing identity stops release signing' { Reject { New-NovaSigningContext } 'trusted signing identity' }
Check 'Development output requires explicit unsigned mode' {
    Require ((New-NovaSigningContext -AllowUnsigned).Mode -eq 'Unsigned') 'Wrong mode.'
}
Check 'Unsigned mode cannot silently ignore a signing configuration' {
    Reject { New-NovaSigningContext -AllowUnsigned -CertificateThumbprint ('a' * 40) } 'combine'
}
Check 'Certificate and cloud cannot be mixed' {
    Reject { New-NovaSigningContext -CertificateThumbprint ('a' * 40) -SigningDlib 'missing' } 'Choose'
}
Check 'Cloud signing requires a verified publisher' {
    Reject { New-NovaSigningContext -SigningDlib 'missing' -SigningMetadata 'missing' } 'requires'
}
Check 'Certificate thumbprint is validated before provider lookup' {
    Reject { New-NovaSigningContext -CertificateThumbprint '../bad' } '40 hexadecimal'
}
$dlib = Join-Path $Out 'test-dlib.dll'
$metadataPath = Join-Path $Out 'metadata.json'
[IO.File]::WriteAllText($dlib, 'Test placeholder; never loaded.')
function Write-Metadata {
    param([string]$Endpoint, [string]$Account = 'test-account', [string]$Profile = 'test-profile')
    @{ Endpoint = $Endpoint; CodeSigningAccountName = $Account; CertificateProfileName = $Profile } |
        ConvertTo-Json | Set-Content -LiteralPath $metadataPath -Encoding UTF8
}
Check 'Cloud endpoint cannot send signing credentials to another host' {
    Write-Metadata 'https://evil.example/'
    Reject { New-NovaSigningContext -SigningDlib $dlib -SigningMetadata $metadataPath -ExpectedPublisher 'Test Publisher' } 'official regional'
}
Check 'Cloud endpoint rejects lookalike domain suffix' {
    Write-Metadata 'https://krc.codesigning.azure.net.evil.example/'
    Reject { New-NovaSigningContext -SigningDlib $dlib -SigningMetadata $metadataPath -ExpectedPublisher 'Test Publisher' } 'official regional'
}
Check 'Cloud endpoint requires HTTPS' {
    Write-Metadata 'http://krc.codesigning.azure.net/'
    Reject { New-NovaSigningContext -SigningDlib $dlib -SigningMetadata $metadataPath -ExpectedPublisher 'Test Publisher' } 'official regional'
}
Check 'Cloud profile cannot be empty' {
    Write-Metadata 'https://krc.codesigning.azure.net/' -Profile ''
    Reject { New-NovaSigningContext -SigningDlib $dlib -SigningMetadata $metadataPath -ExpectedPublisher 'Test Publisher' } 'CertificateProfileName'
}
Write-Metadata 'https://krc.codesigning.azure.net/'
Check 'Cloud metadata selects the provided identity and Microsoft timestamp' {
    $context = New-NovaSigningContext -SigningDlib $dlib -SigningMetadata $metadataPath -ExpectedPublisher 'Test Publisher'
    Require ($context.Mode -eq 'ArtifactSigning' -and $context.IdentityArguments -contains '/dmdf' -and
        $context.TimestampUrl -eq 'http://timestamp.acs.microsoft.com') 'Cloud configuration mismatch.'
}
Check 'Timestamp URL cannot carry a password' {
    Reject { New-NovaSigningContext -SigningDlib $dlib -SigningMetadata $metadataPath -ExpectedPublisher 'Test Publisher' -TimestampUrl 'https://user:password@example.com' } 'without credentials'
}
Check 'Unsigned installer is rejected without modifying it' {
    $installer = $(if ($UnsignedFile) { $UnsignedFile } else { $dlib })
    $hash = (Get-FileHash -LiteralPath $installer).Hash
    Reject { Assert-NovaCodeSignature -Path $installer -Context ([pscustomobject]@{ ExpectedPublisher = 'Test Publisher' }) } 'Invalid Authenticode'
    Require ((Get-FileHash -LiteralPath $installer).Hash -eq $hash) 'Signature verification modified the installer.'
}
Check 'Missing identity stops packaging before output/build' {
    $packageOut = Join-Path $Out 'blocked-package'
    Reject { & (Join-Path $repoRoot 'Tools/package_hub.ps1') -EngineRoot 'missing-engine' -Out $packageOut -CertificateThumbprint '' -SigningDlib '' -SigningMetadata '' -ExpectedPublisher '' } 'trusted signing identity'
    Require (!(Test-Path -LiteralPath $packageOut)) 'A failed preflight created output.'
}

# Boundary simulations exercise post-sign verification without creating a certificate,
# invoking cloud signing, or changing any machine trust settings.
function Get-NovaSigningCertificate { param($Store, $Thumbprint) return $script:testCertificate }
function New-TestCertificate {
    $value = [pscustomobject]@{
        HasPrivateKey = $true; NotBefore = (Get-Date).AddDays(-1); NotAfter = (Get-Date).AddDays(30)
        EnhancedKeyUsageList = @([pscustomobject]@{ ObjectId = '1.3.6.1.5.5.7.3.3' })
        Subject = 'CN=Test Publisher'; Issuer = 'CN=Test Authority'; Thumbprint = ('a' * 40)
    }
    $value | Add-Member ScriptMethod GetNameInfo { param($Type, $ForIssuer) return 'Test Publisher' }
    return $value
}
$script:testCertificate = New-TestCertificate
Check 'Certificate-store identity uses exact thumbprint and publisher' {
    $context = New-NovaSigningContext -CertificateThumbprint ('a' * 40) -CertificateStore LocalMachine
    Require ($context.IdentityArguments -contains '/sm' -and $context.IdentityArguments -contains ('a' * 40) -and $context.ExpectedPublisher -eq 'Test Publisher') 'Certificate selection mismatch.'
}
Check 'A certificate without a private key is rejected' {
    $script:testCertificate.HasPrivateKey = $false
    Reject { New-NovaSigningContext -CertificateThumbprint ('a' * 40) } 'private key'
    $script:testCertificate = New-TestCertificate
}
Check 'An expired certificate is rejected' {
    $script:testCertificate.NotAfter = (Get-Date).AddDays(-1)
    Reject { New-NovaSigningContext -CertificateThumbprint ('a' * 40) } 'validity period'
    $script:testCertificate = New-TestCertificate
}
Check 'A self-signed certificate is rejected' {
    $script:testCertificate.Issuer = $script:testCertificate.Subject
    Reject { New-NovaSigningContext -CertificateThumbprint ('a' * 40) } 'self-signed'
    $script:testCertificate = New-TestCertificate
}
Check 'Non-code-signing certificates are rejected' {
    $script:testCertificate.EnhancedKeyUsageList = @()
    Reject { New-NovaSigningContext -CertificateThumbprint ('a' * 40) } 'not a code signing'
    $script:testCertificate = New-TestCertificate
}
Check 'Changing the publisher requires updating the explicit expectation' {
    Reject { New-NovaSigningContext -CertificateThumbprint ('a' * 40) -ExpectedPublisher 'Another Company' } 'does not match'
}
$script:toolCalls = [Collections.Generic.List[object]]::new()
function Invoke-NovaSignTool { param($Tool, $ToolArguments) $script:toolCalls.Add($ToolArguments) }
function Get-AuthenticodeSignature { param($LiteralPath, $ErrorAction) return $script:testSignature }
$script:testSignature = [pscustomobject]@{ Status = 'Valid'; SignerCertificate = (New-TestCertificate); TimeStamperCertificate = [pscustomobject]@{ Subject = 'CN=Test TSA' } }
$context = New-NovaSigningContext -CertificateThumbprint ('a' * 40)
Check 'A signed file without a timestamp blocks release' {
    $script:testSignature.TimeStamperCertificate = $null
    Reject { Assert-NovaCodeSignature -Path $dlib -Context $context } 'Missing trusted timestamp'
    $script:testSignature.TimeStamperCertificate = [pscustomobject]@{ Subject = 'CN=Test TSA' }
}
Check 'An unexpected signing identity blocks release' {
    Reject { Assert-NovaCodeSignature -Path $dlib -Context ([pscustomobject]@{ ExpectedPublisher = 'Another Company' }) } 'Unexpected publisher'
}
Check 'Hash mismatch signature status blocks release' {
    $script:testSignature.Status = 'HashMismatch'
    Reject { Assert-NovaCodeSignature -Path $dlib -Context $context } 'Invalid Authenticode'
    $script:testSignature.Status = 'Valid'
}
Check 'Another certificate with the same publisher cannot replace the selected certificate' {
    $script:testSignature.SignerCertificate.Thumbprint = ('b' * 40)
    Reject { Assert-NovaCodeSignature -Path $dlib -Context $context } 'Unexpected signing certificate'
    $script:testSignature.SignerCertificate.Thumbprint = ('a' * 40)
}
Check 'Signing uses SHA256 and RFC3161 then checks Authenticode policy' {
    $record = Invoke-NovaCodeSigning -Paths $dlib -Context $context
    $sign = $script:toolCalls[$script:toolCalls.Count - 2]
    $verify = $script:toolCalls[$script:toolCalls.Count - 1]
    Require ($sign[0] -eq 'sign' -and $sign -contains '/fd' -and $sign -contains '/tr' -and $sign -contains '/td') 'Signing omitted digest/timestamp options.'
    Require ($verify[0] -eq 'verify' -and $verify -contains '/pa' -and $verify -contains '/tw') 'Verification policy mismatch.'
    Require ($record.SHA256 -eq (Get-FileHash $dlib).Hash.ToLowerInvariant()) 'Report hash does not describe the final signed file.'
}
Check 'A signing tool failure stops before recording a successful signature' {
    function Invoke-NovaSignTool { throw 'Simulated signing provider failure' }
    Reject { Invoke-NovaCodeSigning -Paths $dlib -Context $context } 'provider failure'
}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Out 'results.json') -Encoding UTF8
$failed = @($results | Where-Object { !$_.Passed })
$results | Format-Table Name, Passed -AutoSize
if ($failed.Count) { $failed | Format-List; throw "$($failed.Count) signing checks failed." }
Write-Output "PASS $($results.Count)/$($results.Count). Evidence: $Out"
