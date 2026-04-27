#ifndef MM_H
#define MM_H

#include <stddef.h>
#include <stdint.h>

#include "list.h"

/* ── Constants ───────────────────────────────────────────────────────────────
 */

/* Round x up to the nearest multiple of a (a must be a power of two). */
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))
/* Round x down to the nearest multiple of a (a must be a power of two). */
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

#define PAGE_SIZE 4096ULL
#define PAGE_SHIFT 12
#define MAX_ORDER 10 /* max block = 2^10 pages = 4 MiB */

/* Physical load address for user programs copied from initramfs. */
#ifdef QEMU
#define USER_LOAD_ADDR 0x80400000UL
#else
#define USER_LOAD_ADDR 0x00400000UL
#endif

/*
 * Pool sizes for the dynamic chunk allocator.
 * Requests > the largest pool (but <= MAX_ALLOC_SIZE) are served by the page
 * frame allocator directly.
 */
#define NUM_POOLS 10
#define MAX_ALLOC_SIZE ((uint64_t)(1 << MAX_ORDER) * PAGE_SIZE)

/* ── Page frame descriptor ───────────────────────────────────────────────────
 */

/*
 * Frame Array (mem_map[]):  one entry per 4 KiB page frame in the first memory
 * region reported by the Device Tree.
 *
 * order    : when refcount == 0 and the page is the head of a free block, this
 *            is the block's order (0 .. MAX_ORDER).  When refcount == 1
 *            (allocated), order stores the allocation order so free_pages()
 *            knows the block size.
 *
 *            Inner pages that belong to a larger free block are never directly
 *            accessible via any free_area list; their order field is 0 from
 *            the initial memset and is only meaningful once the page is split
 *            out and explicitly placed on a free_area list.
 *
 * refcount : 0 = free  /  1 = allocated or permanently reserved
 *
 * chunk_size : 0  → full-page allocation or free page.
 *              > 0 → this page has been partitioned into fixed-size chunks of
 *                    this byte size for the dynamic memory allocator.
 *
 * _pad     : explicit padding so that list_head (two 8-byte pointers on RV64)
 *            starts at an 8-byte-aligned offset within the struct.
 *
 * list     : intrusive list node; linked into free_area[order] when the page
 *            is the head of a free block.
 */
struct page {
    uint32_t order;
    uint32_t refcount;
    uint32_t chunk_size;
    uint32_t _pad; /* keeps list_head 8-byte aligned (3×4 B + 4 B pad = 16 B) */
    struct list_head list;
};

/* ── Public API ──────────────────────────────────────────────────────────────
 */

/*
 * mm_init  – one-shot memory-manager bootstrap.
 *
 * Performs, in order:
 *   1. Parse the Device Tree to discover the first physical memory region.
 *   2. Record all reserved regions (DTB, kernel, initramfs, /reserved-memory).
 *   3. Run the startup (bump-pointer) allocator to carve out the Frame Array.
 *   4. Initialise the Buddy System with the remaining free pages.
 *   5. Initialise the chunk pool free-lists.
 *
 * Must be called before alloc_pages(), free_pages(), allocate(), or free().
 */
void mm_init(const void *fdt);

/* ── Buddy System (Page Frame Allocator) ────────────────────────────────────
 */

/*
 * alloc_pages  – allocate 2^order contiguous page frames.
 *
 * Returns a pointer to the struct page for the first frame, or NULL on
 * failure.  The returned block is 4 KiB * 2^order bytes long and is
 * 4 KiB * 2^order–aligned in physical memory.
 *
 * Complexity: O(log n) in the number of managed page frames.
 */
struct page *alloc_pages(uint32_t order);

/*
 * free_pages  – release a block previously obtained from alloc_pages().
 *
 * Merges with the buddy block iteratively until no further merge is possible
 * (buddy is allocated or at MAX_ORDER).
 *
 * Complexity: O(log n).
 */
void free_pages(struct page *pg);

/*
 * memory_reserve  – mark a physical address range as occupied so the buddy
 * system will never allocate it.
 *
 * Called during mm_init() for each reserved region, and may be called again
 * later if new firmware-reserved regions are discovered.
 *
 * start : physical byte address (need not be page-aligned; will be rounded)
 * size  : byte length of the region
 */
void memory_reserve(uint64_t start, uint64_t size);

/*
 * page_to_phys  – O(1) conversion: struct page * → physical byte address.
 */
uint64_t page_to_phys(const struct page *p);

/*
 * phys_to_page  – O(1) conversion: physical byte address → struct page *.
 */
struct page *phys_to_page(uint64_t phys);

/* ── Dynamic Memory Allocator (Chunk Pools) ─────────────────────────────────
 */

/*
 * allocate  – allocate a contiguous block of at least 'size' bytes.
 *
 * For small requests (≤ the largest pool size) the request is rounded up to
 * the nearest pool size and served from a pre-partitioned 4 KiB page.
 * For larger requests a whole number of page frames is allocated via the
 * buddy system and the physical base address is returned directly.
 *
 * Returns NULL if size == 0, size > MAX_ALLOC_SIZE, or memory is exhausted.
 */
void *allocate(uint64_t size);

/*
 * free  – release a pointer previously returned by allocate().
 */
void free(void *ptr);

#endif /* MM_H */
