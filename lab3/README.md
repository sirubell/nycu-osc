[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/f9OZrVzc)

# Lab 3 — Memory Allocator

Implementation of a two-tier physical memory allocator for the OrangePi RV2 (RISC-V) kernel, covering a buddy-system page frame allocator, a dynamic chunk allocator, and a startup bump-pointer allocator, with full reserved-memory support via the Device Tree.

---

## Basic Exercise 1 — Buddy System (40%)

Implement a buddy-system page frame allocator that manages physical memory at 4 KiB page granularity.

**Implementation:**

- `kernel/mm.c` maintains a Frame Array (`mem_map[]`) of `struct page` descriptors — one entry per 4 KiB frame — with O(1) lookup via direct index: `phys_to_page(addr)` and `page_to_phys(pg)`.
- 11 free lists (`free_area[0..MAX_ORDER]`, MAX_ORDER = 10) hold heads of free blocks of 2^order pages. Blocks at order 10 span 4 MiB.
- `alloc_pages(order)` searches `free_area[order]` upward for the smallest available block, splits larger blocks downward (releasing the upper half buddy at each step), and inserts remainders into the appropriate free lists — O(log n).
- `free_pages(pg)` resets the page metadata, then iteratively merges with its buddy (found by XORing the PFN with `1 << order`) until the buddy is allocated or order reaches `MAX_ORDER` — O(log n).
- `dump_free_areas()` is called at the end of every `alloc_pages()` and `free_pages()` to print the block count at each order level, making the buddy state observable after every operation.

**Log format:**
```
[FL+] free_area[<n>] <- Page <pfn>. Range: [<pfn>, <pfn+size-1>]
[FL-] free_area[<n>] -> Page <pfn>. Range: [<pfn>, <pfn+size-1>]
[FL Split] Page <pfn> Order <n> -> Two children at order <n-1>
[FL Merge] Page <pfn> + Buddy <buddy_pfn> -> Order <n+1>
[Page Alloc] <addr> Order <n> (page <pfn>) -> Caller
[Page Free]  <addr> Order <n> (page <pfn>) <- Caller
free_area[10] <count>
...
free_area[0] <count>
```

---

## Basic Exercise 2 — Dynamic Memory Allocator (30%)

Implement a dynamic memory allocator (`allocate` / `free`) that builds on top of the Page Frame Allocator for small, variable-size requests.

**Implementation:**

- 10 fixed size classes: `{16, 32, 48, 64, 96, 128, 256, 512, 1024, 2048}` bytes. Each pool is an intrusive free-list (`list.h`) of chunk-sized slots carved from 4 KiB pages.
- `allocate(size)` rounds up to the nearest pool size and pops a slot from the corresponding pool. If the pool is empty, a fresh 4 KiB page is obtained via `alloc_pages(0)`, partitioned into equal chunks, and all slots are added to the pool. Requests larger than 2048 bytes but within `MAX_ALLOC_SIZE` (4 MiB) are served directly by `alloc_pages`.
- `free(ptr)` looks up the owning page's `chunk_size` field. If non-zero, the chunk is returned to its pool. If zero, the page was a direct allocation and is returned via `free_pages`.
- **Slab page reclaim:** `refcount` is repurposed to count idle chunks in each pool page (starts at `PAGE_SIZE / chunk_size`). Each hand-out decrements it; each `free` increments it. When `refcount` reaches `total_chunks` again — meaning every chunk is back in the pool — all chunks from that page are swept out of the pool list and `free_pages()` is called, returning the 4 KiB block to the buddy system.
- Constraints: `1 ≤ size ≤ MAX_ALLOC_SIZE`; `ptr` must be a pointer previously returned by `allocate`.

**Log format:**
```
[Alloc] size=<n> -> page allocator (order <o>)
[Chunk] Pool empty for size <sz>, expanding with new page
[Chunk] Allocate <addr> size=<n> -> chunk <sz>
[Chunk] Free <addr> -> pool (size <sz>)
[Slab] Page <addr> fully empty (<n> x <sz>B chunks) -> returning to buddy
[Free] <addr> -> page allocator
```

---

## Advanced Exercise 1 — Efficient Page Allocation (10%)

Alloc and free a page in O(log n) while ensuring any page frame lookup is O(1).

**Implementation:**

- `page_to_phys(pg)` and `phys_to_page(addr)` are O(1) direct-index operations on `mem_map[]`: `mem_base + (pg - mem_map) * PAGE_SIZE` and `mem_map[(addr - mem_base) >> PAGE_SHIFT]`.
- The buddy XOR trick (`idx ^ (1 << order)`) locates buddies in O(1).
- `alloc_pages` and `free_pages` each iterate at most `MAX_ORDER` times → O(log n).

---

## Advanced Exercise 2 — Reserved Memory (10%)

Transition from a hardcoded allocable region to a fully dynamic layout driven by the Device Tree. Reserve all occupied memory before any allocation.

**Implementation:**

- `parse_memory_regions(fdt)` iterates all `/memory` nodes via `fdt_foreach_memory()` to discover the physical DRAM layout at runtime — no hardcoded addresses. All regions are printed.
- `memory_reserve(start, size)` walks every `free_area` list from `MAX_ORDER` down to 0. For each free block:
  - **No overlap** — leave it in the list.
  - **Full overlap** — set `refcount = 1` (permanently reserved) and remove.
  - **Partial overlap** — split the block into two children at `order - 1` and reinsert both; they will be re-examined at the lower order.
- Called during `mm_init()` for each of the following, all discovered at runtime:
  1. DTB blob — pointer passed by OpenSBI in `a1`; size from FDT header `totalsize` field
  2. Kernel image — `_start` .. `_end` (linker symbols)
  3. Initramfs — `linux,initrd-start` .. `linux,initrd-end` from DTB `/chosen` node
  4. Platform-specific regions — all subnodes of `/reserved-memory` in the DTB

**Log format:**
```
[MM] /memory regions:
  [0] base=<addr>  size=<sz>
  [1] base=<addr>  size=<sz>
[MM] Reserved [<name>]: <start> - <end>
[Reserve] Reserve address [<start>, <end>). Range of pages: [<pfn_start>, <pfn_end>)
```

---

## Advanced Exercise 3 — Startup Allocation (20%)

Bootstrap the Page Frame Allocator's own Frame Array using a dedicated bump-pointer allocator, breaking the chicken-and-egg dependency between the two allocators.

**Implementation:**

- Before the buddy system is initialised, `record_reserved()` tracks all known reserved regions in a small static array (`reserved_regions[]`).
- `first_free_addr(size)` scans all recorded reservations and returns the lowest page-aligned address where a block of `size` bytes fits without overlap — a bump-pointer search used exactly once. Panics if no room is found.
- The Frame Array is placed at that address: `mem_map = (struct page *)first_free_addr(fa_bytes)`, zero-initialised with `memset`, and immediately added to the reserved list so the buddy system will never hand it out.
- The startup allocator is used only during early boot; once the buddy system is live, all further allocations go through `alloc_pages`.

---

## Memory Layout

### QEMU (`-M virt -m 8G`)

```
0x80000000  mem_base / KERNEL_START  — kernel binary loaded here by bootloader
            _end                     — end of kernel image (link-time symbol)
            Frame Array              — bump-allocated immediately after _end
            free pages               — managed by buddy system
0x84000000  initramfs                — injected by QEMU -initrd flag
            DTB                      — generated by QEMU, passed in a1
```

### OrangePi RV2 (Board)

The board has 8 GiB DDR split across two physical memory regions with a 2 GiB hole
between them. Both regions are managed using a flat PFN space; the hole is permanently
reserved so the buddy system never allocates it.

```
── Region [0]: 0x000000000 – 0x080000000 (2 GiB) ──────────────────────────────
0x000000000  mem_base                — first frame in Frame Array (PFN 0)
0x000080000  mmode_resv0             — OpenSBI/M-mode reserved region
0x000200000  KERNEL_START            — kernel binary loaded here by bootloader
             _end                    — end of kernel image
0x000209000  Frame Array             — bump-allocated (~80 MiB, covers full span)
0x05209000   free pages              — region [0] managed by buddy system
0x02ff40000  dpu_reserved            — DPU reserved region (from /reserved-memory)
0x030000000  rcpu_mem regions        — RCPU heap, vrings, buffers (from /reserved-memory)
~0x07dd00000 DTB + initramfs         — loaded by U-Boot (runtime address)
0x07f000000  framebuffer             — reserved by /reserved-memory
0x080000000  end of region [0]

── Hole: 0x080000000 – 0x100000000 (2 GiB, reserved) ──────────────────────────
             hole pages              — refcount=1, never allocated

── Region [1]: 0x100000000 – 0x280000000 (6 GiB) ──────────────────────────────
0x100000000  free pages              — region [1] managed by buddy system (PFN 1048576)
0x280000000  end of region [1]
```

---

## Boot and Init Flow

```
OpenSBI
  │  a1 = pointer to Flattened Device Tree (FDT/DTB)
  ▼
kernel/start.S
  │  clears BSS, sets up stack
  │  tail start_kernel(dtb)
  ▼
kernel/main.c  start_kernel()
  │  uart_init(dtb)           — UART base from DTB /soc/serial reg
  │  (waits for Enter key)    — ensures terminal is ready before printing
  │  mm_init(dtb)
  │    parse_memory_regions() — all /memory regions, computes span, records holes [Adv Ex 2]
  │    record_reserved() ×4   — DTB, kernel image, initrd, /reserved-memory      [Adv Ex 2]
  │    first_free_addr()      — bump-pointer locates space for Frame Array  [Adv Ex 3]
  │    memset(mem_map, 0)     — zero-initialise all struct page descriptors
  │    free_area[] seeded     — MAX_ORDER blocks cover all managed pages    [Basic Ex 1]
  │    memory_reserve() ×N    — splits/marks all reserved ranges in buddy   [Adv Ex 2]
  │    chunk_pool[] zeroed    — pool free-lists initialised empty           [Basic Ex 2]
  │  test_alloc_1()           — allocator smoke test (required for demo)
  │  shell_run(dtb)
  ▼
interactive shell
```

---

## How to Build and Run

### QEMU

```sh
make qemu
# QEMU prints: char device redirected to /dev/pts/X
```

In a second terminal, connect and send the kernel:
```sh
uv run send_kernel.py /dev/pts/X kernel/payload_os.bin
```

After transfer completes, `send_kernel.py` hands off to `screen`. Press **Enter** once to trigger the kernel banner.

### OrangePi RV2 (Board)

```sh
make board        # builds kernel/payload_os.bin
# Press Reset on the board, then:
uv run send_kernel.py /dev/ttyUSB0 kernel/payload_os.bin
```

Press **Enter** in `screen` after the kernel loads to trigger the banner.

---

## Sample Output (OrangePi RV2)

```
================================
       OS Kernel  (Lab 3)
================================

[MM] Initializing memory manager...
[MM] /memory regions:
  [0] base=0x0000000000000000  size=0x0000000080000000
  [1] base=0x0000000100000000  size=0x0000000180000000
[MM] mem_base=0x0000000000000000  span=0x0000000280000000  total_pages=2621440  usable_pages=2097152
[MM] Reserved [hole]: 0x0000000080000000 - 0x0000000100000000
[MM] Reserved [dtb]: 0x000000007dd70000 - 0x000000007dd8d000
[MM] Reserved [kernel]: 0x0000000000200000 - 0x0000000000209000
[MM] Reserved [initramfs]: 0x000000007dd8f000 - 0x000000007dd90000
[MM] Reserved [framebuffer@7f000000]: 0x000000007f000000 - 0x0000000080000000
[MM] Reserved [mmode_resv0@0]: 0x0000000000000000 - 0x0000000000080000
[MM] Reserved [rcpu_mem_heap@30000000]: 0x0000000030000000 - 0x0000000030200000
...
[MM] Frame array: 0x0000000000209000 - 0x0000000005209000
[MM] Reserved [frame-array]: 0x0000000000209000 - 0x0000000005209000
[Reserve] Reserve address [0x000000007dd70000, 0x000000007dd8d000). Range of pages: [515440, 515469)
[FL-] free_area[10] -> Page 515072. Range: [515072, 516095]
[FL Split] Page 515072 Order 10 -> Two children at order 9
...
[MM] Memory manager ready.

Testing memory allocation...
[Alloc] size=4000 -> page allocator (order 0)
[FL-] free_area[0] -> Page 515469. Range: [515469, 515469]
[Page Alloc] 0x000000007dd8d000 Order 0 (page 515469) -> Caller
free_area[10] 500
...
free_area[0] 2
[Free] 0x000000007dd8d000 -> page allocator
[Page Free] 0x000000007dd8d000 Order 0 (page 515469) <- Caller
[FL+] free_area[0] <- Page 515469. Range: [515469, 515469]
...

Testing dynamic allocator...
[Chunk] Pool empty for size 128, expanding with new page
[FL-] free_area[0] -> Page 4617. Range: [4617, 4617]
[Page Alloc] 0x0000000001209000 Order 0 (page 4617) -> Caller
[Chunk] Allocate 0x0000000001209000 size=128 -> chunk 128
[Chunk] Free 0x0000000001209000 -> pool (size 128)
[Slab] Page 0x0000000001209000 fully empty (32 x 128B chunks) -> returning to buddy
[Page Free] 0x0000000001209000 Order 0 (page 4617) <- Caller
[FL Merge] Page 4618 + Buddy 4619 -> Order 1
[FL-] free_area[0] -> Page 4619. Range: [4619, 4619]
[FL+] free_area[1] <- Page 4618. Range: [4618, 4619]
...
Allocation failed as expected for size > MAX_ALLOC_SIZE
opi-rv2>
```

---

## Bonus — All Physical Memory Regions (flat PFN space with holes)

Extends the memory manager to handle all usable memory regions reported by the DTB,
rather than only the first one.

**Approach:** flat PFN address space with holes (Approach B).

- `mem_size` is now the full **span** from `mem_base` to the end of the last region, including any holes between regions.
- The Frame Array (`mem_map[]`) covers the entire span — hole entries are allocated but permanently marked `refcount = 1`, so they never enter any free list and buddy merges across the hole are naturally blocked.
- Holes between regions are detected in `parse_memory_regions()` and recorded as reserved. The existing `memory_reserve()` loop at buddy init time marks their pages automatically — no changes to buddy logic needed.
- `page_to_phys` / `phys_to_page` remain O(1) with the same formula.
- On the OrangePi RV2: manages 2 GiB (region [0]) + 6 GiB (region [1]) = **8 GiB total**, with a 2 GiB hole reserved at 0x80000000–0x100000000. Frame array is ~80 MiB.

---

## Directory Structure

```
lab3/
├── Makefile               # Top-level: make qemu / make board / make clean
│
├── bootloader/            # UART bootloader (unchanged from Lab 2)
│   ├── main.c             # UART init, BOOT magic scan, payload jump
│   ├── start.S            # Entry point with self-relocation loop
│   ├── link.ld            # Links bootloader at KERNEL_START + 0x200000
│   ├── bootloader.its     # FIT image spec (bootloader + DTB + initramfs)
│   ├── initramfs.cpio     # Initial ramdisk (bundled into bootloader.fit)
│   ├── x1_orangepi-rv2.dtb  # Devicetree blob for OrangePi RV2
│   └── Makefile
│
├── kernel/                # OS Kernel payload
│   ├── main.c             # Entry point: uart_init, mm_init, test, shell
│   ├── mm.c               # Buddy system + chunk allocator + slab reclaim
│   ├── mm.h               # Public API: alloc_pages, free_pages, allocate, free
│   ├── shell.[ch]         # Interactive shell: help, hello, info, ls, cat
│   ├── start.S            # BSS clear, stack setup
│   └── link.ld
│
├── lib/                   # Shared drivers and utilities
│   ├── uart.[ch]          # UART driver — base address discovered via FDT
│   ├── fdt.[ch]           # FDT parser: fdt_getprop, fdt_get_chosen_addr, fdt_foreach_memory, fdt_foreach_reserved_memory
│   ├── list.h             # Intrusive doubly-linked list (Linux-style list_head)
│   ├── cpio.[ch]          # CPIO (New ASCII) parser: initrd_list, initrd_cat
│   ├── string.[ch]        # strcmp, strncmp, strlen, memset
│   └── utils.[ch]         # panic() — print message to UART and halt
│
└── send_kernel.py         # Host-side UART transfer script
```
