# RV64GC Bare-Metal Test Suite — Reference

Reference notes for the existing bare-metal suite in this folder, written to support
porting it to **ZephyrOS / ztest**. The eventual port lives in `../my-app`. For the
porting strategy and project guidelines, see `../CLAUDE.md` §5.

> The suite calls itself "RV64GC dual-core SoC with Sv39 MMU" (`README.md`). The target
> chip is RV64**GC V** (vector), but the current tests exercise only RV64GC — **there
> are no vector (`V`) tests yet**. Vector coverage is a future addition.

---

## 1. Build & Run

| Item | Value |
|------|-------|
| Toolchain | `riscv64-unknown-elf-` (gcc/as/ld/objcopy/objdump/size) |
| Arch flags | `-march=rv64gc -mabi=lp64d` |
| Code model | `-mcmodel=medany` |
| Freestanding | `-nostdlib -nostartfiles -ffreestanding -fno-builtin -fno-common`, links `-lgcc` |
| Linker script | `common/link.ld` |
| Optimization | `-O1 -g` |
| Simulator | **Spike** (`--isa=rv64gc -m0x80000000:0x200000 -p2`) — 2 MB @ 0x8000_0000, 2 harts |
| RTL sim | Verilator (consumes `.bin` / `.memh64`) |

Two build flavors (`Makefile`):
- **Spike** (`build-spike/`, default): adds `-DSPIKE_BUILD -DUART_REG_SHIFT=0`. Output via
  HTIF console protocol.
- **RTL** (`build-rtl/`): no `SPIKE_BUILD`; `UART_REG_SHIFT=2` (4-byte 16550 stride). Also
  emits `.bin` (`--set-start 0x80000000`) and `.memh64` for Verilog `$readmemh`.

Key make targets: `make spike` (build all 8 ELFs), `make run` / `make run-all` (build +
run all on Spike sequentially, stop on first FAIL), `make run-p0`…`run-p4`,
`make rtl`, `make memh64`, `make dumps`, `make clean`.

Pass/fail at the make level = Spike **exit code** (0 = PASS). Tests drive that exit code
through HTIF (see §5).

---

## 2. Memory Map (`common/platform.h`, `common/link.ld`)

| Region | Base | Size / Notes |
|--------|------|--------------|
| DEBUG | `0x0000_0000` | 4 KB |
| BOOTROM | `0x0000_1000` | 4 KB |
| CLINT | `0x0200_0000` | 48 KB — `msip[h]`=+0, `mtimecmp[h]`=+0x4000, `mtime`=+0xBFF8 |
| PLIC | `0x0C00_0000` | 64 MB (declared, not exercised) |
| UART0 | `0x1000_0000` | 4 KB — `THR`=+0, `LSR`=+5 (shifted by `UART_REG_SHIFT`), `LSR.THRE`=bit5 |
| SPI0 / GPIO / DMA | `0x1000_1000` / `_2000` / `_3000` | declared, not exercised |
| **SRAM** | `0x8000_0000` | **2 MB** (`SRAM_SIZE`, overridable) |
| HTIF `tohost` | `SRAM_END − 0x1000` | i.e. `0x801F_F000` |
| HTIF `fromhost` | `SRAM_END − 0x0FF8` | i.e. `0x801F_F008` |

SRAM layout (`link.ld`): `.text.entry` @ `0x8000_0000`, then `.text/.rodata/.data`, then
`.bss`. Inside BSS: a **32 KB page-table pool** (`_page_table_start.._end`), two **16 KB
stacks** (`STACK_SIZE=0x4000`) for hart0/hart1, then heap up to the HTIF block.
`NUM_HARTS = 2`.

---

## 3. Boot & Privilege Flow (`common/boot.S`)

1. `csrr mhartid`: **hart 0** takes `_stack0_top`, **hart 1** takes `_stack1_top`. Only
   hart 0 clears BSS.
2. M-mode setup (hart 0): set `mtvec`=`_m_trap_vector`; open **PMP** fully
   (`pmpaddr0=-1`, `pmpcfg0 = R|W|X|NAPOT`); delegate exceptions
   `medeleg = MEDELEG_DEFAULT (0x0000_B3FF)` (note: the SBI-aware variant
   `MEDELEG_WITH_SBI = 0x0000_B1FF` keeps ecall-from-S in M-mode); delegate interrupts
   `mideleg = 0x222` (S soft/timer/ext); enable M soft+timer in `mie`; set
   `mstatus.MPP = S`.
3. `mepc = s_mode_entry`; `mret` → drops hart 0 into **S-mode**.
4. Hart 1: `mepc = core1_smode_park_loop`; `mret` → parks in S-mode waiting for IPI.

So every test runs in **S-mode**, with M-mode acting as a tiny firmware/SBI layer
underneath.

---

## 4. Trap Architecture (`common/m_trap.c`, `common/smode.c`)

**M-mode trap vector** saves a 16-reg frame, calls
`m_trap_handler(mcause, mepc, mtval, frame)`, restores `mepc` from the return value, `mret`.
It implements a minimal **SBI** (dispatched on `ecall`-from-S, fid in `a7`):
- `SBI_SET_TIMER (0)`: program `mtimecmp[hart]`, clear `mip.STIP`, enable `mie.MTIP`.
- `SBI_SEND_IPI (1)`: set `msip[target]`; returns `mepc+4`.
- `SBI_CLEAR_IPI (2)`: clear `mip.SSIP`.
- M timer/soft IRQs are reflected down to S-mode by setting `mip.STIP` / `mip.SSIP`.
- Unknown ecalls are forwarded to the S-mode handler (writes `scause/sepc/stval`, jumps `stvec`).

**S-mode trap vector** saves a full 32-reg / 256-byte frame (`sepc` at `frame[30]`), calls
`s_trap_handler(scause, stval, sepc, frame)`, restores `sepc` (handler may advance it to
skip an instruction), `sret`. It handles:
- IRQs: S-timer (`5`) and S-soft (`1`) bump counters in `g_trap`, then clear the pending bit.
- Page faults (`12/13/15`): two modes via `g_trap.handler_action` — **0 = fix-PTE**
  (walk page table, OR in `V/A/D` plus the needed `R/W/X`, `fence`, `sfence.vma`, retry)
  or **1 = skip** (advance `sepc` past the faulting instruction).
- Illegal instruction (`2`) → `test_fail(300)`.

**The trap contract** is the global `struct trap_state g_trap` (`common/test.h`):
`scause/stval/sepc`, `fault_count`, `expected_cause`, `expected_stval`, `handler_action`,
and volatile IRQ counters (`timer_irq_count`, `soft_irq_count`, `last_irq_cause`). Tests
set expectations into `g_trap`, provoke a trap, then assert on what the handler recorded.

---

## 5. Test Harness (`common/test.h`, `common/smode.c`)

**Console**: `uart_putc()` — under `SPIKE_BUILD` uses the HTIF `tohost` console command
(`(1<<56)|(1<<48)|c`); otherwise polls `UART0_LSR.THRE` then writes `UART0_THR`
(8-bit if `UART_REG_SHIFT==0`, else 32-bit). Plus `uart_puts/put_hex/put_dec`.

**Pass/fail → HTIF** (the actual verdict mechanism):
- `test_pass()` → `htif_write(1)` (tohost = 1), then `wfi` loop. Spike exits 0.
- `test_fail(code)` → `htif_write((code<<1)|1)`, then `wfi`. Spike exits non-zero;
  bit0=1 signals failure, upper bits carry `code`.

**Assertion**: `ASSERT_EQ(actual, expected, msg, code)` prints a diagnostic and calls
`test_fail(code)` on mismatch. Tests are otherwise plain C functions invoked from a
per-file entry; "all asserts passed" ends in `test_pass()`.

**Inline helpers** in `test.h`: SBI wrappers (`sbi_set_timer`, `sbi_send_ipi`,
`sbi_clear_ipi`), CSR read/write macros, atomics (`lr_d`/`sc_d`, `amo_*_d`/`_w`, `cas_d`),
fences (`fence_i`, `sfence_vma_all`, `sfence_vma_addr`), and Sv39 page-table helpers
(`pt_alloc_page`, `pte_leaf`, `pt_setup_identity_kernel`, `mmu_enable_sv39`).

Sv39 details: `PGSHIFT=12`, megapage=2 MB, gigapage=1 GB, 512 PTEs/level,
`PTE_PPN_SHIFT=10`; bits `V/R/W/X/U/G/A/D`; `satp` mode field at bit 60 (`SV39=8`).

---

## 6. Per-Phase Inventory

| Phase | File | Feature under test | Key CSRs / ops | Verdict |
|-------|------|--------------------|----------------|---------|
| **P0** | `tests/p0_sanity.c` | UART TX, CSR read (`sstatus`), S-mode trap via non-SBI ecall, SRAM r/w sweep (1024 words) | `csrr`, `ecall`, `g_trap` | `test_pass`/`ASSERT_EQ` |
| **P1A** | `tests/p1a_mmu_enable.c` | Enable Sv39 + identity map; 4 KB page map; instruction fetch through a mapped page | `satp`, `sfence.vma`, `fence.i` | translate load/store/exec correctly |
| **P1B** | `tests/p1b_fault_gen.c` | Generate each fault class: V=0 load/store/exec; R=0; W=0; X=0; U-page w/ SUM=0; misaligned superpage | page faults `12/13/15`, `sstatus.SUM` | assert `scause`/`fault_count` (skip-mode) |
| **P1C** | `tests/p1c_fault_handle.c` | Fix-PTE-and-retry for load/store/exec; 8 sequential faults; A/D-bit mode detection (SVADU vs SVADE) | `handler_action=0`, PTE walk, `sfence.vma` | retried access succeeds |
| **P1D** | `tests/p1d_tlb_ptw.c` | Targeted `sfence.vma`; stale TLB w/o flush; TLB thrash (128 pages); megapage; gigapage; PTW through D-cache | `sfence.vma addr`, leaf PTEs | correct/old-or-new values per spec |
| **P2** | `tests/p2_interrupts.c` | S-timer IRQ, S-soft self-IPI, repeated timers, timer×fault race, SIE enable/disable, `sie.STIE` mask, `wfi` | SBI timer/IPI, `sstatus.SIE`, `sie`, `sip`, `wfi` | IRQ counters / `last_irq_cause` |
| **P3** | `tests/p3_atomics.c` | LR/SC (+ aq/rl), 64-bit AMOs (swap/add/and/or/xor/min/max/minu/maxu), 32-bit AMOs, CAS via LR/SC | `lr.d`/`sc.d`, `amo*.d`/`.w`, fences | result values via `ASSERT_EQ` |
| **P4** | `tests/p4_dualcore.c` | Wake hart 1 (IPI); fence-ordered producer/consumer; atomic-increment stress (→200); spinlock mutex (→100); ping-pong | `sbi_send_ipi`, `amoadd.d`, `amoswap.d`, fences, park loop | shared-counter expected totals |

Hart-1 wake path: `wake_core1_smode(fn)` waits for `g_core1_smode_ready`, publishes
`g_smode_target=fn` (`fence w,w`), sends IPI; `core1_smode_park_loop` runs `fn` then clears
the target and `wfi`s again.

---

## 7. Mapping to Zephyr (porting orientation)

What Zephyr already owns — **don't re-port these**, trust the kernel and assert behavior:

| Bare-metal piece | Zephyr equivalent |
|------------------|-------------------|
| `boot.S`, M-mode setup, PMP, deleg | Zephyr arch init / `arch/riscv` early boot |
| Hand-rolled SBI (`m_trap.c`) | On real HW: OpenSBI/firmware; on `qemu_riscv64`: provided |
| S-mode trap vector / dispatch | Zephyr exception & IRQ framework (`IRQ_CONNECT`, `_isr`) |
| Sv39 page-table helpers, `satp` setup | Zephyr `CONFIG_MMU` / `arch_mem_map`, demand-paging APIs |
| CLINT timer programming | Zephyr timer driver + `k_timer` / IRQ APIs |
| `test_pass`/`test_fail` + HTIF, `ASSERT_EQ` | **ztest**: `ZTEST`/`ZTEST_SUITE`, `zassert_*`; twister reporting |
| UART console (`smode.c`) | Zephyr console / `printk` over the board's UART driver |

What stays as **raw inline asm** in the ztest port (no kernel equivalent — the chip
feature *is* the thing under test): CSR reads, `amo*`/`lr`/`sc`, `fence`/`fence.i`,
`sfence.vma`, and deliberate fault-provoking accesses.

Net: porting is **re-architecting**, not copy-paste. Each test becomes
"exercise the RISC-V feature + `zassert` the observable result," running first on
`qemu_riscv64`, later on the custom RV64GCV board.

---

## 8. File Index

| File | Purpose |
|------|---------|
| `Makefile` | Spike/RTL builds, run targets, memh64 |
| `Dockerfile` | Ubuntu 22.04 + RISC-V toolchain + Spike |
| `README.md` | Phase summary table |
| `common/platform.h` | Addresses, CSR/PTE/cause/SBI constants |
| `common/asm_defs.h` | Assembly-only constants |
| `common/link.ld` | SRAM layout, page-table pool, stacks, HTIF |
| `common/boot.S` | Hart split, M-mode setup, M/S trap vectors |
| `common/m_trap.c` | M-mode trap + SBI dispatch |
| `common/smode.c` | S-mode runtime: UART, page tables, S-trap handler, core-1 wake |
| `common/test.h` | `g_trap`, `ASSERT_EQ`, SBI/atomic/PT inline helpers |
| `common/spike_uart.c` | Spike UART helper |
| `tests/p*.c` | P0–P4 test bodies (see §6) |
