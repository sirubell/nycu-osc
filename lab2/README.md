[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/-oo4r1dh)

# Lab 2 — Booting

Implementation of a UART bootloader for the OrangePi RV2 (RISC-V), covering devicetree parsing, initial ramdisk, and bootloader self-relocation.

---

## Lab Requirements

### Basic Exercise 1 — UART Bootloader (30%)

Design a protocol to transmit a kernel binary over UART and load it into memory for execution.

**Implementation:**
- `bootloader/` is a minimal bare-metal program that initialises UART via the devicetree and scans the UART byte stream for a `BOOT` magic header (`0x544F4F42`, little-endian `"BOOT"`) followed by a 4-byte little-endian payload size, then reads that many bytes into memory.
- `send_kernel.py` on the host side packs the header and streams the binary in 128-byte chunks with small inter-chunk delays to avoid overrunning the UART RX buffer.

### Basic Exercise 2 — Devicetree (35%)

Parse the Flattened Devicetree (FDT/DTB) passed by OpenSBI in register `a1` to discover hardware addresses at runtime instead of hardcoding them.

**Implementation:**
- `lib/fdt.c` implements `fdt_path_offset()` and `fdt_getprop()` to walk the FDT structure block and retrieve node properties by path.
- `lib/uart.c` calls `fdt_getprop(dtb, "/soc/serial", "reg", ...)` (falling back to `/soc/uart`) to read the UART base address at boot time.

### Basic Exercise 3 — Initial Ramdisk (35%)

Load and parse a CPIO (New ASCII Format) initramfs archive. The initrd address is read from the FDT `/chosen` node — hardcoded addresses are not allowed.

**Implementation:**
- The initramfs (`initramfs.cpio`) is bundled into `bootloader/bootloader.fit` as a ramdisk image alongside the bootloader binary and DTB. When U-Boot loads this FIT image, it writes `linux,initrd-start` into the DTB `/chosen` node automatically.
- `kernel/main.c` reads `linux,initrd-start` from the DTB via `fdt_getprop` to locate the initrd at runtime — no address is hardcoded and nothing is sent over UART.
- `lib/cpio.c` implements `initrd_list()` and `initrd_cat()` by parsing the 110-byte `cpio_newc_header` followed by the null-terminated filename (4-byte padded) and the file data (4-byte padded).
- The kernel shell exposes `ls` and `cat <file>` commands backed by these parsers.

### Advanced Exercise — Bootloader Self-Relocation (10%)

The bootloader is initially loaded to the standard kernel entry point (`0x00200000` / `0x80200000` on QEMU). To free that address for the actual kernel, the bootloader self-relocates to `KERNEL_START + 0x200000` before receiving the kernel payload.

**Implementation:**
- `bootloader/start.S` compares the runtime PC against the linked address. If they differ, it copies itself word-by-word to the target and performs an absolute jump to the relocated image using a constant table of 64-bit addresses embedded in the binary.
- `bootloader/link.ld` sets `_reloc_target = KERNEL_START + 0x200000` as the VMA, so all internal addresses are already resolved to the post-relocation location.

---

## Memory Map (QEMU)

```
0x80200000  KERNEL_START  — kernel payload loaded here by bootloader
0x80400000  RELOC_TARGET  — bootloader self-relocates here (KERNEL_START + 0x200000)
0x84000000  INITRD         — initramfs injected by QEMU -initrd flag
```

For OrangePi RV2, replace `0x80` prefix with `0x00` (e.g. `0x00200000`, `0x00400000`). The initrd loads at `0x46100000` as specified in `bootloader/bootloader.its`.

---

## Boot Flow

### QEMU

```
QEMU (-initrd initramfs.cpio)
  │  injects linux,initrd-start into generated DTB
  │  a1 = FDT pointer
  ▼
bootloader/start.S
  │  self-relocates from 0x80200000 → 0x80400000
  │  clears BSS, sets up stack
  │  calls start_kernel(dtb)  [a0 = a1 from OpenSBI]
  ▼
bootloader/main.c
  │  uart_init(dtb)           — UART base from FDT /soc/serial reg
  │  scans UART for BOOT magic → loads kernel to 0x80200000
  │  entry(dtb)
  ▼
kernel/start.S
  │  clears BSS, sets up stack
  │  tail start_kernel(dtb)   [a0 preserved]
  ▼
kernel/main.c
  │  uart_init(dtb)
  │  read_initrd_from_dtb(dtb) — reads linux,initrd-start from /chosen
  │  shell_run(initrd_base)
  ▼
interactive shell
```

### OrangePi RV2 (Board)

```
U-Boot loads bootloader.fit from SD card
  │  FIT image contains: bootloader binary + DTB + initramfs.cpio
  │  U-Boot writes linux,initrd-start into DTB /chosen
  │  a1 = FDT pointer
  ▼
bootloader/start.S
  │  self-relocates from 0x00200000 → 0x00400000
  │  clears BSS, sets up stack
  │  calls start_kernel(dtb)
  ▼
bootloader/main.c
  │  uart_init(dtb)
  │  scans UART for BOOT magic → loads kernel to 0x00200000
  │  entry(dtb)
  ▼
kernel/start.S + kernel/main.c
  │  uart_init(dtb)
  │  read_initrd_from_dtb(dtb) — reads linux,initrd-start from /chosen
  │  shell_run(initrd_base)
  ▼
interactive shell
```

---

## How to Build and Run

### QEMU

```sh
make qemu
# QEMU prints: char device redirected to /dev/pts/3  (number varies)
```

In a second terminal:
```sh
python3 send_kernel.py /dev/pts/3 kernel/payload_os.bin
```

`send_kernel.py` replaces itself with `screen` after transfer.

### OrangePi RV2 (Board)

```sh
make board        # builds everything and flashes bootloader.fit to SD card
# Press Reset on the board, then:
python3 send_kernel.py /dev/tty.usbserial-XXXX kernel/payload_os.bin
```

The `DISK` variable defaults to `/dev/disk4s1`. Override with `make board DISK=/dev/diskXsY`.

---

## Sample Session

```
================================
       UART Bootloader
================================
Waiting for BOOT header...
Loading kernel: 0x00001800 bytes -> 0x00200000
Jumping to kernel...

================================
          OS Kernel
================================
[LOG] Kernel executing at: 0x...
[LOG] UART at: 0xd4017000
[LOG] Device Tree Blob at: 0x...
[LOG] Initramfs at: 0x46100000

opi-rv2> help
Available commands:
  help  - show all commands.
  hello - print 'Hello, world!'.
  info  - show system information.
  ls    - list files in initrd.
  cat   - print file content in initrd.
opi-rv2> ls
Total 3 files.
0       .
971     osc.txt
337     penguin.txt
opi-rv2> cat osc.txt
<contents of osc.txt>
opi-rv2> hello
Hello, world!
```

---

## How to Run (QEMU Debug Mode)

**Terminal 1 — Start QEMU paused:**
```sh
cd bootloader/
make debug
# QEMU prints the PTY path and waits for GDB
```

**Terminal 2 — Attach GDB:**
```sh
gdb-multiarch \
  -ex "set arch riscv:rv64" \
  -ex "file bootloader/bootloader.elf" \
  -ex "target remote :1234"
(gdb) b start_kernel
(gdb) continue
```

**Terminal 3 — Send the kernel:**
```sh
python3 send_kernel.py /dev/pts/X kernel/payload_os.bin
```

---

## Directory Structure

```
lab2/
├── Makefile               # Top-level: make qemu / make board / make clean
├── bootloader/            # Self-relocating UART bootloader
│   ├── main.c             # UART init, BOOT scan, payload jump
│   ├── start.S            # Entry point with self-relocation loop
│   ├── link.ld            # Links bootloader at KERNEL_START + 0x200000
│   ├── bootloader.its     # FIT image spec (bootloader + DTB + initramfs)
│   └── Makefile
│
├── kernel/                # OS Kernel payload (loaded by bootloader)
│   ├── main.c             # UART + initrd init (reads DTB /chosen), calls shell
│   ├── start.S            # BSS clear, stack setup, tail-call start_kernel
│   ├── link.ld            # Links kernel at KERNEL_START
│   └── Makefile           # Outputs payload_os.bin
│
├── lib/                   # Shared drivers (used by both bootloader and kernel)
│   ├── uart.[ch]          # UART driver — base address discovered via FDT
│   ├── fdt.[ch]           # FDT parser: fdt_path_offset, fdt_getprop
│   ├── cpio.[ch]          # CPIO (New ASCII) parser: initrd_list, initrd_cat
│   ├── shell.[ch]         # Interactive shell: help, hello, info, ls, cat
│   └── string.[ch]        # strcmp, strncmp, strlen, memset
│
├── send_kernel.py         # Host-side transfer script with terminal handoff
├── initramfs.cpio         # Initial ramdisk archive
└── x1_orangepi-rv2.dtb    # Devicetree blob for OrangePi RV2
```
