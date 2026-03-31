DISK ?= /dev/disk4s1

.PHONY: qemu board clean

# ── QEMU ──────────────────────────────────────────────────────────────────────
# Builds kernel + bootloader in QEMU mode, starts QEMU with -initrd so that
# QEMU injects linux,initrd-start into DTB.
# After QEMU prints the PTY path, run send_kernel.py in another terminal:
#   python3 send_kernel.py /dev/pts/X kernel/payload_os.bin

qemu:
	$(MAKE) -C kernel build_qemu
	$(MAKE) -C bootloader run

# ── Board ─────────────────────────────────────────────────────────────────────
# Builds kernel + bootloader.fit (with ramdisk), then flashes to SD card.
# After flashing, press reset on the board, then run send_kernel.py:
#   python3 send_kernel.py /dev/ttyUSB0 kernel/payload_os.bin

board:
	$(MAKE) -C kernel build
	$(MAKE) -C bootloader build
	sudo e2cp bootloader/bootloader.fit $(DISK):/kernel.fit
	sync
	@echo "Flash complete. Press reset on the board."

# ── Clean ─────────────────────────────────────────────────────────────────────

clean:
	$(MAKE) -C kernel clean
	$(MAKE) -C bootloader clean
