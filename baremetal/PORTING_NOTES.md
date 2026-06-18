# Porting Notes — RV64 bare-metal suite → ZephyrOS/QEMU

Cumulative record of **how** the port works and **why** each decision was made, so a fresh
session (or a new engineer) can reconstruct the reasoning. Companion docs:
`../../rv64gcv-baremetal-testsuite-main/BAREMETAL_REFERENCE.md` (original suite internals)
and `CLAUDE.md` §5 (terse guidelines). **Status: P0, P1A–P1D, P2, P3, P4 all pass on
`qemu_riscv64/qemu_virt_riscv64/smode`.**

---

## 1. The overarching strategy: "exact transfer", not idiomatic ztest

The senior's hard constraint: **do not change the test sequence.** The suite is validated
on Spike (logic) and Verilator (RTL); there is no physical chip yet. So we *transfer* the
exact suite onto Zephyr/QEMU rather than re-expressing tests as idiomatic Zephyr `ztest`.

Consequence — **we bypass the Zephyr kernel**: Zephyr's `reset.S` does its normal early
boot and M→S transition, then (under our flag) **diverts into the suite instead of starting
the kernel** (`z_cstart` never runs). Zephyr therefore acts as: toolchain + boot/M→S setup
+ QEMU runner. The suite keeps full control of boot/traps via its *own* `m_trap.c`
(M-mode handler) and `s_trap_handler` (S-mode), and the test bodies (`tests/p*.c`) compile
**verbatim**. This is the deliberate trade-off of "don't change the test sequence."

Why **S-mode** target (`…/smode` board): the suite runs tests in S-mode with an SBI/firmware
in M-mode beneath — exactly Zephyr's S-mode build (M-mode boot → `mret` to S-mode, with a
resident M-mode handler). So `sstatus`/`satp`/S-traps/SBI map 1:1. The MMU phases (P1) need
S-mode (`satp` paging applies to S/U), and P2's SBI timer/IPI is literally what the M-mode
handler provides.

Why everything is **gated behind `CONFIG_BAREMETAL_SUITE`** (default n): so normal Zephyr
builds are unaffected (verified — stock `hello_world` boots normally with the flag off).

---

## 2. The gated Zephyr-tree edits (and the reason for each)

| File | Change (only when `CONFIG_BAREMETAL_SUITE=y`) | Why |
|------|-----------------------------------------------|-----|
| `zephyr/arch/riscv/Kconfig` | Defines the `CONFIG_BAREMETAL_SUITE` option | The on/off switch; keeps all other edits opt-in. |
| `zephyr/arch/riscv/core/reset.S` | (a) `mtvec` → suite's `_m_trap_vector` instead of Zephyr's `__m_mode_sbi_handler`; (b) divert M→S handoff to `baremetal_main` instead of `z_prep_c`; (c) enable `mie.MSIE` instead of `mie.MTIP` at boot; (d) secondary hart → `core1_smode_park_loop` | (a) so an unknown S-mode `ecall` is *reflected* back to S-mode like the suite's `m_trap.c` (P0.3); (b) the kernel-bypass; (c) P2 needs software-interrupt delivery + avoids a spurious boot timer (see §4-P2); (d) P4's second hart (see §4-P4). |
| `zephyr/boards/qemu/riscv64/board.cmake` | Append `,mmu=on` to the QEMU `-cpu` string | The smode board advertises `sv39=on` but not the MMU capability; QEMU keeps `satp`=0 (Bare) without `mmu=on` (see §4-P1A). |

Plus **per-app `prj.conf`** (not a Zephyr-tree edit): every phase sets
`CONFIG_BAREMETAL_SUITE=y`; P4 additionally sets `CONFIG_MP_MAX_NUM_CPUS=2` (→ QEMU
`-smp cpus=2`) and `CONFIG_QEMU_ICOUNT=n` (icount is incompatible with `-smp >1`).

---

## 3. The app harness (`my-app/baremetal/`)

- `common/` — shared, phase-independent:
  - `baremetal_main.c` — diverted-to from `reset.S`; calls the suite's `s_mode_entry()`;
    provides a never-called `main()` to satisfy the linker (kernel links but never runs).
  - `smode.c`, `m_trap.c` — **copies** of the suite's files; the ONLY change is the verdict
    sink: the original wrote HTIF `tohost` (Spike testbench); we write the QEMU `sifive_test`
    finisher at `0x0010_0000` (`0x5555`=pass→exit 0, `0x3333|(code<<16)`=fail→exit code).
  - `baremetal_trap.S` — the suite's `_m_trap_vector` + `_s_trap_vector` (extracted verbatim
    from the suite's `boot.S`; the `_start`/boot part is omitted — Zephyr's `reset.S` boots).
  - `baremetal_syms.S` — the linker symbols the suite's `link.ld` would provide
    (`_heap_*`, `_page_table_*`, `_m_trap_info`, and P4's `_bm_secondary_stack_top`),
    as reserved `.bss`. We build under Zephyr's linker, so we supply these here.
  - `baremetal.cmake` — `baremetal_app(<phase>)`: sets the suite include dir,
    `UART_REG_SHIFT=0` (QEMU NS16550 stride) and `asm=__asm__` (Zephyr builds strict ISO C,
    which disables the GNU `asm` keyword the suite uses), then compiles the shared files +
    `…/tests/<phase>.c` verbatim.
- Per phase (`p0_sanity/`, `p1a_mmu_enable/`, …, `p4_dualcore/`) — a thin dir: just
  `CMakeLists.txt` (`include(../common/baremetal.cmake); baremetal_app(<phase>)`) + `prj.conf`.

Build/run a phase:
```
west build -p always -b qemu_riscv64/qemu_virt_riscv64/smode -d build/<phase> my-app/baremetal/<phase>
perl -e 'alarm 25; exec @ARGV' west build -t run -d build/<phase>   # bound it so output flushes
```
Pass = the suite prints `Phase N: ALL TESTS PASSED` + `[PASS]` and QEMU exits 0.

---

## 4. Per-phase flow + what each taught us

**P0 — sanity (UART, CSR, S-mode ecall trap, SRAM).** First proof of the kernel-bypass.
P0.3 exercises the key flow: S-mode `ecall a7=0xFF` → traps to M-mode (`_m_trap_vector` →
`m_trap.c`) → unknown SBI → *reflected* to S-mode (`scause=9`) → `s_trap_handler` records
`g_trap` → returns. Needed the `mtvec`→suite-vector and the divert edits.

**P1A — MMU enable / identity map / 4 KB map / ifetch.** *The `mmu=on` discovery.* First run
failed: P1A.2 store to `0xc000_0000` → `cause=7` (store **access** fault, i.e. no RAM —
*not* a page fault). Diagnosis: `qemu … -d mmu,guest_errors` showed `physical == address`
everywhere (no translation), and a `satp` read-back returned **0** despite writing
`0x8…00080009`. Root cause: the board's `-cpu` string had `sv39=on` but no `mmu=on`, so
QEMU 10.x hardwired `satp` to 0 (Bare mode). P1A.1 "passed" spuriously because identity ≡
Bare for RAM. Fix: gated `,mmu=on` in `board.cmake`.

**P1B / P1C / P1D — page-fault gen / fix+retry / TLB+PTW.** Ran with **no new changes** —
they exercise the suite's S-mode page-fault path (`s_trap_handler`, `handler_action`
0=fix/1=skip), already present. Notable QEMU behaviors (the tests accommodate both): P1C.5
→ **SVADU** (QEMU sets A/D bits in hardware); P1D.2 → stale-TLB returns the **old** value
before `sfence.vma` (QEMU caches the translation). Spec-compliant either way.

**P2 — interrupts (timer/software via SBI, masking, wfi).** First real interrupt path. Two
boot gaps vs the suite's `boot.S`, both fixed by enabling `mie.MSIE` instead of `mie.MTIE`
at boot: (1) P2.2's self-IPI (`sbi_send_ipi(0)`→CLINT MSIP) needs MSIE or the M-software
interrupt never fires; (2) QEMU resets `mtimecmp=0`, so leaving MTIE on at boot fires a
*spurious* timer that injects a stale `STIP`, which would make P2.1 count 2 timer IRQs
instead of 1. The suite arms the timer itself via SBI (`m_trap.c` sets MTIE on demand), so
MTIE isn't needed at boot — dropping it kills the spurious timer; MSIE-on enables IPIs.

**P3 — atomics (LR/SC, AMO .d/.w, CAS).** Pure inline asm in `test.h`; no interrupts/MMU/
boot deps. Passed with just a thin app dir.

**P4 — dual-core (wake, fence, atomic stress, spinlock, ping-pong).** The only phase needing
a **second hart**. Mechanics: `CONFIG_MP_MAX_NUM_CPUS=2` → QEMU `-smp cpus=2`;
`CONFIG_QEMU_ICOUNT=n` (icount ⊥ smp). Both harts start at reset; `reset.S`
`boot_secondary_core` (gated) gives hart 1 its own stack + the same M-setup hart 0 got
(open PMP — *S-mode has no memory access without a PMP entry* — `mtvec`, delegation,
`mie.MSIE`, `MPP=S`) and `mret`s it into `core1_smode_park_loop()`, where it `wfi`s waiting
for an IPI. Wake path: hart 0 `wake_core1_smode(fn)` → `sbi_send_ipi(1)` → CLINT `MSIP[1]` →
hart 1 M-soft → `m_trap.c` injects `SSIP` → S-soft → park loop runs `fn`. Sync globals
(`g_core1_smode_ready`, `g_smode_target`) start at 0 because **QEMU zero-inits guest RAM**
(also why P0–P3 never needed an explicit BSS-zero — on real HW we *would* need one).

---

## 5. QEMU facts this port relies on

- The suite's addresses already match the QEMU `virt` map: SRAM `0x8000_0000`,
  CLINT `0x0200_0000`, UART0 NS16550 `0x1000_0000` (reg-shift 0) — no remapping.
- `virt -bios none`: Zephyr is the M-mode firmware (its `reset.S` + the suite's `m_trap.c`),
  no OpenSBI.
- QEMU **zero-inits guest RAM** → `.bss` is effectively zero at boot (no BSS-zero needed).
- Pass/fail via the `sifive_test` finisher (`0x0010_0000`).
- `satp`/Sv39 needs the CPU `mmu=on` (QEMU 10.x); `-icount` is incompatible with `-smp >1`.
- RAM size differs (Spike 2 MB vs QEMU 256 MB) but is harmless — the suite's `link.ld`-style
  symbols pin everything into the first 2 MB; the extra RAM is unused.

---

## 6. What's intentionally NOT done

- No `ztest`/`twister` integration (we bypass the kernel; verdict is the finisher).
- No custom RV64GCV board/SoC (future: real silicon bring-up — and there a real BSS-zero,
  vector `V` tests, and a firmware decision (own `m_trap.c` vs external OpenSBI) come back).
- No vector (`V`) tests — the suite has none yet.
