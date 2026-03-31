RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall
QEMU_EXEC ?= qemu-system-riscv64
TARGET = kernel

# Address configuration
START_ADDR_BOARD = 0x00200000
START_ADDR_QEMU  = 0x80200000

# Default is Board Mode
KERNEL_START ?= $(START_ADDR_BOARD)
EXTRA_FLAGS  ?= 

# Main build target
build: clean
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) -c *.S *.c
	$(LD) -T link.ld --defsym=KERNEL_START=$(KERNEL_START) -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET)
	@if [ -f kernel.its ]; then \
		mkimage -f kernel.its $(TARGET).fit; \
	fi

# QEMU mode: will clean and rebuild with QEMU parameters, then execute
run:
	@$(MAKE) build KERNEL_START=$(START_ADDR_QEMU) EXTRA_FLAGS="-DQEMU"
	$(QEMU_EXEC) -M virt -m 8G -kernel $(TARGET).elf -display none -serial stdio

clean:
	rm -f $(TARGET) $(TARGET).elf $(TARGET).fit *.o
