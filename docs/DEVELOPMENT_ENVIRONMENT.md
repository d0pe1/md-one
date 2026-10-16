# Development environment

State as bootstrapped on **2026-10-16**. Every version here was read off the
machine, not assumed.

## The host

| | |
| --- | --- |
| Machine | TheBeast (personal workstation) |
| OS | Windows 11 Pro 10.0.26200, AMD64 |
| Shell | PowerShell 7.6.6 |
| Linux | WSL2 **Arch Linux**, kernel 6.6.87.2-microsoft-standard-WSL2, runs as `root` |
| Disk | C: 2485 GB free, WSL root fs 952 GB free |
| Physical FM-1 | **none connected** — two units ordered, not yet arrived |

## Tools present before this work (unchanged)

Windows host:

| Tool | Version | Path |
| --- | --- | --- |
| git | 2.51.0.windows.2 | `C:\Program Files\Git\cmd\git.exe` |
| python | 3.14.7 | `C:\Python314\python.exe` |
| rustc / cargo | 1.93.1 | `C:\Users\Nico\.cargo\bin\` |
| node | 24.20.0 | `C:\Program Files\nodejs\node.exe` |

WSL Arch:

| Tool | Version |
| --- | --- |
| gcc | 15.2.1 (20260209) |
| make | GNU Make 4.4.1 |
| python3 | 3.14.3 |
| git | 2.53.0 |
| curl, tar, xz, file, objdump, readelf | present |

Absent on both, and **not installed** because nothing here needed them: cmake,
ninja, clang, docker, sox, ffmpeg, gnuplot, gdb. The emulator's optional DAW
plugin needs cmake and ninja and is macOS/Apple-silicon only, so it is out of
scope.

## What this bootstrap added

Nothing was installed globally on Windows. Nothing replaced an existing
compiler. `pacman` was never run, so the Arch system packages are untouched.

| What | Where | Version / id |
| --- | --- | --- |
| JieLi pi32v2 toolchain | `~/.jieli/toolchain` (WSL) | `jieli-linux-toolchains-20260730.1`, clang 4.0.1, 136 MB |
| JieLi AC79 AIoT SDK | `~/fw-AC79_AIoT_SDK` (WSL) | branch `AC79NN_SDK_V1.2.1_2023-12-13`, `d179b44`, 1.1 GB |
| Pillow venv | `~/.jieli/venv` (WSL) | Pillow 12.3.0, Python 3.14.3 |
| Linux build copy of Dinghy | `~/fm1lab/dinghy` (WSL) | working copy, see below |
| Workspace | `C:\temp\GitHub\fm1-laboratory` | this tree |

All three installs live outside the project tree, so no toolchain binaries are
in the source. Re-create them with `scripts/setup-wsl-toolchain.sh`, which is
idempotent.

### The toolchain is not pinned — known risk

`scripts/setup-wsl-toolchain.sh` downloads from
`https://pkgman.jieliapp.com/s/linux-toolchain`, which serves **whatever is
current**. It gave us `20260730.1`; fm1-emu's `docs/recon.md` documents
`20250324.1`. The URL offers no way to request a specific build.

This matters because it is the one input we cannot reproduce on demand. It has
not bitten us — see the reproducibility result in `BUILD_AND_TEST.md` — but a
future run on a different day may get a different compiler.

**Mitigation:** the extracted toolchain is kept at `~/.jieli/` and the setup
script never re-downloads when it is already present. Archive that directory
before relying on a build. Tracked in `OPEN_QUESTIONS.md` (Q1).

## Why the work is split across Windows and WSL

Neither side can do all of it:

| Layer | Runs on | Why |
| --- | --- | --- |
| Emulator (`fm1-emu`) | **Windows, native** | Pure Rust, no C dependencies, builds in 14 s with the cargo already installed. No reason to involve WSL. |
| Firmware build | **WSL only** | The pi32v2 toolchain ships as Linux x86-64 binaries. On Windows this would need Docker, which is not installed. |
| Host DSP harness | **WSL** | Windows has no C compiler; WSL has gcc 15.2.1. |

### Filesystem choice, measured

The WSL "move your files to the Linux filesystem" tip is real but matters far
less than it sounds here, because the firmware is small. Measured on this
machine:

| Operation | `/mnt/c` | WSL ext4 | Ratio |
| --- | --- | --- | --- |
| Dinghy clean rebuild | 1–2 s | 0–1 s | ~2x |
| `git status` (per run) | 162 ms | 1 ms | **~160x** |

The build penalty is about one second and does not justify a split tree. The
git penalty is what you actually feel, and it falls on the reference clones,
which we only read.

**Decision:** the workspace stays on `C:\temp\GitHub\fm1-laboratory` — it is
where the Windows-native emulator, your editor and the artifacts live. A Linux
working copy at `~/fm1lab/` is used for firmware builds where the extra second
and faster git are worth it. Both produce byte-identical output (verified).

If iterating on firmware becomes the main activity and 162 ms per git call
grates, move `firmware/` to `~/fm1lab/` permanently; nothing in the harness
depends on the location.

### Git line endings — fixed, but it will come back

Git for Windows was installed with `core.autocrlf=true` **at the system
level**. Every fresh clone under `references/` therefore gets CRLF files, which:

* makes `git status` report every file modified, destroying the clean baseline
  needed for provenance; and
* leaves `\r` in shell scripts, so WSL refuses to run them:
  `./build.sh: /bin/sh^M: bad interpreter`.

That second failure stopped the first Dinghy build outright.

`scripts/fix-line-endings.ps1` sets `core.autocrlf=false` and `core.eol=lf`
**per clone** and re-checks out, then verifies HEAD is unmoved. Your global and
system git config are deliberately left alone so your other repositories keep
behaving as they do today.

**Run it after cloning anything new into `references/`.**

## Reproducing this environment

```powershell
# 1. clone the references (Windows)
.\scripts\clone-references.ps1
.\scripts\fix-line-endings.ps1 -Force      # required, see above

# 2. Linux toolchain (WSL Arch)
wsl -d archlinux -- bash scripts/setup-wsl-toolchain.sh
wsl -d archlinux -- bash scripts/verify-toolchain.sh

# 3. emulator (Windows, native)
cd references\fm1-emu
cargo build --release -p fm1-emu

# 4. firmware packages to test against
.\scripts\fetch-firmware.ps1 dinghy felucca x0x
```

`BUILD_AND_TEST.md` has the verified command for every layer and what each one
actually produced.
