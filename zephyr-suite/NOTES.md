# Idiomatic Zephyr suite — design + porting notes

The CLAUDE.md §6 suite: same chip features as `../baremetal/` (memory/MMU, interrupts,
atomics, dual-core), but on a **full running Zephyr kernel** using **Zephyr APIs + ztest**,
reported by **twister**. Companion docs: `../baremetal/PORTING_NOTES.md` (the exact-transfer
port this parallels) and `rv64gcv-baremetal-testsuite-main/BAREMETAL_REFERENCE.md` (the
feature inventory mirrored here).

**Status: ALL GREEN.** `west twister -T my-app/zephyr-suite -p qemu_riscv64/qemu_virt_riscv64/smode
-p qemu_riscv64/qemu_virt_riscv64/smp` → 8/8 configurations, 49/49 test cases
(P0 5, P1A 3, P1B 12, P1C 5, P1D 5, P2 5, P3 8, P4 6). P1 mirrors the baremetal dirs
one-to-one (`p1a_mmu_enable` … `p1d_tlb_ptw`; the old combined `p1_mmu/` was split).
P1C.1–4 (fix-PTE-and-retry) are now ported via the gated resumable page-fault hook (§2).
Remaining suite cases NOT ported as idiomatic ztests (all documented in-file):
**P1D.6** (PTW-through-D-cache, fix+retry shape, functionally covered by D.1/D.2);
**P2.6** (per-bit `sie.STIE` mask) and **P2.7** (raw `wfi`) — both fight the kernel's
ownership of the system timer. Mechanism caveats (feature covered, mechanism differs):
**P2.2** uses `irq_offload`, not a true CLINT-MSIP self-IPI; **P3.1**'s `.aq/.rl` are
exercised but CAS-level ordering is what the portable path would give.

---

## 1. Shape of the suite

One app tree, mirroring `../baremetal/`'s per-phase layout so future tests follow the same
sequence. Each phase = `CMakeLists.txt` + `prj.conf` + `testcase.yaml` + `src/<phase>.c`,
independently buildable (`west build -b <board> -d build/X my-app/zephyr-suite/<phase>`)
and twister-runnable. No shared `common/` yet: the only shared idiom (the expected-fault
fatal handler, ~12 lines) lives in the two phases that need it; extract it when a third user
appears.

ztest is **not** a separate file/program — `ZTEST()` test bodies, `zassert_*`, and
`ZTEST_SUITE()` live in the same `.c` that calls kernel APIs; ztest provides its own
`test_main()` and the per-test PASS/FAIL + summary output twister parses.

| Phase | Board | Suite features → Zephyr APIs |
|---|---|---|
| `p0_sanity` | smode | UART→`printk`; CSR→`csr_read(sstatus)` (UXL check); **ecall from S-mode**→raw `ecall a7=0xFF` into the resident M-mode SBI runtime, assert `SBI_ERR_NOT_SUPPORTED` + resume (suite P0.3); bonus trap-caught→provoked load access fault, `scause=5`/`stval` asserted in handler; SRAM sweep→1024-word pattern |
| `p1a_mmu_enable` | smode | (all P1 dirs need `CONFIG_RISCV_MMU`, §2) **A.1** satp readback + identity store/load + console-as-MMIO-proof; **A.2** chosen VA 0xC000_0000 → static page with PA-alias readback both directions; **A.3** ifetch via created R\|X mapping |
| `p1b_fault_gen` | smode | all 8 suite tests at the suite's VAs (0xD00x_0000/0xE000_0000), exact scause+stval pinned: **B.1** load V=0 (PPN intact, `map_raw`), **B.2** store V=0 →15, **B.3** exec V=0 →12, **B.4** load X-only →13, **B.5** store RO →15, **B.6** exec no-X →12, **B.7** U-page + `sstatus.SUM=0` →13, **B.8** misaligned megapage (PPN[0]\|1 via `pte_lookup` edit) →13. Plus API-flavor twins (keep-both decision): B.1b surgical V-clear on live PTE, B.5b/6b via `k_mem_map`, B.9 unmapped-after-`k_mem_unmap` |
| `p1c_fault_handle` | smode | **C.1** load fix+retry, **C.2** store fix+retry (add W), **C.3** exec fix+retry (add X), **C.4** 8 sequential fix+retry, all via the new gated **resumable page-fault hook** (§2) — kernel does the retry, the test's `p1c_fixup` resolver repairs the leaf with `z_riscv_mmu_pte_lookup`; **C.5** A/D-mode detection (SVADU on QEMU, hardware-set A asserted; SVADE branch → fatal hook). Resolver is cleared in `before` so only C.1–4 are resumable |
| `p1d_tlb_ptw` | smode | **D.1** targeted `sfence.vma addr` (re-pointed leaf; flushed VA reads new, other VA unaffected), **D.2** stale TLB (no sfence → old-or-new legal, QEMU returns old; after sfence → new), **D.3** TLB thrash 128 pages ×2 passes (512 KB .bss backing), **D.4** 2 MB megapage (single level-1 leaf, image alias), **D.5** gigapage root-leaf (UART+CLINT same root[0] leaf; kernel-design note: image is 4 KB-mapped, gigapage is MMIO). **D.6 (PTW-through-D-cache) not implementable** (needs fix+retry); its essence (PTE edits visible to walker after sfence) is D.1/D.2 |
| `p2_interrupts` | smode (+`CONFIG_RISCV_MMU` for P2.4) | **P2.1** one-shot `k_timer` with chip-level evidence (callback captures `scause`/`sie` from inside the delivering ISR; asserts `scause==Interrupt\|IRQ_S_TIMER` + `sie.STIE` — suite's S-timer-via-SBI, M-mode-injected STIP); **P2.2** `irq_offload()` (soft-IRQ feature; not a true CLINT MSIP self-IPI); **P2.3** periodic timer ×5; **P2.4** timer × page-fault — a fix-retried page fault (the P1C hook) coexisting with a running periodic timer (load lands + timer keeps ticking); **P2.5** `irq_lock` masks expiry / unlock delivers it; `k_sem` from ISR throughout. (P2.6 per-bit STIE mask, P2.7 wfi: baremetal-only) |
| `p3_atomics` | smode | `atomic_cas` (both paths), `atomic_swap/set`, `add/sub/inc/dec` (+64-bit width), `and/or/xor`, signed `min/max`, unsigned `minu/maxu` (riscv arch header). **P3.1** raw `lr.d/sc.d` (+`.aq/.rl`, under `irq_lock`) and **P3.3** 32-bit `.w` AMOs via raw inline asm — `atomic_t` is 64-bit on rv64 and the API hides lr/sc, so the instruction itself is the feature under test |
| `p4_dualcore` | **smp** | pinned thread runs on CPU 1; producer/consumer ordering via atomic flag **both directions** (CPU0↔CPU1, suite P4.1A/B); 2×100 `atomic_inc`→200; `k_spinlock` non-atomic counter→100; sem ping-pong ×20 |

**Why P4 is on the smp (M-mode) board:** stock Zephyr's secondary-hart boot
(`reset.S` `boot_secondary_core`) never does the M→S transition — only the boot hart does —
so **S-mode + SMP is unsupported in-tree** (same gap that forced baremetal P4's gated
secondary divert). The dual-core features under test are privilege-agnostic. Making S-mode
SMP real (secondary M→S + SBI IPI in the resident M-mode runtime) is a possible future
kernel feature.

**Expected-fault pattern** (P0/P1, from `tests/kernel/mem_protect/protection`, extended with
the suite's `g_trap` contract): override the weak `k_sys_fatal_error_handler`; the test
declares `expected_scause`/`expected_stval` (suite: `g_trap.expected_cause/expected_stval`),
sets `expect_fault`, provokes, ends with `zassert_unreachable()`. The handler runs inside
the S-mode exception path where `scause`/`stval` still hold the hardware-written values —
it compares them and calls `ztest_test_pass()` on match / `ztest_test_fail()` on mismatch.
So each fault test pins its exact class: P0 access fault (5, no MMU); P1 page faults
(load 13 / store 15 / ifetch 12, Sv39 on) — the suite-P1A teaching contrast in miniature.
Reaching the handler at all also proves medeleg delegation (an exception arriving in M-mode
would spin in `sbi.S m_mode_unhandled`). The `E: ... ZEPHYR FATAL ERROR` dumps in passing
output are the *expected* exceptions.

## 2. The kernel feature this suite drove: CONFIG_RISCV_MMU (Sv39)

Zephyr had **no page-table MMU on RISC-V** (only PMP, which is M-mode-only; `satp` existed
only as CSR #defines). P1 needed one, so it was added — the §6 "learn Zephyr by doing"
centerpiece. All gated behind `CONFIG_RISCV_MMU` (default n); stock builds and the
baremetal suite are untouched (re-verified: baremetal 8/8 after every kernel change).

| File | Change |
|---|---|
| `zephyr/arch/riscv/Kconfig` | `RISCV_MMU` (depends `RISCV_S_MODE && 64BIT`, selects `CPU_HAS_MMU`+`MMU`) + `RISCV_MMU_NUM_TABLES` (pool size, default 32) |
| `zephyr/arch/riscv/core/mmu.c` | **new** — static page-table pool (no heap: needed pre-`z_cstart` and under spinlock; mirrors arm64 `xlat_tables` and the suite's `pt_alloc_page`); Sv39 3-level walker; `arch_mem_map`/`arch_mem_unmap` (+`sfence.vma`); `z_riscv_mmu_init()` identity-maps the kernel image `[z_mapped_start, z_mapped_end)` 4 KB-paged RWX + low-1 GB MMIO gigapage RW, then writes `satp`. **Raw-control companions** (two-tier model): `z_riscv_mmu_pte_lookup(va)` walks the live tables and returns the (identity-mapped, editable) leaf PTE pointer incl. superpage leaves — caller sfences after edits; `z_riscv_mmu_map_raw(va, pa, pte_bits, level)` is the suite's `pt_map_page`: exact caller-supplied PTE bits (V=0 constructible) at level 0/1/2 (4 KB/2 MB/1 GB), level-aligned, locked, sfenced |
| `zephyr/arch/riscv/core/prep_c.c` | call `z_riscv_mmu_init()` after BSS-zero, before `z_cstart()` |
| `zephyr/arch/riscv/core/CMakeLists.txt` | compile `mmu.c` under the flag |
| `zephyr/include/zephyr/arch/riscv/common/linker.ld` | define `z_mapped_start = ROM_BASE` (non-XIP) — the generic MMU layer's kernel-image bounds (`z_mapped_end` already came from `ram-end.ld`) |
| `zephyr/boards/qemu/riscv64/board.cmake` | gated `,mmu=on` (separate block from the `CONFIG_BAREMETAL_SUITE` one) — QEMU keeps `satp` read-only-zero without it |
| `zephyr/arch/riscv/include/kernel_arch_func.h` | `z_riscv_mmu_init()` + raw-helper + fault-hook prototypes |
| `zephyr/arch/riscv/core/fatal.c` | **resumable page-fault hook**: in `z_riscv_fault()`, for causes 12/13/15, consult a registered resolver and `return` (retry the instruction) if it resolved — same control-flow slot as arm64 `z_arm64_do_demand_paging` / x86 `z_x86_page_fault_handler`. Resolver registered via `z_riscv_mmu_set_fault_fixup()` (in `mmu.c`); **default NULL ⇒ behavior byte-identical to today**. Lets the idiomatic suite do P1C.1–4 fix-PTE-and-retry. NOT demand paging (handles permission-upgrade faults 15/12 that paging doesn't model) |

Design choices: **identity-mapped** (VA==PA, like the suite — no virt/phys offset juggling);
**boot maps only kernel image + MMIO**, leaving the rest of RAM to the generic page-frame
allocator (`k_mem_map` hands it out — mapping all RAM up front would alias the frame pool);
kernel-mode only (no `K_MEM_PERM_USER` users yet — RISC-V userspace needs PMP, a different
mechanism). Not done: SMP secondary-hart `satp` (S-mode SMP doesn't exist anyway, see §1),
demand paging, fine-grained W^X of the kernel image itself.

**P2.1 timer-flow note:** `k_timer` on the smode board is the suite's P2.1 flow verbatim
(verified in source): the kernel timer driver (`riscv_supervisor_timer.c`) has its own
`sbi_set_timer()` issuing the `SBI_EXT_TIME` ecall; `sbi.S` writes CLINT `mtimecmp`; MTIP
fires in M-mode and `sbi.S` *forwards to S-mode by raising STIP* (exactly the suite's
`m_trap.c` reflection); the S-timer interrupt (`scause=Interrupt|5`) runs the driver ISR and
the k_timer callback. The k_timer expiry callback runs nested in that ISR, where `scause`
still holds the live cause — P2.1 captures and asserts it as direct evidence.

**P0.3 firmware-policy note:** an unknown `ecall` from S-mode behaves differently under the
suite's custom firmware vs Zephyr's: the suite's `m_trap.c` *reflected* it back to S-mode as
an exception (`scause=9`), while Zephyr's `sbi.S` — like OpenSBI on real silicon — follows
the SBI spec: handled in M-mode, returns `SBI_ERR_NOT_SUPPORTED` in `a0`, resumes at
`mepc+4`. Same chip flow (S→M trap via medeleg, firmware decode, resume); the idiomatic test
asserts the SBI-spec outcome. The positive SBI path is exercised by P2 implicitly (the
kernel timer driver issues `SBI_EXT_TIME` ecalls for every `k_timer`/`k_msleep`).

## 3. Pitfalls hit (so you don't re-hit them)

- **`z_mapped_start` must be page-aligned and cover the whole image.** First attempt placed
  it at `__text_region_start` (mid-page, after the reset/exception sections):
  `ASSERTION FAIL [phys % 0x1000 == 0]` in the page-frame DB. `hello_world` masked it
  (no `CONFIG_ASSERT`); ztest caught it. Anchor at `ROM_BASE`.
- **`k_mem_map` zeroes the new mapping through itself** — a read-only mapping faults during
  setup unless `K_MEM_MAP_UNINIT` is passed (the RO-write test does).
- **csr.h already defines `PTE_V/R/W/X/U/A/D` + `PTE_PPN_SHIFT`** — don't redefine in
  `mmu.c`; twister builds with `-Werror` (plain `west build` only warned).
- **Twister's QEMU console reader is ASCII-only** (decodes byte-by-byte): an em-dash in a
  `printk` → `FAILED: unexpected byte`, log truncated mid-test. Keep test output ASCII.
- In-tree `tests/kernel/mem_protect/mem_map` is `arch_allow: arm, x86` (needs a
  twister-injected linker section). Not worth forcing; this suite's P1 covers `k_mem_map`.
- Tests may call the arch MMU API (`arch_mem_map`/`arch_mem_unmap`) directly for chosen-VA
  mappings by adding `${ZEPHYR_BASE}/kernel/include` to the app include path — in-tree
  precedent: `tests/arch/arm64/arm64_mmu`, `tests/kernel/mem_protect/mem_map`. Pick VAs
  outside the kernel image, the VM region (`CONFIG_KERNEL_VM_BASE`+`SIZE`), and the MMIO
  gigapage (P1 uses 0xC000_0000/0xC001_0000 = Sv39 root[3]).
- **Manual mapping / permission bits — the two-tier model (both methods first-class):**
  portable flags (`K_MEM_PERM_RW/EXEC/USER`) cover V (map/unmap), W, X, U; raw PTE bits
  (V-only games, A/D, G, X-only, superpage levels) use the gated helpers
  `z_riscv_mmu_pte_lookup` (edit any bit on a live PTE + own `sfence.vma`) and
  `z_riscv_mmu_map_raw` (exact bits + level, the suite's `pt_map_page`). Kernel stays
  fully active; everything is behind `CONFIG_RISCV_MMU`.
- When a megapage/raw mapping aliases the kernel image, reference reads through the
  *identity* mapping must stay below `z_mapped_end` — only the image is identity-mapped at
  boot (the alias side covers the whole superpage; see `test_raw_megapage_map`).
- Stray QEMU from a previous run → `cannot lock pid file`: `pkill -f qemu-system-riscv64`
  and remove `build/*/zephyr/qemu.pid`.

## 4. Build / run / report

```sh
# one phase, directly (bound the run so output flushes):
west build -p always -b qemu_riscv64/qemu_virt_riscv64/smode -d build/zs_p1 my-app/zephyr-suite/p1_mmu
perl -e 'alarm 30; exec @ARGV' west build -t run -d build/zs_p1
# p4_dualcore uses -b qemu_riscv64/qemu_virt_riscv64/smp

# the whole suite, the §6 way:
west twister -T my-app/zephyr-suite \
  -p qemu_riscv64/qemu_virt_riscv64/smode -p qemu_riscv64/qemu_virt_riscv64/smp
```

Done = twister `executed test configurations passed (100.00%)`; per-phase done = ztest
`PROJECT EXECUTION SUCCESSFUL`.
