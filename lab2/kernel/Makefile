RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
TARGET = payload_os
LIB_DIR = ../lib

# Address configuration
START_ADDR_BOARD = 0x00200000
START_ADDR_QEMU  = 0x80200000

# Default is Board Mode
KERNEL_START ?= $(START_ADDR_BOARD)
EXTRA_FLAGS  ?=

CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall -I$(LIB_DIR)

# Local sources
LOCAL_SRCS_S = start.S
LOCAL_SRCS_C = main.c shell.c

# Shared library sources
LIB_SRCS_C = $(LIB_DIR)/uart.c $(LIB_DIR)/fdt.c $(LIB_DIR)/string.c $(LIB_DIR)/cpio.c

# Main build target — outputs payload_os.bin for sending via send_kernel.py
build: clean
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) -c $(LOCAL_SRCS_S) $(LOCAL_SRCS_C)
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) -c $(LIB_SRCS_C)
	$(LD) -T link.ld --defsym=KERNEL_START=$(KERNEL_START) -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin

# Build for QEMU
build_qemu:
	@$(MAKE) build KERNEL_START=$(START_ADDR_QEMU) EXTRA_FLAGS="-DQEMU"

clean:
	rm -f $(TARGET).bin $(TARGET).elf *.o
