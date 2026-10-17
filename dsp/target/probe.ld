/* SPDX-License-Identifier: MIT
 *
 * Linker script for the Braids cycle probe.
 *
 * Derived from references/fm1-emu/probes/harness/probe.ld (MIT, Joakim
 * Eriksson), with one addition: symbols around .data and its load address.
 *
 * WHY. The upstream probe script places .data in RAM with a load address in
 * XIP, but the probe crt0 never copies it -- upstream's probes are plain C
 * with no initialised globals, so it never mattered. Braids is C++ and its
 * dispatch tables are initialised data:
 *
 *   AnalogOscillator::fn_table_    0x48 bytes
 *   DigitalOscillator::fn_table_  0x118 bytes
 *   MacroOscillator::fn_table_    0x180 bytes
 *
 * Without the copy those read as zeros and Render() jumps to 0x00000000.
 * (A real firmware's crt0 does this copy; see Dinghy's.)
 */
/* RAM starts ABOVE the results block, not 32 KiB below it.
 *
 * probes/harness/probe.h hardcodes RESULTS at 0x01C10000, and the upstream
 * region began at 0x01C08000 -- fine for upstream's probes, which hold a few
 * hundred bytes of state. probe_drone.cc holds four BraidsVoice objects at
 * 17,152 B each and ran its .bss 56 KB past RESULTS, so the probe wrote its
 * own pass/fail verdict on top of its own DSP state and still reported
 * "0 failures". The cycle counts survived (those come from the emulator's
 * instruction counter at the halt breakpoint, not from RESULTS) but the
 * self-check did not.
 *
 * Starting above RESULTS makes the collision impossible at any .bss size
 * rather than merely unlikely. The probe no longer mirrors the firmware's RAM
 * layout, which costs nothing: it is not the firmware, and nothing it
 * measures depends on its own addresses. */
MEMORY
{
    XIP (rx)  : ORIGIN = 0x02000120, LENGTH = 0x40000
    RAM (rwx) : ORIGIN = 0x01C10040, LENGTH = 0x37FC0
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); } > XIP

    .data : {
        . = ALIGN(4);
        _data_start = .;
        *(.data*) *(.sdata*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM AT > XIP
    _data_load = LOADADDR(.data);

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM

    /DISCARD/ : { *(.comment) *(.note*) }
}

/* Ties the RAM origin above to the RESULTS address in probes/harness/probe.h.
 * The overlap is structural now rather than a size limit, so this only fires
 * if someone moves ORIGIN back down -- which is exactly how it happened the
 * first time. */
ASSERT(_data_start >= 0x01C10040,
       "probe RAM must start above the RESULTS block at 0x01C10000")
