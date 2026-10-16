<#
.SYNOPSIS
  Download community FM-1 firmware packages (.fwsc) for use with the emulator.

.DESCRIPTION
  Reads the fm1-emu catalog (references/fm1-emu/web/firmwares.json) and downloads the
  named firmware packages from their authors' own sites into artifacts/firmwares/.

  This exists instead of the upstream `node tools/fetch_firmwares.mjs` because that
  script derives its root via `new URL("..", import.meta.url).pathname`, which yields
  "/C:/..." on Windows and resolves to the invalid path "C:\C:\...". We do not modify
  the upstream repository, so the workspace carries its own fetcher.

  The emulator loads a .fwsc package directly (--fwsc), so unpacking is not required.

.EXAMPLE
  .\scripts\fetch-firmware.ps1 dinghy felucca
  .\scripts\fetch-firmware.ps1 -List
#>
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Id,
    [switch] $List
)

$ErrorActionPreference = 'Stop'
$root    = Split-Path -Parent $PSScriptRoot
$catalog = Join-Path $root 'references\fm1-emu\web\firmwares.json'
$outDir  = Join-Path $root 'artifacts\firmwares'

if (-not (Test-Path $catalog)) { throw "Catalog not found: $catalog (clone references/fm1-emu first)" }
$fw = (Get-Content $catalog -Raw | ConvertFrom-Json).firmwares

if ($List -or -not $Id) {
    $fw | Select-Object id, version, author, license, @{n = 'byo'; e = { [bool]$_.byo } } |
        Sort-Object id | Format-Table -AutoSize
    return
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

foreach ($want in $Id) {
    $f = $fw | Where-Object id -eq $want | Select-Object -First 1
    if (-not $f)   { Write-Warning "unknown id '$want' (use -List)"; continue }
    if ($f.byo)    { Write-Warning "'$want' ships no downloadable package (bring your own .fwsc)"; continue }

    $dest = Join-Path $outDir "$($f.id)-$($f.version).fwsc"
    if (Test-Path $dest) { Write-Host "exists  $($f.id) -> $dest"; continue }

    Write-Host "fetch   $($f.id) <- $($f.package)"
    Invoke-WebRequest -Uri $f.package -OutFile $dest -UseBasicParsing

    $sha = (Get-FileHash $dest -Algorithm SHA256).Hash.ToLower()
    $len = (Get-Item $dest).Length
    Write-Host ("        {0} bytes  sha256 {1}" -f $len, $sha)
    # Provenance alongside the binary: these are other people's released artifacts.
    [pscustomobject]@{
        id = $f.id; version = $f.version; author = $f.author; license = $f.license
        repo = $f.repo; package = $f.package; sha256 = $sha; bytes = $len
        fetched = (Get-Date).ToString('o')
    } | ConvertTo-Json | Set-Content "$dest.json"
}
