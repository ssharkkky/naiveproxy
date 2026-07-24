param(
  [Parameter(Mandatory = $true)][ValidateSet("Install", "Check", "Remove")]
  [string]$Operation,
  [Parameter(Mandatory = $true)][string]$CertificatePath,
  [Parameter(Mandatory = $true)][string]$ExpectedThumbprint
)

$ErrorActionPreference = "Stop"
$expected = ($ExpectedThumbprint -replace '[^0-9A-Fa-f]', '').ToUpperInvariant()
$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
  $CertificatePath
)
if ($certificate.Thumbprint.ToUpperInvariant() -ne $expected) {
  throw "temporary certificate thumbprint does not match"
}
if ($certificate.Subject -ne $certificate.Issuer) {
  throw "TrustedPeople qualification certificate must be self-signed"
}

$store = [System.Security.Cryptography.X509Certificates.X509Store]::new(
  "TrustedPeople",
  [System.Security.Cryptography.X509Certificates.StoreLocation]::LocalMachine
)
try {
  $openFlags = if ($Operation -eq "Check") {
    [System.Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly
  } else {
    [System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite
  }
  $store.Open($openFlags)

  if ($Operation -eq "Install") {
    $store.Add($certificate)
  } elseif ($Operation -eq "Remove") {
    @($store.Certificates) |
      Where-Object { $_.Thumbprint.ToUpperInvariant() -eq $expected } |
      ForEach-Object { $store.Remove($_) }
  }

  $matches = @(
    $store.Certificates |
      Where-Object { $_.Thumbprint.ToUpperInvariant() -eq $expected }
  ).Count
  if ($Operation -eq "Remove") {
    if ($matches -ne 0) {
      throw "temporary certificate remained in LocalMachine TrustedPeople"
    }
  } elseif ($matches -ne 1) {
    throw "temporary certificate is not uniquely present in LocalMachine TrustedPeople"
  }
} finally {
  $store.Close()
  $store.Dispose()
  $certificate.Dispose()
}

Write-Output "M5_WINDOWS_TRUSTED_LEAF_$($Operation.ToUpperInvariant())_OK"
