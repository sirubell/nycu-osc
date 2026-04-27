CROSS   ?= riscv64-unknown-elf
CC      := $(CROSS)-gcc
LD      := $(CROSS)-ld
OBJCOPY := $(CROSS)-objcopy
QEMU    ?= qemu-system-riscv64

TARGET  := bootloader
LIB_DIR := ../lib
INITRD  := initramfs.cpio

CFLAGS := -mcmodel=medany -ffreestanding -nostdlib -g -Wall -I$(LIB_DIR)
SRCS   := start.S main.c \
          $(LIB_DIR)/uart.c $(LIB_DIR)/fdt.c $(LIB_DIR)/string.c

.PHONY: build _build_qemu run debug clean

build: clean
	$(CC) $(CFLAGS) -c $(SRCS)
	$(LD) -T link.ld --defsym=KERNEL_START=0x00200000 -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin
	mkimage -f bootloader.its $(TARGET).fit

_build_qemu: clean
	$(CC) $(CFLAGS) -DQEMU -c $(SRCS)
	$(LD) -T link.ld --defsym=KERNEL_START=0x80200000 -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin

run: _build_qemu
	$(QEMU) -M virt -m 8G -kernel $(TARGET).elf -initrd $(INITRD) -display none -serial pty

debug: _build_qemu
	$(QEMU) -M virt -m 8G -kernel $(TARGET).elf -initrd $(INITRD) -display none -serial pty -S -s

clean:
	rm -f $(TARGET).bin $(TARGET).elf $(TARGET).fit *.o
