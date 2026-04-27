.PHONY: qemu board clean

# Build kernel for QEMU + start QEMU with serial PTY.
# After QEMU prints the PTY path, send the kernel in another terminal:
#   uv run send_kernel.py /dev/pts/X kernel/payload_os.bin
qemu:
	$(MAKE) -C kernel build_qemu
	$(MAKE) -C bootloader run

# Build OS kernel for the board only.
# Press reset, then send the kernel:
#   uv run send_kernel.py /dev/ttyUSB0 kernel/payload_os.bin
board:
	$(MAKE) -C kernel build

clean:
	$(MAKE) -C kernel clean
	$(MAKE) -C bootloader clean
