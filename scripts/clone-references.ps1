<#
.SYNOPSIS
  Clone the upstream FM-1 projects into references/ at their recorded SHAs.

.DESCRIPTION
  Reproduces the reference set documented in docs/REPOSITORY_MAP.md.

  By default each repo is checked out at the SHA this workspace was validated
  against, so the tree matches the documentation. Pass -Latest to track the
  default branches instead -- useful, but then the SHAs in REPOSITORY_MAP.md
  are stale and should be refreshed.

  ALWAYS run scripts/fix-line-endings.ps1 -Force afterwards: Git for Windows is
  configured with core.autocrlf=true at the system level here, which puts CRLF
  into shell scripts and breaks every POSIX build under WSL.

.EXAMPLE
  .\scripts\clone-references.ps1
  .\scripts\fix-line-endings.ps1 -Force
#>
[CmdletBinding()]
param([switch] $Latest)

$ErrorActionPreference = 'Stop'
$refs = Join-Path (Split-Path -Parent $PSScriptRoot) 'references'
New-Item -ItemType Directory -Force -Path $refs | Out-Null

# SHAs validated 2026-10-16; see docs/REPOSITORY_MAP.md
$repos = @(
    @{ n = 'fm1-emu';         u = 'https://github.com/joakimeriksson/fm1-emu'; sha = '7196a9f45bb19677b31d258a23a23dd09e0db9de' }
    @{ n = 'dinghy';          u = 'https://github.com/zvenson/dinghy';         sha = 'd44df7db09b98ccad2f1233b8f83f5f09c04829e' }
    @{ n = 'Felucca';         u = 'https://github.com/hugelton/Felucca';       sha = 'a53ecf428c29c780108fd70a31b71a32a6b5c313' }
    @{ n = 'FM-1-RE';         u = 'https://github.com/AL-255/FM-1-RE';         sha = 'ec832f281ca165a7d5e0722985f3acb1465046d5' }
    @{ n = 'lunar-modulator'; u = 'https://github.com/ip2k/lunar-modulator';   sha = 'bfa042446f36c8647efddc6d76b06753d2a5b612' }
    @{ n = 'hortator';        u = 'https://github.com/DeadActive/hortator';    sha = '7e3415971c5800b4a3ad0146a7d95615883d7d96' }
    @{ n = 'awesome-fm-1';    u = 'https://github.com/cicloid/awesome-fm-1';   sha = 'b31003409227464dd781f9a2f03d9d9b62b40d67' }
)

foreach ($r in $repos) {
    $dest = Join-Path $refs $r.n
    if (Test-Path $dest) { Write-Host "exists  $($r.n)"; continue }

    Write-Host "clone   $($r.n)"
    # Shallow enough to reach the pinned SHA without fetching all history.
    git clone --depth 50 $r.u $dest 2>&1 | Out-Null

    if (-not $Latest) {
        Push-Location $dest
        try {
            git checkout -q $r.sha 2>$null
            if ($LASTEXITCODE -ne 0) {
                # the pin is older than our shallow window
                git fetch -q --unshallow 2>&1 | Out-Null
                git checkout -q $r.sha
            }
            Write-Host "        pinned at $($r.sha.Substring(0,7)) (detached)"
        } finally { Pop-Location }
    }
}

Write-Host "`nNow run:  .\scripts\fix-line-endings.ps1 -Force"
