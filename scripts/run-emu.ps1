<#
.SYNOPSIS
  Run an FM-1 firmware package headlessly and collect audio + screen snapshots.

.DESCRIPTION
  Wraps references/fm1-emu's batch runner (fm1-emu.exe) into a repeatable loop.

  Two upstream behaviours this works around, neither of which requires editing
  the emulator:
    * --snap takes snapshot TIMES IN MILLISECONDS, not a filename.
    * snapshots are written to the hardcoded relative path "work/lcd_<ms>.bmp",
      and the write error is discarded, so a missing "work" directory silently
      produces no screens. We create it and then collect the files.

  There is NO -Quiet switch on purpose. The emulator's --quiet skips the whole
  end-of-run report block, and the WAV is written from INSIDE that block
  (crates/fm1-emu/src/main.rs: the `if let Some(w) = &args.wav` is nested in the
  same `if !args.quiet`). So --quiet silently produces no audio and still exits
  0. Since capturing audio is the point of this wrapper, the switch is omitted
  rather than offered as a trap.

  Stale output is deleted before each run and the WAV is checked to be newly
  written, so a failed run cannot be mistaken for a successful one by leaving
  the previous run's file in place.

.PARAMETER Firmware
  Path to a .fwsc package, or an id present in artifacts/firmwares.

.PARAMETER Keys
  Panel script passed through to --keys, e.g. "800-2200:n7,1200-2400:n11".
  Forms: "<ms>:PLAY", "<from>-<to>:n<note>", "<ms>:enc0:+3".

.EXAMPLE
  .\scripts\run-emu.ps1 -Firmware dinghy -Ms 3000 -Keys "800-2200:n7" -Snap 300,1500
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Firmware,
    [int]      $Ms   = 3000,
    [string]   $Keys = '',
    [int[]]    $Snap = @(),
    [string]   $Name = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$emu  = Join-Path $root 'references\fm1-emu\target\release\fm1-emu.exe'
if (-not (Test-Path $emu)) {
    throw "emulator not built: $emu`n  cd references/fm1-emu; cargo build --release -p fm1-emu"
}

# accept either a path or a catalog id
$pkg = $Firmware
if (-not (Test-Path $pkg)) {
    $pkg = Get-ChildItem (Join-Path $root 'artifacts\firmwares') -Filter "$Firmware-*.fwsc" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $pkg) { throw "no package for '$Firmware' (scripts/fetch-firmware.ps1 $Firmware)" }
}
if (-not $Name) { $Name = [IO.Path]::GetFileNameWithoutExtension($pkg) }
# the emulator is invoked from a scratch CWD (see below), so every path it
# receives must be absolute
$pkg = (Resolve-Path $pkg).Path

$outDir = Join-Path $root "artifacts\emu\$Name"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# the emulator resolves work/ relative to the CWD, so run from a scratch dir we own
$runDir = Join-Path $outDir '_run'
New-Item -ItemType Directory -Force -Path (Join-Path $runDir 'work') | Out-Null

$wav = Join-Path $outDir "$Name.wav"

# Clear stale output: otherwise a run that writes nothing leaves the previous
# run's WAV and screens in place and looks like it succeeded.
Remove-Item $wav -ErrorAction SilentlyContinue
Remove-Item (Join-Path $outDir 'screen_*.bmp') -ErrorAction SilentlyContinue
Remove-Item (Join-Path $runDir 'work\lcd_*.bmp') -ErrorAction SilentlyContinue

$emuArgs = @('--fwsc', $pkg, '--ms', $Ms, '--wav', $wav)
if ($Keys)       { $emuArgs += @('--keys', $Keys) }
if ($Snap.Count) { $emuArgs += @('--snap', ($Snap -join ',')) }

Push-Location $runDir
try {
    Write-Host "run  $(Split-Path -Leaf $pkg)  ${Ms}ms  keys='$Keys'"
    & $emu @emuArgs 2>&1 | Select-Object -Last 6
    $code = $LASTEXITCODE
} finally { Pop-Location }

if ($code -ne 0) { throw "fm1-emu exited $code" }

# collect the screens out of the scratch work/ dir
Get-ChildItem (Join-Path $runDir 'work') -Filter 'lcd_*.bmp' -ErrorAction SilentlyContinue | ForEach-Object {
    $dst = Join-Path $outDir "screen_$($_.BaseName -replace '^lcd_','').bmp"
    Move-Item $_.FullName $dst -Force
    Write-Host "screen -> $dst"
}
if (-not (Test-Path $wav)) {
    throw "fm1-emu exited 0 but wrote no WAV to $wav.`n" +
          "  The emulator only writes audio from inside its end-of-run report, " +
          "so anything that skips that block (a halt, a --quiet flag) yields silence."
}
Write-Host "audio  -> $wav"
Write-Host "`n-- audio summary --"
python (Join-Path $root 'scripts\wavstat.py') $wav
