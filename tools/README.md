# tools/

**Empty.** Nothing needed to live here, and that is deliberate.

## Where the tooling actually is

| Tool | Location | Why there |
| --- | --- | --- |
| pi32v2 clang 4.0.1 | `~/.jieli/toolchain` (WSL) | Dinghy's `build.sh` default; keeps 136 MB of vendor binaries out of the tree |
| JieLi AC79 SDK | `~/fw-AC79_AIoT_SDK` (WSL) | Dinghy's default; 1.1 GB |
| Pillow venv | `~/.jieli/venv` (WSL) | needed by Dinghy's build and host mode |
| `fm1-emu.exe` | `references/fm1-emu/target/release/` | cargo build output, stays with its source |
| Our scripts | `scripts/` | small, text, version-controlled |

Install the first three with `scripts/setup-wsl-toolchain.sh` (idempotent) and
check them with `scripts/verify-toolchain.sh`.

## Upstream tools worth knowing about

Not installed — listed so they are findable when needed.

| Tool | Where | For |
| --- | --- | --- |
| `fm1_identify.py` | `references/lunar-modulator/tools/` | read model, firmware version and flash ID from a connected FM-1. Verified on hardware upstream. **First thing to run when a device arrives.** |
| `fm1_install.py` | `references/dinghy/tools/` | install a `.fwsc` over USB. **Do not run** until the gate in `docs/FIRMWARE_SAFETY.md` is satisfied. |
| `tools/disasm.py`, `probes/` | `references/fm1-emu/` | pi32v2 disassembly and differential ISA probes |
| `jl-uboot-tool`, `jl-misctools` | kagaimiq, not cloned | mask-ROM boot and `.fwsc` container parsing |
| FM-1-Transporter | kurogedelic, not cloned | full 1 MiB flash read/write over USB |

## If something does end up here

Keep binaries out of git — `.gitignore` already excludes `tools/toolchain/` and
`tools/*/bin/`. Prefer a script in `scripts/` that fetches and verifies a tool
over committing the tool itself.
