[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/faw2eYL1)

# Lab 1: Hello World - Operating System Capstone

This repository contains the bare-metal kernel implementation for Lab 1, designed to run on the OrangePi RV2 (RISC-V) and QEMU. It implements a basic interactive shell with UART communication and OpenSBI integration.

## Project Structure
* `main.c`: Contains the OpenSBI wrapper functions and the main interactive shell loop.
* `string.c`: Custom bare-metal implementations of string operations (`strcmp`, `strlen`, `memset`).
* `start.S`: Assembly startup routine to zero the `.bss` segment and configure the stack pointer.
* `uart.c`: Memory-Mapped I/O logic for UART serial communication.
* `link.ld`: Linker script defining the memory layout for the OS.
* `kernel.its`: Configuration file for packaging the kernel and Device Tree Blob (DTB) into a Flattened Image Tree (FIT).
* `Makefile`: Build configuration for compiling the kernel and generating FIT images.

## Prerequisites
This project uses a hybrid workflow: compiling inside an OrbStack Linux environment, and deploying/debugging from the macOS host.

### macOS Host (Deployment & Serial)
Install `e2tools` on your Mac to copy files directly into the `ext4` Linux partition of your SD card without needing to mount it:
```bash
brew install e2tools
```

### Linux Environment (OrbStack)
Ensure you have the RISC-V cross-compiler (`riscv64-unknown-elf-gcc`), QEMU, and U-Boot tools (`u-boot-tools` or `mkimage`) installed in your Linux machine.

---

## 1. Entering the Development Environment (macOS -> Linux)
Open your macOS terminal and enter your OrbStack Linux machine (e.g., `osc-dev`) where your toolchain is installed:
```bash
orb -m osc-dev
```

## 2. Building the Kernel (Linux)
Inside the OrbStack Linux environment, compile the source code and package it into a FIT image:
```bash
# Compile and generate kernel.fit
make build
```

## 3. Deploying to the SD Card (macOS)
1. Connect your SD card reader to your Mac and identify the boot partition:
```bash
diskutil list
```
2. Overwrite the existing FIT image inside the unmounted boot partition using `e2tools`:
```bash
# Replace /dev/disk4s1 with your actual partition identifier
sudo e2rm /dev/disk4s1:/kernel.fit
sudo e2cp kernel.fit /dev/disk4s1:/kernel.fit

# Ensure data is flushed to the SD card before ejecting
sync
```

## 4. Running the Kernel

### On the physical OrangePi RV2 board (macOS)
1. Safely eject the SD card and insert it into the OrangePi RV2.
2. Establish a connection using `screen`:
```bash
screen /dev/cu.usbserial-<your_device_id> 115200
```
3. Power on the board to access the `opi-rv2> ` shell.

### On QEMU Emulator (Linux)
To quickly verify your kernel's basic logic locally inside OrbStack:
```bash
make run
```
To exit QEMU, press `Ctrl-A` followed by `X`.
