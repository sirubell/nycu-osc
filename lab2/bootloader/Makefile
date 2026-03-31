RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
QEMU_EXEC ?= qemu-system-riscv64
TARGET = bootloader
LIB_DIR = ../lib
INITRD = ../initramfs.cpio

# Address configuration
START_ADDR_BOARD = 0x00200000
START_ADDR_QEMU  = 0x80200000

# Default is Board Mode
KERNEL_START ?= $(START_ADDR_BOARD)
EXTRA_FLAGS  ?=

CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall -I$(LIB_DIR)

# Local sources
LOCAL_SRCS_S = start.S
LOCAL_SRCS_C = main.c

# Shared library sources (no cpio — bootloader doesn't need initrd parsing)
LIB_SRCS_C = $(LIB_DIR)/uart.c $(LIB_DIR)/fdt.c $(LIB_DIR)/string.c

build: clean
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) -c $(LOCAL_SRCS_S) $(LOCAL_SRCS_C)
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) -c $(LIB_SRCS_C)
	$(LD) -T link.ld --defsym=KERNEL_START=$(KERNEL_START) -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin
	@if [ -f bootloader.its ]; then \
		mkimage -f bootloader.its $(TARGET).fit; \
	fi

# QEMU mode: rebuild with QEMU parameters, then run with serial PTY
# -initrd lets QEMU inject linux,initrd-start into DTB so kernel can read it
run:
	@$(MAKE) build KERNEL_START=$(START_ADDR_QEMU) EXTRA_FLAGS="-DQEMU"
	$(QEMU_EXEC) -M virt -m 8G -kernel $(TARGET).elf -initrd $(INITRD) -display none -serial pty

clean:
	rm -f $(TARGET).bin $(TARGET).elf $(TARGET).fit *.o

# QEMU debug mode: pauses execution and waits for GDB attachment on port 1234
debug:
	@$(MAKE) build KERNEL_START=$(START_ADDR_QEMU) EXTRA_FLAGS="-DQEMU -g"
	$(QEMU_EXEC) -M virt -m 8G -kernel $(TARGET).elf -initrd $(INITRD) -display none -serial pty -S -s
