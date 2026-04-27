CROSS   ?= riscv64-unknown-elf
CC      := $(CROSS)-gcc
LD      := $(CROSS)-ld
OBJCOPY := $(CROSS)-objcopy

TARGET  := payload_os
LIB_DIR := ../lib

CFLAGS := -mcmodel=medany -ffreestanding -nostdlib -g -Wall -I$(LIB_DIR)
SRCS   := start.S main.c shell.c mm.c \
          $(LIB_DIR)/uart.c $(LIB_DIR)/fdt.c $(LIB_DIR)/string.c $(LIB_DIR)/cpio.c \
          $(LIB_DIR)/utils.c

.PHONY: build build_qemu clean

build: clean
	$(CC) $(CFLAGS) -c $(SRCS)
	$(LD) -T link.ld --defsym=KERNEL_START=0x00200000 -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin

build_qemu: clean
	$(CC) $(CFLAGS) -DQEMU -c $(SRCS)
	$(LD) -T link.ld --defsym=KERNEL_START=0x80200000 -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin

clean:
	rm -f $(TARGET).bin $(TARGET).elf *.o
