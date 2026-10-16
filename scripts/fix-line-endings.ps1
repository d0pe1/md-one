<#
.SYNOPSIS
  Force LF line endings in the reference clones, without touching global git config.

.DESCRIPTION
  Git for Windows installs with core.autocrlf=true at the SYSTEM level. Every
  clone under references/ therefore gets CRLF working-tree files, which:

    * makes `git status` report every file as modified, destroying the clean
      baseline we need for provenance; and
    * leaves CR characters in shell scripts, so WSL refuses to run them:
        ./build.sh: /bin/sh^M: bad interpreter: No such file or directory

  This sets core.autocrlf=false and core.eol=lf on each clone only, then forces
  a re-checkout so the working tree matches upstream byte for byte. The user's
  system and global git configuration are left alone, so their other repos keep
  whatever behaviour they already had.

  Safe to re-run.

  WARNING: this does a hard reset. It is meant for pristine upstream clones that
  we only ever read. Once you have local work in a reference clone, move that
  work out (or into firmware/) before running this. Pass -Force to proceed.

  Note on why there is no "has local changes?" guard: after core.autocrlf is set
  to false, the CRLF working tree genuinely differs from the LF index, so every
  file reports as modified and any such guard would skip every clone. The check
  that matters is the one done after the reset: HEAD must still be the SHA
  recorded in docs/REPOSITORY_MAP.md.
#>
[CmdletBinding()]
param(
    [string] $ReferencesDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'references'),
    [switch] $Force
)

$ErrorActionPreference = 'Stop'

foreach ($repo in Get-ChildItem -Directory $ReferencesDir) {
    if (-not (Test-Path (Join-Path $repo.FullName '.git'))) { continue }
    Push-Location $repo.FullName
    try {
        git config core.autocrlf false
        git config core.eol lf

        $before = (git rev-parse HEAD).Trim()

        # Re-checkout everything so the working tree is rewritten with LF.
        git rm --cached -r -q . | Out-Null
        git reset --hard -q
        git clean -fdq -e build -e target

        $after = (git rev-parse HEAD).Trim()
        if ($after -ne $before) { throw "$($repo.Name): HEAD moved $before -> $after" }

        $dirty = git status --porcelain
        if ($dirty) {
            Write-Warning "$($repo.Name): still dirty after reset:`n$dirty"
        } else {
            Write-Host "clean   $($repo.Name)  ($($after.Substring(0,7)))"
        }
    } finally { Pop-Location }
}
