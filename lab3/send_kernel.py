#!/usr/bin/env python3
"""
send_kernel.py — UART kernel loader.

Stage 1 (Wait):     Monitors serial output until the bootloader prints
                    "Waiting for BOOT header", then proceeds automatically.
                    Ctrl+C skips the wait and sends immediately.
Stage 2 (Transfer): Sends BOOT magic header + kernel payload.
Stage 3 (Handoff):  Replaces this Python process with screen.

Usage:
  python3 send_kernel.py <serial_port> <kernel.bin>
"""

import os
import select
import struct
import sys
import termios
import time
import tty


BOOTLOADER_READY_STR = b"Waiting for BOOT"
KERNEL_JUMP_STR = b"Jumping to kernel"
WAIT_TIMEOUT = 15  # seconds to wait for bootloader before giving up
JUMP_TIMEOUT = 30  # seconds to wait for "Jumping to kernel" after transfer


def stream(tty_file, data, chunk_size=128, delay=0.002):
    for i in range(0, len(data), chunk_size):
        tty_file.write(data[i : i + chunk_size])
        time.sleep(delay)


def wait_for_bootloader(fd):
    """Read and display serial output until the bootloader is ready."""
    print("[*] Waiting for bootloader... (Ctrl+C to skip)")
    buf = b""
    deadline = time.time() + WAIT_TIMEOUT
    try:
        while time.time() < deadline:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                data = os.read(fd, 256)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.flush()
                    buf += data
                    if BOOTLOADER_READY_STR in buf:
                        print()  # newline after bootloader output
                        return
                    # Keep only the last 64 bytes to search for the marker
                    buf = buf[-64:]
        print("\n[!] Timed out waiting for bootloader, sending anyway...")
    except KeyboardInterrupt:
        print("\n[*] Skipping wait, sending now...")


def send_kernel(tty_path, kernel_path):
    # --- Load files -----------------------------------------------------------
    try:
        with open(kernel_path, "rb") as f:
            kernel_data = f.read()
    except FileNotFoundError:
        print(f"Error: kernel image '{kernel_path}' not found.")
        sys.exit(1)

    print(f"[*] Kernel:   {len(kernel_data)} bytes")

    # Protocol header (little-endian magic + LE uint32 size)
    boot_header = struct.pack("<4sI", b"BOOT", len(kernel_data))

    # --- Stage 1 + 2: Wait then Transfer --------------------------------------
    print(f"[*] Opening {tty_path}...")
    try:
        with open(tty_path, "r+b", buffering=0) as tty_file:
            fd = tty_file.fileno()
            old_settings = termios.tcgetattr(fd)
            try:
                # Set baud rate to 115200 explicitly — macOS may reset to 9600 on open
                attrs = termios.tcgetattr(fd)
                attrs[4] = termios.B115200  # input speed
                attrs[5] = termios.B115200  # output speed
                termios.tcsetattr(fd, termios.TCSANOW, attrs)

                tty.setraw(fd)

                wait_for_bootloader(fd)

                print("[*] Sending BOOT header + kernel...")
                tty_file.write(boot_header)
                time.sleep(0.5)  # let bootloader print its status line
                stream(tty_file, kernel_data)

                # Stage 2b: keep reading until bootloader says "Jumping to kernel"
                # so the user can see "Loading kernel/initrd" messages in this terminal.
                print("[*] Waiting for bootloader to jump to kernel...")
                buf = b""
                deadline = time.time() + JUMP_TIMEOUT
                while time.time() < deadline:
                    r, _, _ = select.select([fd], [], [], 0.1)
                    if r:
                        data = os.read(fd, 256)
                        if data:
                            sys.stdout.buffer.write(data)
                            sys.stdout.flush()
                            buf += data
                            if KERNEL_JUMP_STR in buf:
                                print()
                                break
                            buf = buf[-64:]

                print("[+] Transfer complete.")
            finally:
                termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    except OSError as e:
        print(f"Error: {e}")
        sys.exit(1)

    # --- Stage 3: Handoff to screen -------------------------------------------
    # Brief pause to let the OS release the TTY before screen opens.
    time.sleep(0.1)
    logfile = os.path.splitext(os.path.basename(kernel_path))[0] + ".log"
    print(f"[*] Handing off to screen on {tty_path} (log: {logfile})...\n")
    # Write a temporary screenrc so the old screen 4.x (macOS built-in) can set
    # the logfile path — it predates the -Logfile flag added in screen 4.6+.
    screenrc = f"/tmp/send_kernel_screenrc"
    with open(screenrc, "w") as f:
        # Use just the filename (no path) — screen's config parser does not
        # handle spaces in paths, so an absolute path like
        # "/Users/.../Operating Systems Capstone (OSC)/..." would be silently
        # ignored, falling back to the default "screenlog.0".
        # A bare filename is resolved relative to screen's CWD, which is
        # inherited from this Python process.
        f.write(f"logfile {logfile}\n")
        f.write("defscrollback 5000\n")
    os.execvp("screen", ["screen", "-L", "-c", screenrc, tty_path, "115200"])


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 send_kernel.py <serial_port> <kernel.bin>")
        print(
            "  e.g. python3 send_kernel.py /dev/cu.usbserial-0001 kernel/payload_os.bin"
        )
        sys.exit(1)
    send_kernel(sys.argv[1], sys.argv[2])
