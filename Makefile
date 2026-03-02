TARGET = i686-elf
CC     = gcc
AS     = as
LD     = ld
QEMU   ?= qemu-system-i386

# Flags for building a freestanding 32‑bit kernel.  We disable stack
# protector and all floating point support.  We also disable PIE and
# built‑ins so that no assumptions are made about the runtime
# environment.
 CFLAGS = -ffreestanding -fno-pic -m32 -O2 -Wall -Wextra \
          -nostdlib -fno-builtin -fno-stack-protector -mno-sse \
          -mno-sse2 -mno-red-zone -mno-80387
ASFLAGS = --32
LDFLAGS = -m elf_i386

# Build directories
BUILD = build
ISO   = skezos.iso
SMOKE_LOG = $(BUILD)/qemu-smoke.log
SMOKE_TIMEOUT_SECS ?= 8
SMOKE_MARKER = SKEZOS_SMOKE_READY
PHASE4_REPEAT ?= 3
USERLAND_BUILD = $(BUILD)/userland
INITRAMFS_STAGING = $(BUILD)/initramfs_root
INITRAMFS_TAR = $(BUILD)/initramfs_demo.tar
USERLAND_ASFLAGS = $(ASFLAGS) -I userland
USERLAND_CFLAGS = $(CFLAGS) -I userland
USERLAND_LDFLAGS = $(LDFLAGS) -nostdlib -N -e _start

SRCS  = $(wildcard kernel/*.c)
OBJS  = $(patsubst kernel/%.c,$(BUILD)/%.o,$(SRCS))

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
	$(USERLAND_BUILD)/sleep_slot9.o

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
	$(USERLAND_BUILD)/sleep.elf

.PHONY: all run clean toolchain-check qemu-smoke qemu-smoke-userfault qemu-smoke-phase4 qemu-smoke-phase4-repeat qemu-smoke-phase5 qemu-smoke-phase6 check

all: $(ISO)

$(BUILD)/%.o: kernel/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/multiboot2_header.o: boot/multiboot2_header.S
	@mkdir -p $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/loader.o: boot/loader.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/kernel.elf: $(BUILD)/multiboot2_header.o $(BUILD)/loader.o $(ASM_OBJS) $(OBJS)
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o $@ $^

$(BUILD)/initramfs_demo_blob.o: kernel/initramfs_demo_blob.c

$(USERLAND_BUILD)/%.o: userland/%.S userland/runtime.inc userland/syscall_abi.inc
	@mkdir -p $(USERLAND_BUILD)
	$(AS) $(USERLAND_ASFLAGS) $< -o $@

$(USERLAND_BUILD)/%.o: userland/%.c userland/userlib.h
	@mkdir -p $(USERLAND_BUILD)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

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

run: $(ISO)
	# Runs QEMU with graphical UI (GTK) and forward COM1 to this terminal.
	# Using "mon:stdio" keeps stdin input reliable when piped input at launch.
	$(QEMU) \
		-cdrom $(ISO) \
		-display gtk \
		-serial mon:stdio \
		-monitor none \

toolchain-check:
	@missing=0; \
	for tool in $(CC) $(AS) $(LD) grub-mkrescue xorriso $(QEMU) timeout tar od; do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "Missing required tool: $$tool"; \
			missing=1; \
		fi; \
	done; \
	if [ $$missing -ne 0 ]; then \
		echo "Install missing tools and rerun."; \
		exit 1; \
	fi

qemu-smoke: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(SMOKE_LOG)
	@echo "[qemu-smoke] Booting headless VM for up to $(SMOKE_TIMEOUT_SECS)s..."
	@rc=0; \
	timeout -s INT -k 2s $(SMOKE_TIMEOUT_SECS)s $(QEMU) \
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

qemu-smoke-userfault:
	@$(MAKE) qemu-smoke SMOKE_LOG=$(BUILD)/qemu-smoke-userfault.log
	@if ! grep -Fq "user page fault:" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing user page fault log"; \
		tail -n 80 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@if ! grep -Fq "name=user-fault" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing user-fault task termination log"; \
		tail -n 80 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@if ! awk 'BEGIN{pf=0;post=0} /user page fault:/{pf=1} pf && ($$0 ~ /sched demo:/ || $$0 ~ /idle heartbeat/ || $$0 ~ /sched: task start/ || $$0 ~ /sys_exit:/){post=1} END{exit (pf && post) ? 0 : 1}' $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] No scheduler progress observed after user fault"; \
		tail -n 120 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-userfault] PASS"

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

qemu-smoke-phase5:
	@$(MAKE) qemu-smoke-phase4
	@if ! grep -Fq "elf-b: sys_waitpid /bin/hello3.elf ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase5] Missing waitpid hello3 success log"; \
		tail -n 180 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_waitpid /bin/hello4.elf ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase5] Missing waitpid hello4 success log"; \
		tail -n 180 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_waitpid /bin/hello3.elf reuse ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase5] Missing waitpid reuse success log"; \
		tail -n 200 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'sys_waitpid:' $(BUILD)/qemu-smoke-phase4.log)" -lt 3 ]; then \
		echo "[qemu-smoke-phase5] Expected at least three sys_waitpid kernel logs"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'elf32: scratch reclaimed path=' $(BUILD)/qemu-smoke-phase4.log)" -lt 5 ]; then \
		echo "[qemu-smoke-phase5] Expected scratch reclamation for all ELF loads"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sched: task stack reclaimed pid=" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase5] Missing waited-child stack reclamation log"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Eq "sched: deferred stack reclaimed live_large=(65536|69632|81920|86016)" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase5] Missing final deferred stack reclamation watermark"; \
		tail -n 240 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-phase5] PASS"

qemu-smoke-phase6:
	@$(MAKE) qemu-smoke-phase5
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-phase6.log
	@echo "[qemu-smoke-phase6] Booting headless VM with scripted shell input..."
	@rc=0; \
	{ sleep 5; \
		printf 'helx\177p\n'; \
		sleep 0.25; \
		printf 'readlq\177n\n'; \
		sleep 0.25; \
		printf 'stdin handoff worzz\177\177ks\n'; \
		sleep 0.25; \
		printf 'ps\n'; \
		sleep 0.25; \
		printf 'uptime\n'; \
		sleep 0.25; \
		printf 'sleep 2\n'; \
		sleep 0.25; \
		printf 'echo phase6 interactive echo\n'; \
		sleep 0.25; \
		printf 'cat readme.txt\n'; \
		sleep 0.25; \
		printf 'missing\n'; \
		sleep 0.25; \
		printf 'exit\n'; \
	} | \
	timeout -s INT -k 2s $(SMOKE_TIMEOUT_SECS)s $(QEMU) \
		-cdrom $(ISO) \
		-display none \
		-serial mon:stdio \
		-monitor none \
		-no-reboot \
		-no-shutdown > $(BUILD)/qemu-smoke-phase6.log 2>&1 || rc=$$?; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
		echo "[qemu-smoke-phase6] QEMU failed (exit $$rc)."; \
		exit $$rc; \
	fi
	@if ! grep -Fq "$(SMOKE_MARKER)" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing readiness marker: $(SMOKE_MARKER)"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: bootstrap shell online" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing shell bootstrap banner"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! awk 'BEGIN{in_shell=0;bad=0} /sh: bootstrap shell online/{in_shell=1} /sh: bootstrap shell exit/{in_shell=0} in_shell && /sched demo:/{bad=1} END{exit bad ? 1 : 0}' $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Unexpected worker demo log spam while shell was active"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fo 'sh> ' $(BUILD)/qemu-smoke-phase6.log | wc -l)" -lt 7 ]; then \
		echo "[qemu-smoke-phase6] Missing repeated shell prompts"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: builtins help wait ps exit; external names map to /bin/<name>.elf" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing shell help output"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "readln: stdin handoff works" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing foreground stdin handoff output"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "ps: pid=" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing ps task snapshot output"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "uptime: ticks_hi=" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing uptime output"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sleep: requested=2 elapsed=" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing sleep output"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'phase6 interactive echo' $(BUILD)/qemu-smoke-phase6.log)" -lt 2 ]; then \
		echo "[qemu-smoke-phase6] Missing echo output"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "cat: SkezOS tarfs demo" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing cat output"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: command failed" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing unknown-command failure output"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sys_waitpid: parent_pid=" $(BUILD)/qemu-smoke-phase6.log || ! grep -Fq "task=user-shell waited_pid=" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing user-shell waitpid log"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: bootstrap shell exit" $(BUILD)/qemu-smoke-phase6.log; then \
		echo "[qemu-smoke-phase6] Missing shell exit line"; \
		tail -n 220 $(BUILD)/qemu-smoke-phase6.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-phase6] PASS (interactive shell)"

check:
	@$(MAKE) toolchain-check
	@$(MAKE) clean
	@$(MAKE) all
	@$(MAKE) qemu-smoke-userfault
	@$(MAKE) qemu-smoke-phase6

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
