# --- Build ---
# make all          # Default: builds Spike ELFs
# make spike
# make rtl
# make memh64       # Build all .memh64 files for Verilog $readmemh
# make run          # Run ALL tests sequentially on Spike
# make dumps        # Generate .dump files for all Spike tests
# make dumps-spike  # Same as above
# make dumps-rtl    # Generate .dump files for all RTL tests
# make clean

CROSS_COMPILE ?= riscv64-unknown-elf-
CC      = $(CROSS_COMPILE)gcc
AS      = $(CROSS_COMPILE)gcc
LD      = $(CROSS_COMPILE)gcc
OBJDUMP = $(CROSS_COMPILE)objdump
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE    = $(CROSS_COMPILE)size
SPIKE  ?= spike

ARCH_FLAGS = -march=rv64gc -mabi=lp64d

COMMON  = common
TESTS   = tests
SCRIPTS = scripts
SIM     = sim

# --- Spike build ---
BUILD_SPIKE   = build-spike
CFLAGS_SPIKE  = $(ARCH_FLAGS) -mcmodel=medany -fno-common -fno-builtin \
                -nostdlib -nostartfiles -ffreestanding \
                -Wall -Wextra -Wno-unused-parameter -O1 -g \
                -DSPIKE_BUILD -DUART_REG_SHIFT=0 -I$(COMMON)
ASFLAGS_SPIKE = $(ARCH_FLAGS) -D__ASSEMBLY__ -I$(COMMON)
LDFLAGS_SPIKE = $(ARCH_FLAGS) -nostdlib -nostartfiles -T $(COMMON)/link.ld

# --- RTL build (no SPIKE_BUILD, default UART reg shift) ---
BUILD_RTL     = build-rtl
CFLAGS_RTL    = $(ARCH_FLAGS) -mcmodel=medany -fno-common -fno-builtin \
                -nostdlib -nostartfiles -ffreestanding \
                -Wall -Wextra -Wno-unused-parameter -O1 -g \
                -I$(COMMON)
ASFLAGS_RTL   = $(ARCH_FLAGS) -D__ASSEMBLY__ -I$(COMMON)
LDFLAGS_RTL   = $(ARCH_FLAGS) -nostdlib -nostartfiles -T $(COMMON)/link.ld

# Sources
BOOT_SRC = $(COMMON)/boot.S
SHARED_C = $(COMMON)/smode.c $(COMMON)/m_trap.c
HEADERS  = $(COMMON)/platform.h $(COMMON)/test.h $(COMMON)/asm_defs.h

TEST_NAMES = p0_sanity p1a_mmu_enable p1b_fault_gen p1c_fault_handle \
             p1d_tlb_ptw p2_interrupts p3_atomics p4_dualcore

# Derived — Spike
SPIKE_BOOT   = $(BUILD_SPIKE)/boot.o
SPIKE_SHARED = $(patsubst $(COMMON)/%.c,$(BUILD_SPIKE)/%.o,$(SHARED_C))
SPIKE_ELFS   = $(patsubst %,$(BUILD_SPIKE)/%.elf,$(TEST_NAMES))

# Derived — RTL
RTL_BOOT   = $(BUILD_RTL)/boot.o
RTL_SHARED = $(patsubst $(COMMON)/%.c,$(BUILD_RTL)/%.o,$(SHARED_C))
RTL_ELFS   = $(patsubst %,$(BUILD_RTL)/%.elf,$(TEST_NAMES))
RTL_BINS   = $(patsubst %,$(BUILD_RTL)/%.bin,$(TEST_NAMES))
RTL_MEMH   = $(patsubst %,$(BUILD_RTL)/%.memh64,$(TEST_NAMES))

# ============================================================================

.PHONY: all spike rtl memh64 clean clean-spike clean-rtl \
        run run-all dumps dumps-spike dumps-rtl \
        $(patsubst %,run-%,$(TEST_NAMES)) \
        $(patsubst %,memh64-%,$(TEST_NAMES))

all: spike

# --- Spike ---

spike: $(SPIKE_ELFS)
	@echo ""; echo "=== Spike ELFs ==="
	@for f in $(SPIKE_ELFS); do $(SIZE) $$f; done

$(BUILD_SPIKE):
	mkdir -p $@

$(BUILD_SPIKE)/boot.o: $(BOOT_SRC) $(HEADERS) | $(BUILD_SPIKE)
	$(AS) $(ASFLAGS_SPIKE) -c $< -o $@

$(BUILD_SPIKE)/%.o: $(COMMON)/%.c $(HEADERS) | $(BUILD_SPIKE)
	$(CC) $(CFLAGS_SPIKE) -c $< -o $@

$(BUILD_SPIKE)/%.o: $(TESTS)/%.c $(HEADERS) | $(BUILD_SPIKE)
	$(CC) $(CFLAGS_SPIKE) -c $< -o $@

$(BUILD_SPIKE)/%.elf: $(BUILD_SPIKE)/%.o $(SPIKE_BOOT) $(SPIKE_SHARED) $(COMMON)/link.ld | $(BUILD_SPIKE)
	$(LD) $(LDFLAGS_SPIKE) $(SPIKE_BOOT) $(SPIKE_SHARED) $< -o $@ -lgcc
	@echo "  Built: $@"

# --- RTL ---

rtl: $(RTL_ELFS)
	@echo ""; echo "=== RTL ELFs ==="
	@for f in $(RTL_ELFS); do $(SIZE) $$f; done

$(BUILD_RTL):
	mkdir -p $@

$(BUILD_RTL)/boot.o: $(BOOT_SRC) $(HEADERS) | $(BUILD_RTL)
	$(AS) $(ASFLAGS_RTL) -c $< -o $@

$(BUILD_RTL)/%.o: $(COMMON)/%.c $(HEADERS) | $(BUILD_RTL)
	$(CC) $(CFLAGS_RTL) -c $< -o $@

$(BUILD_RTL)/%.o: $(TESTS)/%.c $(HEADERS) | $(BUILD_RTL)
	$(CC) $(CFLAGS_RTL) -c $< -o $@

$(BUILD_RTL)/%.elf: $(BUILD_RTL)/%.o $(RTL_BOOT) $(RTL_SHARED) $(COMMON)/link.ld | $(BUILD_RTL)
	$(LD) $(LDFLAGS_RTL) $(RTL_BOOT) $(RTL_SHARED) $< -o $@ -lgcc
	@echo "  Built: $@"

# --- bin / memh64 (for Verilog $readmemh) ---

$(BUILD_RTL)/%.bin: $(BUILD_RTL)/%.elf
	$(OBJCOPY) -O binary --set-start 0x80000000 $< $@

$(BUILD_RTL)/%.memh64: $(BUILD_RTL)/%.bin
	od -An -tx8 -w8 -v $< | \
	  sed 's/^ *//;s/ *$$//' | \
	  awk '{ s=$$1; printf "%s%s%s%s%s%s%s%s\n", \
	    substr(s,15,2), substr(s,13,2), substr(s,11,2), substr(s,9,2), \
	    substr(s,7,2),  substr(s,5,2),  substr(s,3,2),  substr(s,1,2) }' > $@
	@echo "  memh64: $@ ($(shell wc -l < $@) lines)"

memh64: $(RTL_MEMH)
	@echo ""; echo "=== memh64 files ready ==="

memh64-%: $(BUILD_RTL)/%.memh64
	@true

# --- Disassembly ---

$(BUILD_SPIKE)/%.dump: $(BUILD_SPIKE)/%.elf
	$(OBJDUMP) -D -S $< > $@

$(BUILD_RTL)/%.dump: $(BUILD_RTL)/%.elf
	$(OBJDUMP) -D -S $< > $@

dumps: dumps-spike
dumps-spike: $(patsubst %,$(BUILD_SPIKE)/%.dump,$(TEST_NAMES))
dumps-rtl:   $(patsubst %,$(BUILD_RTL)/%.dump,$(TEST_NAMES))

# --- Run on Spike ---

SPIKE_FLAGS = --isa=rv64gc -m0x80000000:0x200000 -p2

define run-test
	@echo ""
	@echo "======== Running $(1) ========"
	@$(SPIKE) $(SPIKE_FLAGS) $(BUILD_SPIKE)/$(1).elf ; \
	EXIT=$$? ; \
	if [ $$EXIT -eq 0 ]; then echo ">>> $(1): PASS" ; \
	else echo ">>> $(1): FAIL (exit code $$EXIT)" ; exit 1 ; fi
endef

run-p0:  $(BUILD_SPIKE)/p0_sanity.elf;       $(call run-test,p0_sanity)
run-p1a: $(BUILD_SPIKE)/p1a_mmu_enable.elf;  $(call run-test,p1a_mmu_enable)
run-p1b: $(BUILD_SPIKE)/p1b_fault_gen.elf;   $(call run-test,p1b_fault_gen)
run-p1c: $(BUILD_SPIKE)/p1c_fault_handle.elf;$(call run-test,p1c_fault_handle)
run-p1d: $(BUILD_SPIKE)/p1d_tlb_ptw.elf;     $(call run-test,p1d_tlb_ptw)
run-p2:  $(BUILD_SPIKE)/p2_interrupts.elf;   $(call run-test,p2_interrupts)
run-p3:  $(BUILD_SPIKE)/p3_atomics.elf;      $(call run-test,p3_atomics)
run-p4:  $(BUILD_SPIKE)/p4_dualcore.elf;     $(call run-test,p4_dualcore)

run run-all: spike
	$(call run-test,p0_sanity)
	$(call run-test,p1a_mmu_enable)
	$(call run-test,p1b_fault_gen)
	$(call run-test,p1c_fault_handle)
	$(call run-test,p1d_tlb_ptw)
	$(call run-test,p2_interrupts)
	$(call run-test,p3_atomics)
	$(call run-test,p4_dualcore)
	@echo ""
	@echo "============================================"
	@echo "  ALL PHASES PASSED"
	@echo "============================================"

# --- Clean ---

clean: clean-spike clean-rtl
clean-spike:
	rm -rf $(BUILD_SPIKE)
clean-rtl:
	rm -rf $(BUILD_RTL)

