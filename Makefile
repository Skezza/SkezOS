TARGET = i686-elf
CC     = gcc
AS     = as
LD     = ld
QEMU   ?= qemu-system-i386
GUI_BOOT ?= 0

# Flags for building a freestanding 32‑bit kernel.  We disable stack
# protector and all floating point support.  We also disable PIE and
# built‑ins so that no assumptions are made about the runtime
# environment.
CFLAGS = -ffreestanding -fno-pic -m32 -O2 -Wall -Wextra \
         -nostdlib -fno-builtin -fno-stack-protector -mno-sse \
         -mno-sse2 -mno-red-zone -mno-80387 \
         -DSKEZOS_GUI_BOOT=$(GUI_BOOT)
ASFLAGS = --32
LDFLAGS = -m elf_i386

# Build directories
ifeq ($(GUI_BOOT),1)
BUILD = build-gui
ISO   = skezos-gui.iso
else
BUILD = build
ISO   = skezos.iso
endif
SMOKE_LOG = $(BUILD)/qemu-smoke.log
LIFECYCLE_SMOKE_LOG = $(BUILD)/qemu-smoke-lifecycle.log
SMOKE_TIMEOUT_SECS ?= 18
SHELL_SMOKE_TIMEOUT_SECS ?= 35
RELIABILITY_SCENARIO_TIMEOUT_SECS ?= 70
RELIABILITY_SMOKE_STEP_SECS ?= 0.20
RELIABILITY_SMOKE_RUNNER_SETTLE_SECS ?= 1.50
RELIABILITY_SMOKE_DIAG_SETTLE_SECS ?= 1.00
RELIABILITY_REPLAY_RUNNER_SETTLE_SECS ?= 1.50
RELIABILITY_FUZZ_RUNNER_SETTLE_SECS ?= 2.25
NIGHTLY_GUI_TRIAGE_BOOT_WAIT_SECS ?= 16
NIGHTLY_GUI_TRIAGE_ARTIFACT_DIR ?= $(BUILD)/artifacts
GUI_VISUAL_BASELINE_BOOT_WAIT_SECS ?= 16
GUI_VISUAL_BASELINE_ARTIFACT_DIR ?= $(BUILD)/artifacts
GUI_VISUAL_BASELINE_HASH_FB_SHELL_V6 ?= 0x17AA9EDD
GUI_NAV_BOOT_WAIT_SECS ?= 16
GUI_NAV_ARTIFACT_DIR ?= $(BUILD)/artifacts
GUI_VISUAL_BASELINE_HASH_FB_SHELL_V6_NAV_TASK ?= 0x77AFF05E
GUI_VISUAL_BASELINE_HASH_FB_SHELL_V6_NAV_FOCUS ?= 0xBFC5F4C6
SMOKE_MARKER = SKEZOS_SMOKE_READY
RELIABILITY_JSON_VALIDATOR = ./scripts/validate_reliability_json.sh
RELIABILITY_JSON_REPORTER = ./scripts/reliability_json_report.sh
REPLAY_HASH_ALL_SEED1337 ?= 2002305826
REPLAY_HASH_QUICK_SEED1337 ?= 2923080070
GUI_STATE_HASH_FB_SHELL_V6 ?= 0xAAA213A9
PHASE4_REPEAT ?= 3
STORAGE_DISK_SIZE_MB ?= 16
STORAGE_RUN_DISK = $(BUILD)/storage-run.img
STORAGE_INTEGRITY_DISK = $(BUILD)/storage-integrity.img
STORAGE_PERSIST_DISK = $(BUILD)/storage-persist.img
STORAGE_REPLAY_DISK = $(BUILD)/storage-replay.img
SHELL_HISTORY_PERSIST_DISK = $(BUILD)/storage-shell-history-persist.img
SHELL_SCRIPT_CORE_DISK = $(BUILD)/storage-shell-script-core.img
USERLAND_BUILD = $(BUILD)/userland
INITRAMFS_STAGING = $(BUILD)/initramfs_root
INITRAMFS_TAR = $(BUILD)/initramfs_demo.tar
USERLAND_ASFLAGS = $(ASFLAGS) -I userland
USERLAND_CFLAGS = $(CFLAGS) -I userland
USERLAND_LDFLAGS = $(LDFLAGS) -nostdlib -N -e _start

SRCS  = $(filter-out kernel/initramfs_demo_blob.c,$(wildcard kernel/*.c))
OBJS  = $(patsubst kernel/%.c,$(BUILD)/%.o,$(SRCS))
INITRAMFS_BLOB_OBJ = $(BUILD)/initramfs_demo_blob.o

ASM_OBJS = $(BUILD)/idt_load.o $(BUILD)/sched_switch.o $(BUILD)/gdt_flush.o $(BUILD)/syscall_entry.o $(BUILD)/user_demo_blob.o

USERLAND_OBJS = \
	$(USERLAND_BUILD)/hello_slot0.o \
	$(USERLAND_BUILD)/hello_slot1.o \
	$(USERLAND_BUILD)/hello_slot2.o \
	$(USERLAND_BUILD)/hello_slot3.o \
	$(USERLAND_BUILD)/sh_slot4.o \
	$(USERLAND_BUILD)/cat_slot5.o \
	$(USERLAND_BUILD)/echo_slot6.o \
	$(USERLAND_BUILD)/readln_slot7.o \
	$(USERLAND_BUILD)/uptime_slot8.o \
	$(USERLAND_BUILD)/sleep_slot9.o \
	$(USERLAND_BUILD)/diag_slot10.o \
	$(USERLAND_BUILD)/fault_slot11.o \
	$(USERLAND_BUILD)/pwd_slot12.o \
	$(USERLAND_BUILD)/ls_slot13.o \
	$(USERLAND_BUILD)/busybox_slot14.o \
	$(USERLAND_BUILD)/reliability_runner_slot15.o \
	$(USERLAND_BUILD)/rm_slot16.o \
	$(USERLAND_BUILD)/forktest_slot17.o \
	$(USERLAND_BUILD)/sleep2_slot18.o \
	$(USERLAND_BUILD)/gui_console_slot19.o \
	$(USERLAND_BUILD)/gui_demo_slot20.o \
	$(USERLAND_BUILD)/gui_session_slot21.o

USERLAND_ELFS = \
	$(USERLAND_BUILD)/hello.elf \
	$(USERLAND_BUILD)/hello2.elf \
	$(USERLAND_BUILD)/hello3.elf \
	$(USERLAND_BUILD)/hello4.elf \
	$(USERLAND_BUILD)/sh.elf \
	$(USERLAND_BUILD)/cat.elf \
	$(USERLAND_BUILD)/echo.elf \
	$(USERLAND_BUILD)/readln.elf \
	$(USERLAND_BUILD)/uptime.elf \
	$(USERLAND_BUILD)/sleep.elf \
	$(USERLAND_BUILD)/diag.elf \
	$(USERLAND_BUILD)/fault.elf \
	$(USERLAND_BUILD)/pwd.elf \
	$(USERLAND_BUILD)/ls.elf \
	$(USERLAND_BUILD)/busybox.elf \
	$(USERLAND_BUILD)/reliability_runner.elf \
	$(USERLAND_BUILD)/rm.elf \
	$(USERLAND_BUILD)/forktest.elf \
	$(USERLAND_BUILD)/sleep2.elf \
	$(USERLAND_BUILD)/gui_console.elf \
	$(USERLAND_BUILD)/gui_demo.elf \
	$(USERLAND_BUILD)/gui_session.elf

.PHONY: all run gui-iso gui-run clean toolchain-check qemu-smoke qemu-smoke-userfault qemu-smoke-phase4 qemu-smoke-phase4-repeat qemu-smoke-lifecycle qemu-smoke-shell-core qemu-smoke-shell-script-core qemu-smoke-reliability qemu-smoke-reliability-replay qemu-smoke-reliability-fuzz-lite-matrix qemu-smoke-gui-session qemu-smoke-gui-fb-dump qemu-smoke-gui-visual-baseline qemu-smoke-gui-visual-baseline-refresh qemu-smoke-gui-nav qemu-smoke-storage-integrity qemu-smoke-storage-persist qemu-smoke-storage-replay qemu-smoke-shell-history-persist qemu-smoke-fork-cow qemu-smoke-fork-cow-stress qemu-smoke-fork-cow-pressure qemu-smoke-shell-bg-replay check check-pr check-release check-nightly

all: $(ISO)

$(BUILD)/%.o: kernel/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/multiboot2_header.o: boot/multiboot2_header.S
	@mkdir -p $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/loader.o: boot/loader.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/kernel.elf: $(BUILD)/multiboot2_header.o $(BUILD)/loader.o $(ASM_OBJS) $(OBJS) $(INITRAMFS_BLOB_OBJ)
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o $@ $^

$(BUILD)/initramfs_demo_blob.o: kernel/initramfs_demo_blob.c

$(USERLAND_BUILD)/%.o: userland/%.S userland/runtime.inc userland/syscall_abi.inc
	@mkdir -p $(USERLAND_BUILD)
	$(AS) $(USERLAND_ASFLAGS) $< -o $@

$(USERLAND_BUILD)/%.o: userland/%.c userland/userlib.h
	@mkdir -p $(USERLAND_BUILD)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(USERLAND_BUILD)/gui_console_slot19.o: userland/guilib.h
$(USERLAND_BUILD)/gui_demo_slot20.o: userland/guilib.h
$(USERLAND_BUILD)/gui_session_slot21.o: userland/guilib.h

$(USERLAND_BUILD)/hello.elf: $(USERLAND_BUILD)/hello_slot0.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01410000 -o $@ $<

$(USERLAND_BUILD)/hello2.elf: $(USERLAND_BUILD)/hello_slot1.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01422000 -o $@ $<

$(USERLAND_BUILD)/hello3.elf: $(USERLAND_BUILD)/hello_slot2.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01434000 -o $@ $<

$(USERLAND_BUILD)/hello4.elf: $(USERLAND_BUILD)/hello_slot3.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01445000 -o $@ $<

$(USERLAND_BUILD)/sh.elf: $(USERLAND_BUILD)/sh_slot4.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01456000 -o $@ $<

$(USERLAND_BUILD)/cat.elf: $(USERLAND_BUILD)/cat_slot5.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01467000 -o $@ $<

$(USERLAND_BUILD)/echo.elf: $(USERLAND_BUILD)/echo_slot6.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01478000 -o $@ $<

$(USERLAND_BUILD)/readln.elf: $(USERLAND_BUILD)/readln_slot7.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01489000 -o $@ $<

$(USERLAND_BUILD)/uptime.elf: $(USERLAND_BUILD)/uptime_slot8.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x0149A000 -o $@ $<

$(USERLAND_BUILD)/sleep.elf: $(USERLAND_BUILD)/sleep_slot9.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014AB000 -o $@ $<

$(USERLAND_BUILD)/diag.elf: $(USERLAND_BUILD)/diag_slot10.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014BC000 -o $@ $<

$(USERLAND_BUILD)/fault.elf: $(USERLAND_BUILD)/fault_slot11.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014CD000 -o $@ $<

$(USERLAND_BUILD)/pwd.elf: $(USERLAND_BUILD)/pwd_slot12.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014DE000 -o $@ $<

$(USERLAND_BUILD)/ls.elf: $(USERLAND_BUILD)/ls_slot13.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014EF000 -o $@ $<

$(USERLAND_BUILD)/busybox.elf: $(USERLAND_BUILD)/busybox_slot14.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01500000 -o $@ $<

$(USERLAND_BUILD)/reliability_runner.elf: $(USERLAND_BUILD)/reliability_runner_slot15.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01511000 -o $@ $<

$(USERLAND_BUILD)/rm.elf: $(USERLAND_BUILD)/rm_slot16.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01522000 -o $@ $<

$(USERLAND_BUILD)/forktest.elf: $(USERLAND_BUILD)/forktest_slot17.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01533000 -o $@ $<

$(USERLAND_BUILD)/sleep2.elf: $(USERLAND_BUILD)/sleep2_slot18.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01544000 -o $@ $<

$(USERLAND_BUILD)/gui_console.elf: $(USERLAND_BUILD)/gui_console_slot19.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01600000 -o $@ $<

$(USERLAND_BUILD)/gui_demo.elf: $(USERLAND_BUILD)/gui_demo_slot20.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01800000 -o $@ $<

$(USERLAND_BUILD)/gui_session.elf: $(USERLAND_BUILD)/gui_session_slot21.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01555000 -o $@ $<

$(INITRAMFS_STAGING)/.stamp: $(USERLAND_ELFS) userland/readme.txt
	@rm -rf $(INITRAMFS_STAGING)
	@mkdir -p $(INITRAMFS_STAGING)/bin
	cp $(USERLAND_BUILD)/hello.elf $(INITRAMFS_STAGING)/bin/hello.elf
	cp $(USERLAND_BUILD)/hello2.elf $(INITRAMFS_STAGING)/bin/hello2.elf
	cp $(USERLAND_BUILD)/hello3.elf $(INITRAMFS_STAGING)/bin/hello3.elf
	cp $(USERLAND_BUILD)/hello4.elf $(INITRAMFS_STAGING)/bin/hello4.elf
	cp $(USERLAND_BUILD)/sh.elf $(INITRAMFS_STAGING)/bin/sh.elf
	cp $(USERLAND_BUILD)/cat.elf $(INITRAMFS_STAGING)/bin/cat.elf
	cp $(USERLAND_BUILD)/echo.elf $(INITRAMFS_STAGING)/bin/echo.elf
	cp $(USERLAND_BUILD)/readln.elf $(INITRAMFS_STAGING)/bin/readln.elf
	cp $(USERLAND_BUILD)/uptime.elf $(INITRAMFS_STAGING)/bin/uptime.elf
	cp $(USERLAND_BUILD)/sleep.elf $(INITRAMFS_STAGING)/bin/sleep.elf
	cp $(USERLAND_BUILD)/diag.elf $(INITRAMFS_STAGING)/bin/diag.elf
	cp $(USERLAND_BUILD)/fault.elf $(INITRAMFS_STAGING)/bin/fault.elf
	cp $(USERLAND_BUILD)/pwd.elf $(INITRAMFS_STAGING)/bin/pwd.elf
	cp $(USERLAND_BUILD)/ls.elf $(INITRAMFS_STAGING)/bin/ls.elf
	cp $(USERLAND_BUILD)/busybox.elf $(INITRAMFS_STAGING)/bin/busybox.elf
	cp $(USERLAND_BUILD)/reliability_runner.elf $(INITRAMFS_STAGING)/bin/reliability_runner.elf
	cp $(USERLAND_BUILD)/rm.elf $(INITRAMFS_STAGING)/bin/rm.elf
	cp $(USERLAND_BUILD)/forktest.elf $(INITRAMFS_STAGING)/bin/forktest.elf
	cp $(USERLAND_BUILD)/sleep2.elf $(INITRAMFS_STAGING)/bin/sleep2.elf
	cp $(USERLAND_BUILD)/gui_console.elf $(INITRAMFS_STAGING)/bin/gui_console.elf
	cp $(USERLAND_BUILD)/gui_demo.elf $(INITRAMFS_STAGING)/bin/gui_demo.elf
	cp $(USERLAND_BUILD)/gui_session.elf $(INITRAMFS_STAGING)/bin/gui_session.elf
	cp userland/readme.txt $(INITRAMFS_STAGING)/bin/readme.txt
	@touch $@

$(INITRAMFS_TAR): $(INITRAMFS_STAGING)/.stamp
	cd $(INITRAMFS_STAGING) && tar --format=ustar --owner=0 --group=0 --numeric-owner -cf ../$(notdir $(INITRAMFS_TAR)) bin

kernel/initramfs_demo_blob.c: $(INITRAMFS_TAR) scripts/generate_initramfs_blob.sh kernel/initramfs_demo_blob.h
	./scripts/generate_initramfs_blob.sh $(INITRAMFS_TAR) $@

$(ISO): $(BUILD)/kernel.elf iso/boot/grub/grub.cfg
	@mkdir -p iso/boot
	cp $(BUILD)/kernel.elf iso/boot/kernel.elf
	grub-mkrescue -o $(ISO) iso > /dev/null 2>&1

run: $(ISO) $(STORAGE_RUN_DISK)
	# Runs QEMU with graphical UI (GTK) and forward COM1 to this terminal.
	# Using "mon:stdio" keeps stdin input reliable when piped input at launch.
	$(QEMU) \
		-cdrom $(ISO) \
		-drive file=$(STORAGE_RUN_DISK),format=raw,if=ide,index=0,media=disk \
		-display gtk \
		-serial mon:stdio \
		-monitor none \

gui-iso:
	$(MAKE) GUI_BOOT=1 skezos-gui.iso

gui-run:
	$(MAKE) GUI_BOOT=1 run

toolchain-check:
	@missing=0; \
	for tool in $(CC) $(AS) $(LD) grub-mkrescue xorriso $(QEMU) timeout tar od dd; do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "Missing required tool: $$tool"; \
			missing=1; \
		fi; \
	done; \
	if [ $$missing -ne 0 ]; then \
		echo "Install missing tools and rerun."; \
		exit 1; \
	fi

$(BUILD)/storage-%.img:
	@mkdir -p $(BUILD)
	@rm -f $@
	@dd if=/dev/zero of=$@ bs=1M count=$(STORAGE_DISK_SIZE_MB) status=none

qemu-smoke: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(SMOKE_LOG)
	@echo "[qemu-smoke] Booting headless VM for up to $(SMOKE_TIMEOUT_SECS)s..."
	@rc=0; \
	timeout -s INT -k 2s $(RELIABILITY_SCENARIO_TIMEOUT_SECS)s $(QEMU) \
		-cdrom $(ISO) \
		-display none \
		-serial file:$(SMOKE_LOG) \
		-monitor none \
		-no-reboot \
		-no-shutdown >/dev/null 2>&1 || rc=$$?; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
		echo "[qemu-smoke] QEMU failed (exit $$rc)."; \
		exit $$rc; \
	fi; \
	if ! grep -Fq "$(SMOKE_MARKER)" $(SMOKE_LOG); then \
		echo "[qemu-smoke] Missing readiness marker: $(SMOKE_MARKER)"; \
		if [ -f $(SMOKE_LOG) ]; then tail -n 40 $(SMOKE_LOG); fi; \
		exit 1; \
	fi; \
	echo "[qemu-smoke] PASS"

qemu-smoke-userfault: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-userfault.log
	@echo "[qemu-smoke-userfault] Booting headless VM with scripted fault trigger..."
	@rc=0; \
	{ sleep 9; \
		printf 'fault\n'; \
		sleep 0.25; \
		printf 'exit\n'; \
	} | \
	timeout -s INT -k 2s $(RELIABILITY_SCENARIO_TIMEOUT_SECS)s $(QEMU) \
		-cdrom $(ISO) \
		-display none \
		-serial mon:stdio \
		-monitor none \
		-no-reboot \
		-no-shutdown > $(BUILD)/qemu-smoke-userfault.log 2>&1 || rc=$$?; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
		echo "[qemu-smoke-userfault] QEMU failed (exit $$rc)."; \
		exit $$rc; \
	fi
	@if ! grep -Fq "$(SMOKE_MARKER)" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing readiness marker: $(SMOKE_MARKER)"; \
		tail -n 120 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	$(call ASSERT_GUI_HASH_IF_FB,qemu-smoke-userfault,$(BUILD)/qemu-smoke-userfault.log)
	@if ! grep -Fq "fault: triggering user page fault" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing fault trigger output"; \
		tail -n 120 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@if ! grep -Fq "user page fault:" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing user page fault log"; \
		tail -n 120 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@if ! grep -Fq "fault recovery: terminating user task pid=" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing task termination log"; \
		tail -n 120 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@if ! grep -Fq "task=user-shell waited_pid=" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing shell waitpid recovery log"; \
		tail -n 160 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-userfault] PASS"

qemu-smoke-gui-session:
	@$(MAKE) GUI_BOOT=1 qemu-smoke
	@if ! grep -Fq "GUI: SESSION READY" build-gui/qemu-smoke.log; then \
		echo "[qemu-smoke-gui-session] Missing GUI session marker"; \
		tail -n 160 build-gui/qemu-smoke.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-gui-session] PASS"

qemu-smoke-phase4:
	@$(MAKE) qemu-smoke SMOKE_LOG=$(BUILD)/qemu-smoke-phase4.log
	@if ! grep -Fq "tarfs: self-check pass (/bin/readme.txt)" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing tarfs self-check log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_read stdin -> 0 bytes" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing SYS_READ smoke log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_spawn /bin/hello3.elf ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawn hello3 success log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_spawn /bin/hello4.elf ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawn hello4 success log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_spawn /bin/hello3.elf reuse ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawn-slot reuse success log"; \
		tail -n 160 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-c: spawned via sys_spawn" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawned child C log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'elf-c: spawned via sys_spawn' $(BUILD)/qemu-smoke-phase4.log)" -lt 2 ]; then \
		echo "[qemu-smoke-phase4] Expected elf-c to spawn at least twice (slot reuse)"; \
		tail -n 180 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-d: spawned via sys_spawn" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawned child D log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sys_open:" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing SYS_OPEN kernel log"; \
		tail -n 140 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-d: readme: SkezOS tarfs demo" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing open/read file-content smoke log"; \
		tail -n 140 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-d: close fd ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing SYS_CLOSE smoke log"; \
		tail -n 140 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'usermode: released spawn slot task=user-spawn-c' $(BUILD)/qemu-smoke-phase4.log)" -lt 2 ]; then \
		echo "[qemu-smoke-phase4] Expected user-spawn-c slot release at least twice"; \
		tail -n 200 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'syscall: stdio fd bind pid=5 task=user-demo fd=1 path=/dev/console' $(BUILD)/qemu-smoke-phase4.log)" -ne 1 ]; then \
		echo "[qemu-smoke-phase4] Unexpected user-demo stdio bind count (fd cache regression?)"; \
		tail -n 160 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-phase4] PASS"

qemu-smoke-phase4-repeat:
	@i=1; \
	while [ $$i -le $(PHASE4_REPEAT) ]; do \
		echo "[qemu-smoke-phase4-repeat] Run $$i/$(PHASE4_REPEAT)"; \
		$(MAKE) qemu-smoke-phase4 || exit $$?; \
		i=$$((i + 1)); \
	done; \
	echo "[qemu-smoke-phase4-repeat] PASS ($(PHASE4_REPEAT) runs)"

qemu-smoke-lifecycle: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(LIFECYCLE_SMOKE_LOG)
	@echo "[qemu-smoke-lifecycle] Booting headless VM with scripted lifecycle input..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-lifecycle,$(LIFECYCLE_SMOKE_LOG),$(LIFECYCLE_SMOKE_SCRIPT))
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-lifecycle,$(LIFECYCLE_SMOKE_LOG))
	$(call ASSERT_GUI_HASH_IF_FB,qemu-smoke-lifecycle,$(LIFECYCLE_SMOKE_LOG))
	@if ! grep -Fq "SMOKE_LIFECYCLE_SPAWN_HELLO3_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing spawn hello3 marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_SPAWN_HELLO4_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing spawn hello4 marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_SPAWN_HELLO3_REUSE_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing spawn reuse marker"; \
		tail -n 200 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_WAIT_HELLO3_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing waitpid hello3 marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_WAIT_HELLO4_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing waitpid hello4 marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_WAIT_HELLO3_REUSE_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing waitpid reuse marker"; \
		tail -n 200 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_FD_OPEN_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing fd open marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_FD_READ_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing fd read marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_FD_CLOSE_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing fd close marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'SMOKE_LIFECYCLE_WAIT_REAP' $(LIFECYCLE_SMOKE_LOG))" -lt 3 ]; then \
		echo "[qemu-smoke-lifecycle] Expected at least three wait/reap kernel markers"; \
		tail -n 220 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'SMOKE_LIFECYCLE_ELF_SCRATCH_RECLAIM' $(LIFECYCLE_SMOKE_LOG))" -lt 5 ]; then \
		echo "[qemu-smoke-lifecycle] Expected scratch reclamation markers for all ELF loads"; \
		tail -n 220 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_STACK_RECLAIM" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing waited-child stack reclaim marker"; \
		tail -n 220 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_STACK_DEFERRED live_large=" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing final deferred stack reclaim watermark"; \
		tail -n 240 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing shell exit line"; \
		tail -n 200 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@echo "[qemu-smoke-lifecycle] PASS"

define LIFECYCLE_SMOKE_SCRIPT
printf 'cd /bin\n'; \
sleep 0.25; \
printf './hello2.elf\n'; \
sleep 0.25; \
printf '\n'; \
sleep 4.00; \
printf 'exit\n';
endef

define SHELL_CORE_SMOKE_SCRIPT
printf 'pwd\n'; \
sleep 0.25; \
printf 'cd /bin\n'; \
sleep 0.25; \
printf 'pw\t\n'; \
sleep 0.25; \
printf 'ls\n'; \
sleep 0.25; \
printf './busybox echo via busybox\n'; \
sleep 0.25; \
printf './busybox pwd\n'; \
sleep 0.25; \
printf 'readlq\177n\n'; \
sleep 0.75; \
printf 'stdin handoff worzz\b\bks\n'; \
sleep 0.25; \
printf 'ps\n'; \
sleep 0.25; \
printf 'timeline 5\n'; \
sleep 0.20; \
printf 'set theme ansi\n'; \
sleep 0.20; \
printf 'set theme plain\n'; \
sleep 0.20; \
printf 'replay 3\n'; \
sleep 0.20; \
printf 'bootshow on\n'; \
sleep 0.20; \
printf 'bootshow\n'; \
sleep 0.20; \
printf 'bootshow run\n'; \
sleep 0.20; \
printf 'bootshow off\n'; \
sleep 0.20; \
printf 'hud\n'; \
sleep 0.20; \
printf 'set hud on\n'; \
sleep 0.20; \
printf 'echo hud-live\n'; \
sleep 0.20; \
printf 'set hud off\n'; \
sleep 0.20; \
printf 'echo shell core interactive echo\n'; \
sleep 0.25; \
printf '\022\n'; \
sleep 0.25; \
printf 'echo recall-target\n'; \
sleep 0.25; \
printf '!!\n'; \
sleep 0.25; \
printf '!echo\n'; \
sleep 0.25; \
printf '!?target\n'; \
sleep 0.25; \
printf '^target^target2^\n'; \
sleep 0.25; \
printf 'echo pref-one\n'; \
sleep 0.25; \
printf 'echo pref-two\n'; \
sleep 0.25; \
printf 'echo pref\033\n'; \
sleep 0.25; \
printf 'history clear\n'; \
sleep 0.25; \
printf 'echo run-target\n'; \
sleep 0.25; \
printf 'echo run-other\n'; \
sleep 0.25; \
printf 'history run 1\n'; \
sleep 0.25; \
printf 'echo nope\025echo ctrlu-ok\n'; \
sleep 0.25; \
printf 'echo alpha beta\027gamma\n'; \
sleep 0.25; \
printf 'his\t\n'; \
sleep 0.25; \
printf 'cat readme.txt\n'; \
sleep 0.25; \
printf 'echo "spaced arg"\n'; \
sleep 0.25; \
printf 'echo escaped\\ space\n'; \
sleep 0.25; \
printf 'echo "x|y"\n'; \
sleep 0.25; \
printf 'echo "a>b"\n'; \
sleep 0.25; \
printf 'echo quoted-redir > "/qfile.txt"\n'; \
sleep 0.25; \
printf 'cat "/qfile.txt"\n'; \
sleep 0.25; \
printf 'rm "/qfile.txt"\n'; \
sleep 0.25; \
	printf 'sleep 80 | cat &\n'; \
	sleep 0.25; \
	printf 'echo bg-foreground\n'; \
	sleep 0.20; \
	printf 'jobs\n'; \
	sleep 0.20; \
	printf 'fg\n'; \
	sleep 0.20; \
	printf 'echo fg-active-done\n'; \
	sleep 0.20; \
	printf 'sleep 80 &\n'; \
	sleep 0.10; \
	printf 'wait\n'; \
	sleep 0.20; \
	printf 'echo wait-active-done\n'; \
	sleep 0.20; \
	printf 'wait\n'; \
	sleep 0.20; \
	printf 'fg\n'; \
	sleep 0.20; \
	printf 'fg x\n'; \
	sleep 0.20; \
	printf 'fg 99\n'; \
	sleep 0.20; \
	printf 'sleep 120 &\n'; \
	sleep 0.10; \
	printf 'sleep2 120 &\n'; \
	sleep 0.10; \
	printf 'sleep 120 &\n'; \
	sleep 0.10; \
	printf 'wait\n'; \
	sleep 0.20; \
	printf 'echo bg-overflow-checked\n'; \
	sleep 0.25; \
	printf 'missing\n'; \
	sleep 0.25; \
	printf 'echo recovery-ok-1\n'; \
	sleep 0.20; \
	printf 'echo recovery-ok-2\n'; \
	sleep 0.20; \
	printf 'echo recovery-ok-3\n'; \
	sleep 0.20; \
	printf 'echo recovery-ok-4\n'; \
	sleep 0.20; \
	printf 'exit\n';
endef

define RELIABILITY_SMOKE_SCRIPT
printf 'cd /\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'echo hello > test.txt\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'cat /test.txt\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'cd /bin\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'uptime\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'sleep 2\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'echo pipeline smoke | cat\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'cat < readme.txt | ./busybox cat\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'echo drop-me > /dev/null\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'echo reliability-v1 > /reliability.txt\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'echo reliability-v2 >> /reliability.txt\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'cat /reliability.txt\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'echo should-fail > /bin/readme.txt\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf '/bin/pwd | cat\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'pwd | cat\n'; \
sleep $(RELIABILITY_SMOKE_STEP_SECS); \
printf 'reliability_runner --seed=1337 --script=all\n'; \
sleep $(RELIABILITY_SMOKE_RUNNER_SETTLE_SECS); \
printf 'diag\n'; \
sleep $(RELIABILITY_SMOKE_DIAG_SETTLE_SECS); \
printf 'exit\n';
endef

define RUN_SCRIPTED_SHELL_SMOKE
@rc=0; \
{ sleep 12; \
$(3) \
} | \
timeout -s INT -k 2s $(SHELL_SMOKE_TIMEOUT_SECS)s $(QEMU) \
	-cdrom $(ISO) \
	-display none \
	-serial mon:stdio \
	-monitor none \
	-no-reboot \
	-no-shutdown > $(2) 2>&1 || rc=$$?; \
if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
	echo "[$(1)] QEMU failed (exit $$rc)."; \
	exit $$rc; \
fi
endef

define RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK
@rc=0; \
{ sleep 12; \
$(4) \
} | \
timeout -s INT -k 2s $(SHELL_SMOKE_TIMEOUT_SECS)s $(QEMU) \
	-cdrom $(ISO) \
	-drive file=$(3),format=raw,if=ide,index=0,media=disk \
	-display none \
	-serial mon:stdio \
	-monitor none \
	-no-reboot \
	-no-shutdown > $(2) 2>&1 || rc=$$?; \
if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
	echo "[$(1)] QEMU failed (exit $$rc)."; \
	exit $$rc; \
fi
endef

define ASSERT_SHELL_BOOT_READY
@if ! grep -Fq "$(SMOKE_MARKER)" $(2); then \
	echo "[$(1)] Missing readiness marker: $(SMOKE_MARKER)"; \
	tail -n 220 $(2); \
	exit 1; \
fi
@if ! grep -Fq "SkezOS shell ready" $(2); then \
	echo "[$(1)] Missing shell bootstrap banner"; \
	tail -n 220 $(2); \
	exit 1; \
fi
endef

define ASSERT_GUI_HASH_IF_FB
@if grep -Fq "display: framebuffer console active" $(2); then \
	if ! grep -Fq "display: gui_state_hash=$(GUI_STATE_HASH_FB_SHELL_V6) profile=fb-shell-v6" $(2); then \
		echo "[$(1)] Missing or changed framebuffer GUI state hash"; \
		tail -n 220 $(2); \
		exit 1; \
	fi; \
fi
endef

qemu-smoke-shell-core: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-shell-core.log
	@echo "[qemu-smoke-shell-core] Booting headless VM with scripted shell input..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-shell-core,$(BUILD)/qemu-smoke-shell-core.log,$(SHELL_CORE_SMOKE_SCRIPT))
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-shell-core,$(BUILD)/qemu-smoke-shell-core.log)
	$(call ASSERT_GUI_HASH_IF_FB,qemu-smoke-shell-core,$(BUILD)/qemu-smoke-shell-core.log)
	@if ! awk 'BEGIN{in_shell=0;bad=0} /SkezOS shell ready/{in_shell=1} /sh: exit/{in_shell=0} in_shell && /sched demo:/{bad=1} END{exit bad ? 1 : 0}' $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Unexpected worker demo log spam while shell was active"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fo 'sh> ' $(BUILD)/qemu-smoke-shell-core.log | wc -l)" -lt 11 ]; then \
		echo "[qemu-smoke-shell-core] Missing repeated shell prompts"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "/"; then \
		echo "[qemu-smoke-shell-core] Missing root pwd output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "/bin"; then \
		echo "[qemu-smoke-shell-core] Missing changed pwd output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "busybox.elf" $(BUILD)/qemu-smoke-shell-core.log || ! grep -Fq "readme.txt" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing ls directory output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'via busybox' $(BUILD)/qemu-smoke-shell-core.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing busybox multicall output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxc '/bin')" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing inherited child cwd output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "readln: stdin handoff works" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing foreground stdin handoff output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "PID  PPID  MODE" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing ps task snapshot output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "timeline: seq=" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing timeline builtin output"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "set: theme=ansi" $(BUILD)/qemu-smoke-shell-core.log || \
		! grep -Fq "set: theme=plain" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing set theme toggle output"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "replay: seq=" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing replay builtin output"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fq "bootshow: on"; then \
		echo "[qemu-smoke-shell-core] Missing bootshow enable output"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fq "bootshow: showcase"; then \
		echo "[qemu-smoke-shell-core] Missing bootshow showcase output"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fq "bootshow: off"; then \
		echo "[qemu-smoke-shell-core] Missing bootshow disable output"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "set: hud=on" $(BUILD)/qemu-smoke-shell-core.log || \
		! grep -Fq "set: hud=off" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing set hud toggle output"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'hud: jobs=' $(BUILD)/qemu-smoke-shell-core.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing hud status line output"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "hud-live"; then \
		echo "[qemu-smoke-shell-core] Missing hud-on live marker output"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'shell core interactive echo' $(BUILD)/qemu-smoke-shell-core.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing echo output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "spaced arg"; then \
		echo "[qemu-smoke-shell-core] Missing quoted-arg whitespace output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "escaped space"; then \
		echo "[qemu-smoke-shell-core] Missing escaped-space argv output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "x|y"; then \
		echo "[qemu-smoke-shell-core] Missing quoted pipe-literal output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "a>b"; then \
		echo "[qemu-smoke-shell-core] Missing quoted redirect-literal output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "quoted-redir"; then \
		echo "[qemu-smoke-shell-core] Missing quoted redirect-path output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "bg-foreground"; then \
		echo "[qemu-smoke-shell-core] Missing foreground responsiveness marker after bg launch"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "jobs: id=" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing jobs builtin listing output"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "fg: done job=" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing fg builtin completion output"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "fg-active-done"; then \
		echo "[qemu-smoke-shell-core] Missing fg completion marker"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "wait-active-done"; then \
		echo "[qemu-smoke-shell-core] Missing blocking wait completion marker"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'wait: no background jobs' $(BUILD)/qemu-smoke-shell-core.log)" -lt 1 ]; then \
		echo "[qemu-smoke-shell-core] Missing wait no-job marker"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "fg: no background jobs" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing fg no-job marker"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "fg: usage: fg [job_id]" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing fg usage marker for invalid arguments"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "fg: job not found" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing fg not-found marker for unknown job id"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "bg: started job=" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing background launch marker"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "bg: done job=" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing background completion marker"; \
		tail -n 240 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "bg: overflow cmd=" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing background overflow marker"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "bg-overflow-checked"; then \
		echo "[qemu-smoke-shell-core] Missing background overflow completion marker"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "search: * -> echo shell core interactive echo" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing Ctrl+R reverse-search hint output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "history: !! -> echo recall-target" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing history event recall hint output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "history: !echo -> echo recall-target" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing history prefix-recall hint output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "history: !?target -> echo recall-target" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing history contains-recall hint output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "history: ^target^target2^ -> echo recall-target2" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing history substitution hint output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'recall-target' $(BUILD)/qemu-smoke-shell-core.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing history event recall command output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "recall-target2"; then \
		echo "[qemu-smoke-shell-core] Missing history substitution command output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'pref-two' $(BUILD)/qemu-smoke-shell-core.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing prefix-filter history browse replay output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "history: run 1 -> echo run-target" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing history run replay hint output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxc 'run-target')" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing history run replay command output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "ctrlu-ok"; then \
		echo "[qemu-smoke-shell-core] Missing Ctrl+U line-kill edit output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "alpha gamma"; then \
		echo "[qemu-smoke-shell-core] Missing Ctrl+W word-kill edit output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "SkezOS tarfs demo" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing cat output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'run: launch failed' $(BUILD)/qemu-smoke-shell-core.log)" -lt 1 ]; then \
		echo "[qemu-smoke-shell-core] Missing launch failure marker"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "open failed path=/bin/missing.elf" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing unknown-command failure path marker"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Eq '^[[:space:]]*[0-9]+[[:space:]]+history$$'; then \
		echo "[qemu-smoke-shell-core] Missing history builtin output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sys_waitpid: parent_pid=" $(BUILD)/qemu-smoke-shell-core.log || ! grep -Fq "task=user-shell waited_pid=" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing user-shell waitpid log"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'display: cmd_latency tag=' $(BUILD)/qemu-smoke-shell-core.log)" -lt 3 ]; then \
		echo "[qemu-smoke-shell-core] Missing display command-latency telemetry logs"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'display: cmd_latency_budget tag=' $(BUILD)/qemu-smoke-shell-core.log)" -lt 3 ]; then \
		echo "[qemu-smoke-shell-core] Missing display latency-budget telemetry logs"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'display: cmd_health tag=' $(BUILD)/qemu-smoke-shell-core.log)" -lt 3 ]; then \
		echo "[qemu-smoke-shell-core] Missing display command-health telemetry logs"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'display: cmd_health_state from=' $(BUILD)/qemu-smoke-shell-core.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing display command-health state transitions"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'display: cmd_health_window tag=' $(BUILD)/qemu-smoke-shell-core.log)" -lt 3 ]; then \
		echo "[qemu-smoke-shell-core] Missing display command-health rolling-window logs"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'display: cmd_health_recovery from=' $(BUILD)/qemu-smoke-shell-core.log)" -lt 1 ]; then \
		echo "[qemu-smoke-shell-core] Missing display command-health recovery logs"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing shell exit line"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-shell-core] PASS (interactive shell)"

qemu-smoke-shell-script-core: $(ISO) $(SHELL_SCRIPT_CORE_DISK)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-shell-script-core.boot1.log $(BUILD)/qemu-smoke-shell-script-core.boot2.log
	@echo "[qemu-smoke-shell-script-core] Boot #1 creates /persist scripts, Boot #2 validates scripting features..."
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-shell-script-core.boot1,$(BUILD)/qemu-smoke-shell-script-core.boot1.log,$(SHELL_SCRIPT_CORE_DISK),printf 'cd /persist\n'; \
sleep 0.25; \
printf "echo 'A=rcv; echo rc-auto; echo rc-\\$$A; echo rc-left && echo rc-right' > rc.sh\n"; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-shell-script-core.boot1,$(BUILD)/qemu-smoke-shell-script-core.boot1.log)
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-shell-script-core.boot2,$(BUILD)/qemu-smoke-shell-script-core.boot2.log,$(SHELL_SCRIPT_CORE_DISK),printf 'cd /persist\n'; \
sleep 0.25; \
printf 'A=hello\n'; \
sleep 0.25; \
printf 'echo $$A\n'; \
sleep 0.25; \
printf 'echo and-left && echo and-right\n'; \
sleep 0.25; \
printf 'missingcmd && echo and-should-not-run\n'; \
sleep 0.25; \
printf 'missingcmd || echo or-fallback\n'; \
sleep 0.25; \
printf 'echo seq-one; echo seq-two\n'; \
sleep 0.25; \
printf 'export B=world\n'; \
sleep 0.25; \
printf 'echo $$B\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-shell-script-core.boot2,$(BUILD)/qemu-smoke-shell-script-core.boot2.log)
	@if ! grep -Fq "rc: running /persist/rc.sh" $(BUILD)/qemu-smoke-shell-script-core.boot2.log; then \
		echo "[qemu-smoke-shell-script-core] Missing rc boot hook marker"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-script-core.boot2.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-script-core.boot2.log | grep -Fxq "rc-auto"; then \
		echo "[qemu-smoke-shell-script-core] Missing rc script output"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-script-core.boot2.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-script-core.boot2.log | grep -Fxq "and-right"; then \
		echo "[qemu-smoke-shell-script-core] Missing && chain success output"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-script-core.boot2.log; \
		exit 1; \
	fi
	@if tr -d '\r' < $(BUILD)/qemu-smoke-shell-script-core.boot2.log | grep -Fxq "and-should-not-run"; then \
		echo "[qemu-smoke-shell-script-core] && chain unexpectedly executed rhs after failure"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-script-core.boot2.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-script-core.boot2.log | grep -Fxq "or-fallback"; then \
		echo "[qemu-smoke-shell-script-core] Missing || fallback output"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-script-core.boot2.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-script-core.boot2.log | grep -Fxq "seq-one" || \
		! tr -d '\r' < $(BUILD)/qemu-smoke-shell-script-core.boot2.log | grep -Fxq "seq-two"; then \
		echo "[qemu-smoke-shell-script-core] Missing ';' chain output"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-script-core.boot2.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-script-core.boot2.log | grep -Fxq "hello"; then \
		echo "[qemu-smoke-shell-script-core] Missing assignment expansion output"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-script-core.boot2.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-script-core.boot2.log | grep -Fxq "world"; then \
		echo "[qemu-smoke-shell-script-core] Missing export variable expansion output"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-script-core.boot2.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(BUILD)/qemu-smoke-shell-script-core.boot2.log; then \
		echo "[qemu-smoke-shell-script-core] Missing shell exit marker"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-script-core.boot2.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-shell-script-core] PASS"


qemu-smoke-reliability: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-reliability.log
	@echo "[qemu-smoke-reliability] Booting headless VM with scripted reliability input..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-reliability,$(BUILD)/qemu-smoke-reliability.log,$(RELIABILITY_SMOKE_SCRIPT))
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-reliability,$(BUILD)/qemu-smoke-reliability.log)
	$(call ASSERT_GUI_HASH_IF_FB,qemu-smoke-reliability,$(BUILD)/qemu-smoke-reliability.log)
	@$(RELIABILITY_JSON_VALIDATOR) base $(BUILD)/qemu-smoke-reliability.log
	@$(RELIABILITY_JSON_REPORTER) base $(BUILD)/qemu-smoke-reliability.log > $(BUILD)/qemu-smoke-reliability.report
	@if ! grep -Fq "uptime: " $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing uptime output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sleep: requested=2 observed=" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing sleep output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "pipeline smoke" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing basic pipe output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'SkezOS tarfs demo' $(BUILD)/qemu-smoke-reliability.log)" -lt 1 ]; then \
		echo "[qemu-smoke-reliability] Missing redirected pipeline cat output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-reliability.log | grep -Fxq "hello"; then \
		echo "[qemu-smoke-reliability] Missing root redirect round-trip output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-reliability.log | grep -Fxq "reliability-v1"; then \
		echo "[qemu-smoke-reliability] Missing redirect create output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-reliability.log | grep -Fxq "reliability-v2"; then \
		echo "[qemu-smoke-reliability] Missing redirect append output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'run: redirect open failed' $(BUILD)/qemu-smoke-reliability.log)" -ne 1 ]; then \
		echo "[qemu-smoke-reliability] Unexpected redirect failure count"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "run: builtin does not support pipes/redirection" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing builtin pipeline rejection output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq '{"type":"meta","seed":1337,"script":"all"}' $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing reliability_runner meta JSON"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq '{"type":"case","name":"proc_redirect_reap","ok":true,"rc":0}' $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing reliability_runner proc_redirect_reap JSON"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq '{"type":"case","name":"cwd_path_drift","ok":true,"rc":0}' $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing reliability_runner cwd_path_drift JSON"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq '{"type":"case","name":"pipe_close_order_parent_child","ok":true,"rc":0}' $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing reliability_runner pipe_close_order_parent_child JSON"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq '{"type":"summary","total":3,"failures":0,"ok":true}' $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing reliability_runner summary JSON"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq '{"type":"event","seq":1,' $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing reliability_runner event trace JSON"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: pipe bad ptr ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag pipe bad-pointer check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: dup invalid fd ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag dup invalid-fd check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: dup2 invalid target fd ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag dup2 invalid-target check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: spawn_ex bad flags ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag spawn_ex bad-flags check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Eq "diag: spawn missing path[^[:cntrl:]]*ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag spawn missing-path check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: spawn/wait hello ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag spawn/wait hello check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: waitpid reap again ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag waitpid reap-again check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: waitpid non-child ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag waitpid non-child check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: read invalid fd ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag read invalid-fd check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: close invalid fd ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag close invalid-fd check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: open missing path ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag open missing-path check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: fd open/read/close flow ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag open/read/close flow check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: pipe eof after writer close ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag pipe EOF check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: pipe broken write ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag broken-pipe check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: time_info bad ptr ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag time_info bad-pointer check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: time_info valid ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag time_info valid check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: waitpid nohang pending ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag waitpid nohang pending check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: waitpid nohang no-child ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag waitpid nohang no-child check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: task_snapshot bad ptr ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag task_snapshot bad-pointer check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: task_snapshot zero cap ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag task_snapshot zero-cap check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: task_snapshot cap one ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag task_snapshot cap-one check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: list_dir bad req ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag list_dir bad-request check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: list_dir root zero cap ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag list_dir root zero-cap check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: list_dir root cap one ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag list_dir root cap-one check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: getcwd bad ptr ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag getcwd bad-pointer check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: getcwd zero len ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag getcwd zero-length check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: chdir file notsup ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag chdir file-not-supported check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: cwd root roundtrip ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag cwd round-trip check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: PASS" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag syscall self-check output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing shell exit line"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-reliability] PASS"

qemu-smoke-reliability-replay: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-reliability-replay.log
	@echo "[qemu-smoke-reliability-replay] Booting headless VM with replay profiles..."
	@rc=0; \
	{ sleep 12; \
		printf 'cd /bin\n'; \
		sleep $(RELIABILITY_SMOKE_STEP_SECS); \
		printf 'reliability_runner --replay=all_seed1337\n'; \
		sleep $(RELIABILITY_REPLAY_RUNNER_SETTLE_SECS); \
		printf 'reliability_runner --replay=quick_seed1337\n'; \
		sleep $(RELIABILITY_REPLAY_RUNNER_SETTLE_SECS); \
		printf 'exit\n'; \
	} | \
	timeout -s INT -k 2s $(SHELL_SMOKE_TIMEOUT_SECS)s $(QEMU) \
		-cdrom $(ISO) \
		-display none \
		-serial mon:stdio \
		-monitor none \
		-no-reboot \
		-no-shutdown > $(BUILD)/qemu-smoke-reliability-replay.log 2>&1 || rc=$$?; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
		echo "[qemu-smoke-reliability-replay] QEMU failed (exit $$rc)."; \
		exit $$rc; \
	fi
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-reliability-replay,$(BUILD)/qemu-smoke-reliability-replay.log)
	$(call ASSERT_GUI_HASH_IF_FB,qemu-smoke-reliability-replay,$(BUILD)/qemu-smoke-reliability-replay.log)
	@$(RELIABILITY_JSON_VALIDATOR) replay $(BUILD)/qemu-smoke-reliability-replay.log
	@$(RELIABILITY_JSON_REPORTER) replay $(BUILD)/qemu-smoke-reliability-replay.log \
		--expect-replay-hash all_seed1337=$(REPLAY_HASH_ALL_SEED1337) \
		--expect-replay-hash quick_seed1337=$(REPLAY_HASH_QUICK_SEED1337) \
		> $(BUILD)/qemu-smoke-reliability-replay.report
	@if ! grep -Fq '{"type":"meta_ext","replay":"all_seed1337","fuzz_lite":0}' $(BUILD)/qemu-smoke-reliability-replay.log; then \
		echo "[qemu-smoke-reliability-replay] Missing replay profile all_seed1337 meta_ext"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-replay.log; \
		exit 1; \
	fi
	@if ! grep -Fq '{"type":"meta_ext","replay":"quick_seed1337","fuzz_lite":0}' $(BUILD)/qemu-smoke-reliability-replay.log; then \
		echo "[qemu-smoke-reliability-replay] Missing replay profile quick_seed1337 meta_ext"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-replay.log; \
		exit 1; \
	fi
	@if ! grep -Fq '{"type":"summary","total":3,"failures":0,"ok":true}' $(BUILD)/qemu-smoke-reliability-replay.log; then \
		echo "[qemu-smoke-reliability-replay] Missing full replay summary"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-replay.log; \
		exit 1; \
	fi
	@if ! grep -Fq '{"type":"summary","total":1,"failures":0,"ok":true}' $(BUILD)/qemu-smoke-reliability-replay.log; then \
		echo "[qemu-smoke-reliability-replay] Missing quick replay summary"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-replay.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc '"type":"trace_summary"' $(BUILD)/qemu-smoke-reliability-replay.log)" -lt 2 ]; then \
		echo "[qemu-smoke-reliability-replay] Missing replay trace summaries"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-replay.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(BUILD)/qemu-smoke-reliability-replay.log; then \
		echo "[qemu-smoke-reliability-replay] Missing shell exit line"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-replay.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-reliability-replay] PASS"

qemu-smoke-reliability-fuzz-lite-matrix: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-reliability-fuzz-lite.log
	@echo "[qemu-smoke-reliability-fuzz-lite-matrix] Booting headless VM with fuzz-lite matrix..."
	@rc=0; \
	{ sleep 12; \
		printf 'cd /bin\n'; \
		sleep $(RELIABILITY_SMOKE_STEP_SECS); \
		printf 'reliability_runner --seed=1337 --script=all --fuzz-lite=1\n'; \
		sleep $(RELIABILITY_FUZZ_RUNNER_SETTLE_SECS); \
		printf 'reliability_runner --seed=1337 --script=all --fuzz-lite=2\n'; \
		sleep $(RELIABILITY_FUZZ_RUNNER_SETTLE_SECS); \
		printf 'reliability_runner --seed=1337 --script=all --fuzz-lite=3\n'; \
		sleep $(RELIABILITY_FUZZ_RUNNER_SETTLE_SECS); \
		printf 'exit\n'; \
	} | \
	timeout -s INT -k 2s $(SHELL_SMOKE_TIMEOUT_SECS)s $(QEMU) \
		-cdrom $(ISO) \
		-display none \
		-serial mon:stdio \
		-monitor none \
		-no-reboot \
		-no-shutdown > $(BUILD)/qemu-smoke-reliability-fuzz-lite.log 2>&1 || rc=$$?; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
		echo "[qemu-smoke-reliability-fuzz-lite-matrix] QEMU failed (exit $$rc)."; \
		exit $$rc; \
	fi
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-reliability-fuzz-lite-matrix,$(BUILD)/qemu-smoke-reliability-fuzz-lite.log)
	$(call ASSERT_GUI_HASH_IF_FB,qemu-smoke-reliability-fuzz-lite-matrix,$(BUILD)/qemu-smoke-reliability-fuzz-lite.log)
	@$(RELIABILITY_JSON_VALIDATOR) fuzz $(BUILD)/qemu-smoke-reliability-fuzz-lite.log
	@$(RELIABILITY_JSON_REPORTER) fuzz $(BUILD)/qemu-smoke-reliability-fuzz-lite.log > $(BUILD)/qemu-smoke-reliability-fuzz-lite.report
	@if [ "$$(grep -Fc '"type":"meta_ext","replay":"-","fuzz_lite":1' $(BUILD)/qemu-smoke-reliability-fuzz-lite.log)" -lt 1 ]; then \
		echo "[qemu-smoke-reliability-fuzz-lite-matrix] Missing fuzz-lite=1 meta_ext"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-fuzz-lite.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc '"type":"meta_ext","replay":"-","fuzz_lite":2' $(BUILD)/qemu-smoke-reliability-fuzz-lite.log)" -lt 1 ]; then \
		echo "[qemu-smoke-reliability-fuzz-lite-matrix] Missing fuzz-lite=2 meta_ext"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-fuzz-lite.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc '"type":"meta_ext","replay":"-","fuzz_lite":3' $(BUILD)/qemu-smoke-reliability-fuzz-lite.log)" -lt 1 ]; then \
		echo "[qemu-smoke-reliability-fuzz-lite-matrix] Missing fuzz-lite=3 meta_ext"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-fuzz-lite.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc '{"type":"summary","total":3,"failures":0,"ok":true}' $(BUILD)/qemu-smoke-reliability-fuzz-lite.log)" -lt 3 ]; then \
		echo "[qemu-smoke-reliability-fuzz-lite-matrix] Missing one or more fuzz-lite summaries"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-fuzz-lite.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc '"type":"trace_summary"' $(BUILD)/qemu-smoke-reliability-fuzz-lite.log)" -lt 3 ]; then \
		echo "[qemu-smoke-reliability-fuzz-lite-matrix] Missing one or more fuzz-lite trace summaries"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-fuzz-lite.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(BUILD)/qemu-smoke-reliability-fuzz-lite.log; then \
		echo "[qemu-smoke-reliability-fuzz-lite-matrix] Missing shell exit line"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability-fuzz-lite.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-reliability-fuzz-lite-matrix] PASS"

qemu-smoke-storage-integrity: $(ISO) $(STORAGE_INTEGRITY_DISK)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-storage-integrity.log
	@echo "[qemu-smoke-storage-integrity] Booting headless VM with writable /persist checks..."
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-storage-integrity,$(BUILD)/qemu-smoke-storage-integrity.log,$(STORAGE_INTEGRITY_DISK),printf 'cd /persist\n'; \
sleep 0.25; \
printf 'echo v1 > file.txt\n'; \
sleep 0.25; \
printf 'echo v2 >> file.txt\n'; \
sleep 0.25; \
printf 'cat file.txt\n'; \
sleep 0.25; \
printf 'rm file.txt\n'; \
sleep 0.25; \
printf 'ls\n'; \
sleep 0.25; \
printf 'cat file.txt\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-storage-integrity,$(BUILD)/qemu-smoke-storage-integrity.log)
	@if ! grep -Fq "ata: identify ok" $(BUILD)/qemu-smoke-storage-integrity.log; then \
		echo "[qemu-smoke-storage-integrity] Missing ATA identify success log"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-integrity.log; \
		exit 1; \
	fi
	@if ! grep -Fq "persistfs: mounted /persist" $(BUILD)/qemu-smoke-storage-integrity.log; then \
		echo "[qemu-smoke-storage-integrity] Missing persistfs mount log"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-integrity.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-storage-integrity.log | grep -Fxq "v1"; then \
		echo "[qemu-smoke-storage-integrity] Missing first written line"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-integrity.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-storage-integrity.log | grep -Fxq "v2"; then \
		echo "[qemu-smoke-storage-integrity] Missing appended line"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-integrity.log; \
		exit 1; \
	fi
	@if tr -d '\r' < $(BUILD)/qemu-smoke-storage-integrity.log | grep -Fxq "file.txt"; then \
		echo "[qemu-smoke-storage-integrity] Delete check failed: file still listed"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-integrity.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'cat: open failed' $(BUILD)/qemu-smoke-storage-integrity.log)" -ne 1 ]; then \
		echo "[qemu-smoke-storage-integrity] Expected one post-delete open failure"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-integrity.log; \
		exit 1; \
	fi
	@if grep -Fq "run: redirect open failed" $(BUILD)/qemu-smoke-storage-integrity.log; then \
		echo "[qemu-smoke-storage-integrity] Unexpected redirect failure"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-integrity.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-storage-integrity] PASS"

qemu-smoke-storage-persist: $(ISO) $(STORAGE_PERSIST_DISK)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-storage-persist.boot1.log $(BUILD)/qemu-smoke-storage-persist.boot2.log
	@echo "[qemu-smoke-storage-persist] Boot #1 write, Boot #2 verify..."
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-storage-persist.boot1,$(BUILD)/qemu-smoke-storage-persist.boot1.log,$(STORAGE_PERSIST_DISK),printf 'cd /persist\n'; \
sleep 0.25; \
	printf 'echo persist-token-42 > persist.txt\n'; \
	sleep 0.25; \
	printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-storage-persist.boot1,$(BUILD)/qemu-smoke-storage-persist.boot1.log)
	@clean_flag=$$(od -An -tu4 -N4 -j8 $(STORAGE_PERSIST_DISK) | tr -d '[:space:]'); \
	if [ "$$clean_flag" != "1" ]; then \
		echo "[qemu-smoke-storage-persist] Expected clean flag=1 after boot1 metadata sync (got $$clean_flag)"; \
		exit 1; \
	fi
	@printf '\000\000\000\000' | dd of=$(STORAGE_PERSIST_DISK) bs=1 seek=8 conv=notrunc status=none
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-storage-persist.boot2,$(BUILD)/qemu-smoke-storage-persist.boot2.log,$(STORAGE_PERSIST_DISK),printf 'cd /persist\n'; \
sleep 0.25; \
	printf 'cat persist.txt\n'; \
	sleep 0.25; \
	printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-storage-persist.boot2,$(BUILD)/qemu-smoke-storage-persist.boot2.log)
	@if ! grep -Fq "persistfs: dirty flag detected; running metadata sanity pass" $(BUILD)/qemu-smoke-storage-persist.boot2.log; then \
		echo "[qemu-smoke-storage-persist] Missing dirty-mount warning"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-persist.boot2.log; \
		exit 1; \
	fi
	@if ! grep -Fq "persistfs: sanity pass ok" $(BUILD)/qemu-smoke-storage-persist.boot2.log; then \
		echo "[qemu-smoke-storage-persist] Missing dirty-mount sanity-pass log"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-persist.boot2.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-storage-persist.boot2.log | grep -Fxq "persist-token-42"; then \
		echo "[qemu-smoke-storage-persist] Missing persisted token on second boot"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-persist.boot2.log; \
		exit 1; \
	fi
	@clean_flag=$$(od -An -tu4 -N4 -j8 $(STORAGE_PERSIST_DISK) | tr -d '[:space:]'); \
	if [ "$$clean_flag" != "1" ]; then \
		echo "[qemu-smoke-storage-persist] Expected clean flag=1 after dirty-mount sanity recovery (got $$clean_flag)"; \
		exit 1; \
	fi
	@echo "[qemu-smoke-storage-persist] PASS"

qemu-smoke-storage-replay: $(ISO) $(STORAGE_REPLAY_DISK)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-storage-replay.cycle1.log $(BUILD)/qemu-smoke-storage-replay.cycle2.log $(BUILD)/qemu-smoke-storage-replay.cycle3.log
	@echo "[qemu-smoke-storage-replay] Running deterministic 3-cycle replay..."
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-storage-replay.cycle1,$(BUILD)/qemu-smoke-storage-replay.cycle1.log,$(STORAGE_REPLAY_DISK),printf 'cd /persist\n'; \
sleep 0.25; \
printf 'echo replay-state-1 > replay.txt\n'; \
sleep 0.25; \
printf 'cat replay.txt\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-storage-replay.cycle1,$(BUILD)/qemu-smoke-storage-replay.cycle1.log)
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-storage-replay.cycle2,$(BUILD)/qemu-smoke-storage-replay.cycle2.log,$(STORAGE_REPLAY_DISK),printf 'cd /persist\n'; \
sleep 0.25; \
printf 'cat replay.txt\n'; \
sleep 0.25; \
printf 'echo replay-state-2 > replay.txt\n'; \
sleep 0.25; \
printf 'cat replay.txt\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-storage-replay.cycle2,$(BUILD)/qemu-smoke-storage-replay.cycle2.log)
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-storage-replay.cycle3,$(BUILD)/qemu-smoke-storage-replay.cycle3.log,$(STORAGE_REPLAY_DISK),printf 'cd /persist\n'; \
sleep 0.25; \
printf 'cat replay.txt\n'; \
sleep 0.25; \
printf 'rm replay.txt\n'; \
sleep 0.25; \
printf 'cat replay.txt\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-storage-replay.cycle3,$(BUILD)/qemu-smoke-storage-replay.cycle3.log)
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-storage-replay.cycle1.log | grep -Fxq "replay-state-1"; then \
		echo "[qemu-smoke-storage-replay] Cycle1 missing state marker"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-replay.cycle1.log; \
		exit 1; \
	fi
	@if [ "$$(tr -d '\r' < $(BUILD)/qemu-smoke-storage-replay.cycle2.log | grep -Fxc 'replay-state-1')" -lt 1 ]; then \
		echo "[qemu-smoke-storage-replay] Cycle2 missing replay-state-1"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-replay.cycle2.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-storage-replay.cycle3.log | grep -Fxq "replay-state-2"; then \
		echo "[qemu-smoke-storage-replay] Cycle3 missing replay-state-2"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-replay.cycle3.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'cat: open failed' $(BUILD)/qemu-smoke-storage-replay.cycle3.log)" -ne 1 ]; then \
		echo "[qemu-smoke-storage-replay] Expected one open failure after delete in cycle3"; \
		tail -n 220 $(BUILD)/qemu-smoke-storage-replay.cycle3.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-storage-replay] PASS"

qemu-smoke-shell-history-persist: $(ISO) $(SHELL_HISTORY_PERSIST_DISK)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-shell-history-persist.boot1.log $(BUILD)/qemu-smoke-shell-history-persist.boot2.log $(BUILD)/qemu-smoke-shell-history-persist.boot3.log
	@echo "[qemu-smoke-shell-history-persist] Boot #1 writes shell history, Boot #2 verifies+clears, Boot #3 verifies cleared state..."
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-shell-history-persist.boot1,$(BUILD)/qemu-smoke-shell-history-persist.boot1.log,$(SHELL_HISTORY_PERSIST_DISK),printf 'history clear\n'; \
sleep 0.25; \
printf 'echo hist-alpha\n'; \
sleep 0.25; \
printf 'echo hist-beta\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-shell-history-persist.boot1,$(BUILD)/qemu-smoke-shell-history-persist.boot1.log)
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-shell-history-persist.boot2,$(BUILD)/qemu-smoke-shell-history-persist.boot2.log,$(SHELL_HISTORY_PERSIST_DISK),printf 'history\n'; \
sleep 0.25; \
printf 'history clear\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-shell-history-persist.boot2,$(BUILD)/qemu-smoke-shell-history-persist.boot2.log)
	$(call RUN_SCRIPTED_SHELL_SMOKE_WITH_DISK,qemu-smoke-shell-history-persist.boot3,$(BUILD)/qemu-smoke-shell-history-persist.boot3.log,$(SHELL_HISTORY_PERSIST_DISK),printf 'history\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-shell-history-persist.boot3,$(BUILD)/qemu-smoke-shell-history-persist.boot3.log)
	@if ! grep -Fq "ata: identify ok" $(BUILD)/qemu-smoke-shell-history-persist.boot1.log; then \
		echo "[qemu-smoke-shell-history-persist] Missing ATA identify success log"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-history-persist.boot1.log; \
		exit 1; \
	fi
	@if ! grep -Fq "persistfs: mounted /persist" $(BUILD)/qemu-smoke-shell-history-persist.boot1.log; then \
		echo "[qemu-smoke-shell-history-persist] Missing persistfs mount log on boot1"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-history-persist.boot1.log; \
		exit 1; \
	fi
	@if ! grep -Fq "echo hist-alpha" $(BUILD)/qemu-smoke-shell-history-persist.boot2.log; then \
		echo "[qemu-smoke-shell-history-persist] Missing persisted history entry hist-alpha"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-history-persist.boot2.log; \
		exit 1; \
	fi
	@if ! grep -Fq "echo hist-beta" $(BUILD)/qemu-smoke-shell-history-persist.boot2.log; then \
		echo "[qemu-smoke-shell-history-persist] Missing persisted history entry hist-beta"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-history-persist.boot2.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'history: cleared' $(BUILD)/qemu-smoke-shell-history-persist.boot2.log)" -ne 1 ]; then \
		echo "[qemu-smoke-shell-history-persist] Expected one history clear confirmation on boot2"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-history-persist.boot2.log; \
		exit 1; \
	fi
	@if grep -Fq "echo hist-alpha" $(BUILD)/qemu-smoke-shell-history-persist.boot3.log || grep -Fq "echo hist-beta" $(BUILD)/qemu-smoke-shell-history-persist.boot3.log; then \
		echo "[qemu-smoke-shell-history-persist] Cleared history unexpectedly replayed on boot3"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-history-persist.boot3.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-shell-history-persist] PASS"

qemu-smoke-fork-cow: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-fork-cow.log
	@echo "[qemu-smoke-fork-cow] Booting headless VM with forktest..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-fork-cow,$(BUILD)/qemu-smoke-fork-cow.log,printf 'cd /bin\n'; \
sleep 0.25; \
printf 'forktest\n'; \
sleep 0.50; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-fork-cow,$(BUILD)/qemu-smoke-fork-cow.log)
	@if ! grep -Fq "forktest: child return 0 ok" $(BUILD)/qemu-smoke-fork-cow.log; then \
		echo "[qemu-smoke-fork-cow] Missing child return marker"; \
		tail -n 220 $(BUILD)/qemu-smoke-fork-cow.log; \
		exit 1; \
	fi
	@if ! grep -Fq "forktest: parent return child_pid ok" $(BUILD)/qemu-smoke-fork-cow.log; then \
		echo "[qemu-smoke-fork-cow] Missing parent pid marker"; \
		tail -n 220 $(BUILD)/qemu-smoke-fork-cow.log; \
		exit 1; \
	fi
	@if ! grep -Fq "forktest: cow static ok" $(BUILD)/qemu-smoke-fork-cow.log; then \
		echo "[qemu-smoke-fork-cow] Missing static-data COW marker"; \
		tail -n 220 $(BUILD)/qemu-smoke-fork-cow.log; \
		exit 1; \
	fi
	@if ! grep -Fq "forktest: cow stack ok" $(BUILD)/qemu-smoke-fork-cow.log; then \
		echo "[qemu-smoke-fork-cow] Missing stack COW marker"; \
		tail -n 220 $(BUILD)/qemu-smoke-fork-cow.log; \
		exit 1; \
	fi
	@if ! grep -Fq "forktest: waitpid status ok" $(BUILD)/qemu-smoke-fork-cow.log; then \
		echo "[qemu-smoke-fork-cow] Missing waitpid marker"; \
		tail -n 220 $(BUILD)/qemu-smoke-fork-cow.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'forktest: PASS' $(BUILD)/qemu-smoke-fork-cow.log)" -ne 1 ]; then \
		echo "[qemu-smoke-fork-cow] Expected exactly one PASS marker"; \
		tail -n 220 $(BUILD)/qemu-smoke-fork-cow.log; \
		exit 1; \
	fi
	@if grep -Fq "user page fault:" $(BUILD)/qemu-smoke-fork-cow.log; then \
		echo "[qemu-smoke-fork-cow] Unexpected user page fault"; \
		tail -n 220 $(BUILD)/qemu-smoke-fork-cow.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-fork-cow] PASS"

qemu-smoke-fork-cow-stress: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-fork-cow-stress.log
	@echo "[qemu-smoke-fork-cow-stress] Booting headless VM with repeated forktest..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-fork-cow-stress,$(BUILD)/qemu-smoke-fork-cow-stress.log,printf 'cd /bin\n'; \
sleep 0.25; \
printf 'forktest 4\n'; \
sleep 1.00; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-fork-cow-stress,$(BUILD)/qemu-smoke-fork-cow-stress.log)
	@if [ "$$(grep -Fc 'forktest: PASS' $(BUILD)/qemu-smoke-fork-cow-stress.log)" -ne 4 ]; then \
		echo "[qemu-smoke-fork-cow-stress] Expected four PASS markers"; \
		tail -n 260 $(BUILD)/qemu-smoke-fork-cow-stress.log; \
		exit 1; \
	fi
	@if grep -Fq "user page fault:" $(BUILD)/qemu-smoke-fork-cow-stress.log; then \
		echo "[qemu-smoke-fork-cow-stress] Unexpected user page fault"; \
		tail -n 260 $(BUILD)/qemu-smoke-fork-cow-stress.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-fork-cow-stress] PASS"

qemu-smoke-fork-cow-pressure: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-fork-cow-pressure.log
	@echo "[qemu-smoke-fork-cow-pressure] Booting headless VM with fork pressure scenario..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-fork-cow-pressure,$(BUILD)/qemu-smoke-fork-cow-pressure.log,printf 'cd /bin\n'; \
sleep 0.25; \
printf 'forktest pressure\n'; \
sleep 1.25; \
printf 'echo pressure-shell-alive\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-fork-cow-pressure,$(BUILD)/qemu-smoke-fork-cow-pressure.log)
	@if ! grep -Fq "forktest: pressure fork failure ok" $(BUILD)/qemu-smoke-fork-cow-pressure.log; then \
		echo "[qemu-smoke-fork-cow-pressure] Missing deterministic fork-failure marker"; \
		tail -n 260 $(BUILD)/qemu-smoke-fork-cow-pressure.log; \
		exit 1; \
	fi
	@if ! grep -Fq "forktest: pressure reap ok" $(BUILD)/qemu-smoke-fork-cow-pressure.log; then \
		echo "[qemu-smoke-fork-cow-pressure] Missing pressure reap marker"; \
		tail -n 260 $(BUILD)/qemu-smoke-fork-cow-pressure.log; \
		exit 1; \
	fi
	@if ! grep -Fq "forktest: pressure PASS" $(BUILD)/qemu-smoke-fork-cow-pressure.log; then \
		echo "[qemu-smoke-fork-cow-pressure] Missing pressure PASS marker"; \
		tail -n 260 $(BUILD)/qemu-smoke-fork-cow-pressure.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-fork-cow-pressure.log | grep -Fxq "pressure-shell-alive"; then \
		echo "[qemu-smoke-fork-cow-pressure] Missing shell responsiveness marker after pressure run"; \
		tail -n 260 $(BUILD)/qemu-smoke-fork-cow-pressure.log; \
		exit 1; \
	fi
	@if grep -Fq "user page fault:" $(BUILD)/qemu-smoke-fork-cow-pressure.log; then \
		echo "[qemu-smoke-fork-cow-pressure] Unexpected user page fault"; \
		tail -n 260 $(BUILD)/qemu-smoke-fork-cow-pressure.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-fork-cow-pressure] PASS"

qemu-smoke-shell-bg-replay: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-shell-bg-replay.log
	@echo "[qemu-smoke-shell-bg-replay] Booting headless VM with concurrent background job replay..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-shell-bg-replay,$(BUILD)/qemu-smoke-shell-bg-replay.log,printf 'cd /bin\n'; \
sleep 0.25; \
printf 'sleep 60 | cat &\n'; \
sleep 0.10; \
printf 'sleep2 220 &\n'; \
sleep 0.10; \
printf 'jobs\n'; \
sleep 0.20; \
printf 'fg 1\n'; \
sleep 0.15; \
printf 'jobs\n'; \
sleep 0.20; \
printf 'fg\n'; \
sleep 0.15; \
printf 'wait\n'; \
sleep 0.25; \
printf 'exit\n';)
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-shell-bg-replay,$(BUILD)/qemu-smoke-shell-bg-replay.log)
	@if [ "$$(grep -Fc 'bg: started job=' $(BUILD)/qemu-smoke-shell-bg-replay.log)" -ne 2 ]; then \
		echo "[qemu-smoke-shell-bg-replay] Expected two background launch markers"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-bg-replay.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'bg: done job=' $(BUILD)/qemu-smoke-shell-bg-replay.log)" -ne 2 ]; then \
		echo "[qemu-smoke-shell-bg-replay] Expected two background completion markers"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-bg-replay.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'jobs: id=' $(BUILD)/qemu-smoke-shell-bg-replay.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-bg-replay] Missing jobs builtin listing output"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-bg-replay.log; \
		exit 1; \
	fi
	@if ! grep -Fq 'fg: done job=1' $(BUILD)/qemu-smoke-shell-bg-replay.log || \
		! grep -Fq 'fg: done job=2' $(BUILD)/qemu-smoke-shell-bg-replay.log; then \
		echo "[qemu-smoke-shell-bg-replay] Missing fg completion for explicit and latest job targets"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-bg-replay.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'jobs: id=1' $(BUILD)/qemu-smoke-shell-bg-replay.log)" -lt 1 ] || \
		[ "$$(grep -Fc 'jobs: id=2' $(BUILD)/qemu-smoke-shell-bg-replay.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-bg-replay] Missing multi-job list output for fg replay"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-bg-replay.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'wait: no background jobs' $(BUILD)/qemu-smoke-shell-bg-replay.log)" -lt 1 ]; then \
		echo "[qemu-smoke-shell-bg-replay] Missing no-job wait marker after fg replay"; \
		tail -n 260 $(BUILD)/qemu-smoke-shell-bg-replay.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(BUILD)/qemu-smoke-shell-bg-replay.log; then \
		echo "[qemu-smoke-shell-bg-replay] Missing shell exit line"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-bg-replay.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-shell-bg-replay] PASS"

qemu-smoke-gui-fb-dump: $(ISO)
	@mkdir -p $(NIGHTLY_GUI_TRIAGE_ARTIFACT_DIR)
	@stamp=$$(date -u +%Y%m%dT%H%M%SZ); \
	port=$$((43000 + ($$$$ % 10000))); \
	dump="$(NIGHTLY_GUI_TRIAGE_ARTIFACT_DIR)/gui-fb-failure-$${stamp}.ppm"; \
	log="$(NIGHTLY_GUI_TRIAGE_ARTIFACT_DIR)/gui-fb-failure-$${stamp}.qemu.log"; \
	echo "[qemu-smoke-gui-fb-dump] Capturing framebuffer artifact to $$dump (qmp_port=$$port)"; \
	./scripts/capture_qemu_fb_dump.sh "$(ISO)" "$$dump" "$$log" "$(NIGHTLY_GUI_TRIAGE_BOOT_WAIT_SECS)" "$$port" failure

qemu-smoke-gui-visual-baseline: $(ISO)
	@mkdir -p $(GUI_VISUAL_BASELINE_ARTIFACT_DIR)
	@stamp=$$(date -u +%Y%m%dT%H%M%SZ); \
	port=$$((44000 + ($$$$ % 10000))); \
	dump="$(GUI_VISUAL_BASELINE_ARTIFACT_DIR)/gui-fb-baseline-$${stamp}.ppm"; \
	log="$(GUI_VISUAL_BASELINE_ARTIFACT_DIR)/gui-fb-baseline-$${stamp}.qemu.log"; \
	echo "[qemu-smoke-gui-visual-baseline] Capturing framebuffer baseline artifact to $$dump (qmp_port=$$port)"; \
	./scripts/capture_qemu_fb_dump.sh "$(ISO)" "$$dump" "$$log" "$(GUI_VISUAL_BASELINE_BOOT_WAIT_SECS)" "$$port" baseline; \
	./scripts/gui_visual_baseline.py verify --ppm "$$dump" --profile fb-shell-v6 --expect-hash "$(GUI_VISUAL_BASELINE_HASH_FB_SHELL_V6)"
	@echo "[qemu-smoke-gui-visual-baseline] PASS"

qemu-smoke-gui-visual-baseline-refresh: $(ISO)
	@mkdir -p $(GUI_VISUAL_BASELINE_ARTIFACT_DIR)
	@stamp=$$(date -u +%Y%m%dT%H%M%SZ); \
	port=$$((45000 + ($$$$ % 10000))); \
	dump="$(GUI_VISUAL_BASELINE_ARTIFACT_DIR)/gui-fb-baseline-refresh-$${stamp}.ppm"; \
	log="$(GUI_VISUAL_BASELINE_ARTIFACT_DIR)/gui-fb-baseline-refresh-$${stamp}.qemu.log"; \
	echo "[qemu-smoke-gui-visual-baseline-refresh] Capturing framebuffer artifact to $$dump (qmp_port=$$port)"; \
	./scripts/capture_qemu_fb_dump.sh "$(ISO)" "$$dump" "$$log" "$(GUI_VISUAL_BASELINE_BOOT_WAIT_SECS)" "$$port" baseline; \
	./scripts/gui_visual_baseline.py hash --ppm "$$dump" --profile fb-shell-v6

qemu-smoke-gui-nav: $(ISO)
	@mkdir -p $(GUI_NAV_ARTIFACT_DIR)
	@stamp=$$(date -u +%Y%m%dT%H%M%SZ); \
	port_a=$$((46000 + ($$$$ % 1000))); \
	port_b=$$((47000 + ($$$$ % 1000))); \
	dump_a="$(GUI_NAV_ARTIFACT_DIR)/gui-fb-nav-task-$${stamp}.ppm"; \
	log_a="$(GUI_NAV_ARTIFACT_DIR)/gui-fb-nav-task-$${stamp}.qemu.log"; \
	dump_b="$(GUI_NAV_ARTIFACT_DIR)/gui-fb-nav-focus-$${stamp}.ppm"; \
	log_b="$(GUI_NAV_ARTIFACT_DIR)/gui-fb-nav-focus-$${stamp}.qemu.log"; \
	echo "[qemu-smoke-gui-nav] Capturing TASK view artifact to $$dump_a (qmp_port=$$port_a)"; \
	./scripts/capture_qemu_fb_dump.sh "$(ISO)" "$$dump_a" "$$log_a" "$(GUI_NAV_BOOT_WAIT_SECS)" "$$port_a" nav-task "" "down"; \
	./scripts/gui_visual_baseline.py verify --ppm "$$dump_a" --profile fb-shell-v6-nav-task --expect-hash "$(GUI_VISUAL_BASELINE_HASH_FB_SHELL_V6_NAV_TASK)"; \
	echo "[qemu-smoke-gui-nav] Capturing multi-step focus artifact to $$dump_b (qmp_port=$$port_b)"; \
	./scripts/capture_qemu_fb_dump.sh "$(ISO)" "$$dump_b" "$$log_b" "$(GUI_NAV_BOOT_WAIT_SECS)" "$$port_b" nav-focus "" "down,right,down,left,up"; \
	./scripts/gui_visual_baseline.py verify --ppm "$$dump_b" --profile fb-shell-v6-nav-focus --expect-hash "$(GUI_VISUAL_BASELINE_HASH_FB_SHELL_V6_NAV_FOCUS)"
	@echo "[qemu-smoke-gui-nav] PASS"

check-pr:
	@$(MAKE) toolchain-check
	@$(MAKE) clean
	@$(MAKE) all
	@$(MAKE) qemu-smoke-userfault
	@$(MAKE) qemu-smoke-shell-core
	@$(MAKE) qemu-smoke-reliability
	@$(MAKE) qemu-smoke-fork-cow

check: check-pr

check-release: check-pr
	@$(MAKE) qemu-smoke-lifecycle
	@$(MAKE) qemu-smoke-shell-script-core

check-nightly:
	@rc=0; \
	$(MAKE) check-release || rc=$$?; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-reliability-replay || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-reliability-fuzz-lite-matrix || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-storage-integrity || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-storage-persist || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-storage-replay || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-shell-history-persist || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-fork-cow || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-fork-cow-stress || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-fork-cow-pressure || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-shell-bg-replay || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-gui-nav || rc=$$?; \
	fi; \
	if [ $$rc -eq 0 ]; then \
		$(MAKE) qemu-smoke-gui-visual-baseline || rc=$$?; \
	fi; \
	if [ $$rc -ne 0 ]; then \
		echo "[check-nightly] failure detected; collecting non-gating framebuffer triage artifact..."; \
		$(MAKE) qemu-smoke-gui-fb-dump || \
			echo "[check-nightly] warning: framebuffer triage capture failed"; \
	fi; \
	exit $$rc

clean:
	rm -rf $(BUILD) $(ISO) iso/boot/kernel.elf

# Build object from assembly in the kernel directory
$(BUILD)/idt_load.o: kernel/idt_load.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/sched_switch.o: kernel/sched_switch.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/gdt_flush.o: kernel/gdt_flush.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/syscall_entry.o: kernel/syscall_entry.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/user_demo_blob.o: kernel/user_demo_blob.S
	$(AS) $(ASFLAGS) $< -o $@
