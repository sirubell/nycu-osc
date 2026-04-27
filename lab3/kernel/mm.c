#include "mm.h"
#include "fdt.h"
#include "string.h"
#include "uart.h"
#include "utils.h"

/* ── Linker-script symbols ───────────────────────────────────────────────────
 */
extern char _start[]; /* first byte of kernel image */
extern char _end[];   /* one past the last byte (including stack) */

/* ── Physical memory layout (filled by mm_init) ──────────────────────────────
 */
static uint64_t mem_base; /* lowest physical address managed */
static uint64_t mem_size; /* span: mem_end - mem_base (includes holes) */
static uint64_t total_pages;
static uint64_t usable_pages; /* sum of all region sizes, excluding holes */

#define MAX_MEM_REGIONS 8
static struct {
    uint64_t base;
    uint64_t size;
} mem_regions[MAX_MEM_REGIONS];
static int num_mem_regions;

/* ── Frame Array ─────────────────────────────────────────────────────────────
 */
static struct page *mem_map;

/* ── Buddy system free lists ──────────────────────────────────────────────────
 */
static struct list_head free_area[MAX_ORDER + 1];

/* ── Chunk pools ──────────────────────────────────────────────────────────────
 */
static const uint32_t pool_sizes[NUM_POOLS] = {16,  32,  48,  64,   96,
                                               128, 256, 512, 1024, 2048};
static struct list_head chunk_pool[NUM_POOLS];

/* ── Reserved-region tracking (startup allocator) ────────────────────────────
 */
#define MAX_RESERVED 32

static struct reserved_entry {
    uint64_t start;
    uint64_t end;
} reserved_regions[MAX_RESERVED];
static int num_reserved;

static void record_reserved(const char *name, uint64_t start, uint64_t end) {
    if (num_reserved >= MAX_RESERVED)
        return;
    /* page-align outward */
    start = ALIGN_DOWN(start, PAGE_SIZE);
    end = ALIGN_UP(end, PAGE_SIZE);
    uart_puts("[MM] Reserved [");
    uart_puts(name);
    uart_puts("]: ");
    uart_hex(start);
    uart_puts(" - ");
    uart_hex(end);
    uart_puts("\n");
    reserved_regions[num_reserved].start = start;
    reserved_regions[num_reserved].end = end;
    num_reserved++;
}

/* Return the first PAGE_SIZE-aligned address where a block of 'size' bytes
 * fits without overlapping any recorded reserved region. */
static uint64_t first_free_addr(uint64_t size) {
    uint64_t addr = mem_base;
    int restarted = 1;
    while (restarted) {
        restarted = 0;
        for (int i = 0; i < num_reserved; i++) {
            /* Does [addr, addr+size) overlap [start, end)? */
            if (addr + size > reserved_regions[i].start &&
                addr < reserved_regions[i].end) {
                addr = reserved_regions[i].end;
                restarted = 1;
            }
        }
    }
    if (addr + size > mem_base + mem_size)
        panic("mm: no room for frame array");
    return addr;
}

/* ── FDT parsing ──────────────────────────────────────────────────────────────
 */

static void on_memory_region(const char *name, uint64_t base, uint64_t size,
                             void *data) {
    (void)name;
    (void)data;
    uart_puts("  [");
    uart_put_u64((uint64_t)num_mem_regions);
    uart_puts("] base=");
    uart_hex(base);
    uart_puts("  size=");
    uart_hex(size);
    uart_puts("\n");

    if (num_mem_regions < MAX_MEM_REGIONS) {
        mem_regions[num_mem_regions].base = base;
        mem_regions[num_mem_regions].size = size;
        num_mem_regions++;
    }
}

static void parse_memory_regions(const void *fdt) {
    uart_puts("[MM] /memory regions:\n");
    fdt_foreach_memory(fdt, on_memory_region, 0);

    if (num_mem_regions == 0)
        panic("mm: no memory regions found in DTB");

    /* mem_base = start of first region (assumed lowest). */
    mem_base = mem_regions[0].base;

    /* mem_size = span from mem_base to end of last region (includes holes). */
    uint64_t mem_end = 0;
    for (int i = 0; i < num_mem_regions; i++) {
        uint64_t end = mem_regions[i].base + mem_regions[i].size;
        if (end > mem_end)
            mem_end = end;
        usable_pages += mem_regions[i].size >> PAGE_SHIFT;
    }
    mem_size = mem_end - mem_base;

    /* Record holes between regions so the startup allocator avoids them. */
    for (int i = 0; i + 1 < num_mem_regions; i++) {
        uint64_t hole_start = mem_regions[i].base + mem_regions[i].size;
        uint64_t hole_end = mem_regions[i + 1].base;
        if (hole_end > hole_start)
            record_reserved("hole", hole_start, hole_end);
    }
}

/* Callback for fdt_foreach_reserved_memory → record_reserved. */
static void on_reserved_region(const char *name, uint64_t start, uint64_t size,
                               void *data) {
    (void)data;
    record_reserved(name, start, start + size);
}

/* ── Log helpers ──────────────────────────────────────────────────────────────
 */

/* Page enters buddy free list (free_area[order]). */
static void log_page_add(uint64_t pfn, uint32_t order) {
    uart_puts("[FL+] free_area[");
    uart_put_u64(order);
    uart_puts("] <- Page ");
    uart_put_u64(pfn);
    uart_puts(". Range: [");
    uart_put_u64(pfn);
    uart_puts(", ");
    uart_put_u64(pfn + (1ULL << order) - 1);
    uart_puts("]\n");
}

/* Page leaves buddy free list. */
static void log_page_remove(uint64_t pfn, uint32_t order) {
    uart_puts("[FL-] free_area[");
    uart_put_u64(order);
    uart_puts("] -> Page ");
    uart_put_u64(pfn);
    uart_puts(". Range: [");
    uart_put_u64(pfn);
    uart_puts(", ");
    uart_put_u64(pfn + (1ULL << order) - 1);
    uart_puts("]\n");
}

/* Page handed to caller by alloc_pages(). */
static void log_page_alloc(struct page *pg, uint32_t order) {
    uart_puts("[Page Alloc] ");
    uart_hex(page_to_phys(pg));
    uart_puts(" Order ");
    uart_put_u64(order);
    uart_puts(" (page ");
    uart_put_u64((uint64_t)(pg - mem_map));
    uart_puts(") -> Caller\n");
}

/* Page returned from caller to free_pages(). */
static void log_page_free(struct page *pg, uint32_t order) {
    uart_puts("[Page Free] ");
    uart_hex(page_to_phys(pg));
    uart_puts(" Order ");
    uart_put_u64(order);
    uart_puts(" (page ");
    uart_put_u64((uint64_t)(pg - mem_map));
    uart_puts(") <- Caller\n");
}

static void log_buddy_split(uint64_t pfn, uint32_t order) {
    uart_puts("[FL Split] Page ");
    uart_put_u64(pfn);
    uart_puts(" Order ");
    uart_put_u64(order);
    uart_puts(" -> Two children at order ");
    uart_put_u64(order - 1);
    uart_puts("\n");
}

static void log_buddy_merge(uint64_t buddy_pfn, uint64_t pg_pfn,
                            uint32_t order) {
    uart_puts("[FL Merge] Page ");
    uart_put_u64(pg_pfn);
    uart_puts(" + Buddy ");
    uart_put_u64(buddy_pfn);
    uart_puts(" -> Order ");
    uart_put_u64(order + 1);
    uart_puts("\n");
}

static void log_reserve_range(uint64_t start_pfn, uint64_t end_pfn) {
    uart_puts("[Reserve] Reserve address [");
    uart_hex(mem_base + start_pfn * PAGE_SIZE);
    uart_puts(", ");
    uart_hex(mem_base + end_pfn * PAGE_SIZE);
    uart_puts("). Range of pages: [");
    uart_put_u64(start_pfn);
    uart_puts(", ");
    uart_put_u64(end_pfn);
    uart_puts(")\n");
}

static void log_alloc_pages_path(uint64_t size, uint32_t order) {
    uart_puts("[Alloc] size=");
    uart_put_u64(size);
    uart_puts(" -> page allocator (order ");
    uart_put_u64(order);
    uart_puts(")\n");
}

static void log_chunk_expand(uint32_t csz) {
    uart_puts("[Chunk] Pool empty for size ");
    uart_put_u64(csz);
    uart_puts(", expanding with new page\n");
}

static void log_chunk_alloc(uint64_t addr, uint64_t size, uint32_t chunk_size) {
    uart_puts("[Chunk] Allocate ");
    uart_hex(addr);
    uart_puts(" size=");
    uart_put_u64(size);
    uart_puts(" -> chunk ");
    uart_put_u64(chunk_size);
    uart_puts("\n");
}

static void log_chunk_free(uint64_t addr, uint32_t csz) {
    uart_puts("[Chunk] Free ");
    uart_hex(addr);
    uart_puts(" -> pool (size ");
    uart_put_u64(csz);
    uart_puts(")\n");
}

static void log_free_pages_path(uint64_t addr) {
    uart_puts("[Free] ");
    uart_hex(addr);
    uart_puts(" -> page allocator\n");
}

static void log_slab_reclaim(uint64_t page_base, uint32_t csz,
                             uint32_t total_chunks) {
    uart_puts("[Slab] Page ");
    uart_hex(page_base);
    uart_puts(" fully empty (");
    uart_put_u64(total_chunks);
    uart_puts(" x ");
    uart_put_u64(csz);
    uart_puts("B chunks) -> returning to buddy\n");
}

/* ── Buddy state dump ─────────────────────────────────────────────────────────
 *
 * Prints the number of free blocks at each order level, highest first:
 *   free_area[10] 2037
 *   free_area[9]  0
 *   ...
 *
 * Called at the end of every alloc_pages() and free_pages() so the free-list
 * state is visible after every buddy operation.
 */
static void dump_free_areas(void) {
    for (int i = MAX_ORDER; i >= 0; i--) {
        uint64_t count = 0;
        struct list_head *pos, *tmp;
        list_for_each_safe(pos, tmp, &free_area[i]) count++;
        uint64_t block_kb = (PAGE_SIZE << i) / 1024;
        uart_puts("free_area[");
        uart_put_u64((uint64_t)i);
        uart_puts("] ");
        uart_put_u64(count);
        uart_puts(" blocks x ");
        uart_put_u64(block_kb);
        uart_puts(" KiB = ");
        uart_put_u64(count * block_kb);
        uart_puts(" KiB\n");
    }
}

/* ── Buddy internals ──────────────────────────────────────────────────────────
 */

/* O(1): index XOR gives buddy PFN; direct array access gives struct page *. */
static struct page *get_buddy(struct page *pg, uint32_t order) {
    uint64_t idx = (uint64_t)(pg - mem_map);
    return &mem_map[idx ^ (1ULL << order)];
}

/* ── Public: page_to_phys / phys_to_page ──────────────────────────────────────
 */

uint64_t page_to_phys(const struct page *p) {
    return mem_base + (uint64_t)(p - mem_map) * PAGE_SIZE;
}

struct page *phys_to_page(uint64_t phys) {
    return &mem_map[(phys - mem_base) >> PAGE_SHIFT];
}

/* ── Public: alloc_pages ──────────────────────────────────────────────────────
 */

struct page *alloc_pages(uint32_t order) {
    /* Find the smallest free block of order >= requested */
    uint32_t i = order;
    while (i <= MAX_ORDER && list_empty(&free_area[i]))
        i++;
    if (i > MAX_ORDER)
        return NULL;

    /* Remove head block from free list */
    struct page *pg = list_first_entry(&free_area[i], struct page, list);
    list_del(&pg->list);
    log_page_remove((uint64_t)(pg - mem_map), i);

    /* Split down, releasing the upper half (buddy) at each step */
    while (i > order) {
        i--;
        struct page *buddy = get_buddy(pg, i);
        buddy->order = i;
        buddy->refcount = 0;
        list_add_tail(&buddy->list, &free_area[i]);
        log_page_add((uint64_t)(buddy - mem_map), i);
    }

    pg->order = order;
    pg->refcount = 1;
    log_page_alloc(pg, order);

    dump_free_areas();
    return pg;
}

/* ── Public: free_pages ───────────────────────────────────────────────────────
 */

void free_pages(struct page *pg) {
    pg->refcount = 0;
    pg->chunk_size = 0;
    uint32_t order = pg->order;
    log_page_free(pg, order);

    /* Iteratively merge with buddy (O(log n) total) */
    while (order < MAX_ORDER) {
        struct page *buddy = get_buddy(pg, order);
        uint64_t buddy_pfn = (uint64_t)(buddy - mem_map);

        /* Merge only if buddy is: in range, free (not slab), and head of
         * same-order block.  chunk_size != 0 means the buddy is a slab page
         * whose refcount may be 0 (all chunks in caller's hands) even though
         * it is NOT free — without this guard we would corrupt the list. */
        if (buddy_pfn >= total_pages || buddy->refcount != 0 ||
            buddy->chunk_size != 0 || buddy->order != order)
            break;

        log_buddy_merge(buddy_pfn, (uint64_t)(pg - mem_map), order);
        list_del(&buddy->list);
        log_page_remove(buddy_pfn, order);

        if (buddy < pg)
            pg = buddy;
        order++;
    }

    pg->order = order;
    list_add_tail(&pg->list, &free_area[order]);
    log_page_add((uint64_t)(pg - mem_map), order);

    dump_free_areas();
}

/* ── Public: memory_reserve ───────────────────────────────────────────────────
 */

void memory_reserve(uint64_t start, uint64_t size) {
    if (size == 0 || !mem_map)
        return;

    /* Convert to PFNs relative to mem_base */
    if (start < mem_base) {
        if (start + size <= mem_base)
            return;
        size -= (mem_base - start);
        start = mem_base;
    }

    uint64_t start_pfn = (start - mem_base) >> PAGE_SHIFT;
    uint64_t end_pfn =
        ALIGN_UP(start - mem_base + size, PAGE_SIZE) >> PAGE_SHIFT;

    if (start_pfn >= total_pages)
        return;
    if (end_pfn > total_pages)
        end_pfn = total_pages;

    log_reserve_range(start_pfn, end_pfn);

    /*
     * Scan free_area from highest to lowest order.
     * Blocks that overlap the reserved range are split or marked reserved.
     * Split children at order-1 will be processed when that order is reached.
     */
    for (int ord = MAX_ORDER; ord >= 0; ord--) {
        struct list_head *pos, *tmp;
        list_for_each_safe(pos, tmp, &free_area[ord]) {
            struct page *blk = list_entry(pos, struct page, list);
            uint64_t blk_pfn = (uint64_t)(blk - mem_map);
            uint64_t blk_end = blk_pfn + (1ULL << ord);

            if (blk_end <= start_pfn || blk_pfn >= end_pfn)
                continue; /* no overlap */

            list_del(&blk->list);
            log_page_remove(blk_pfn, (uint32_t)ord);

            if (blk_pfn >= start_pfn && blk_end <= end_pfn) {
                /* Fully contained – mark as reserved/allocated */
                blk->refcount = 1;
            } else {
                /* Partial overlap – split into two children at ord-1 */
                log_buddy_split(blk_pfn, (uint32_t)ord);
                struct page *child2 = get_buddy(blk, (uint32_t)(ord - 1));
                blk->order = (uint32_t)(ord - 1);
                blk->refcount = 0;
                child2->order = (uint32_t)(ord - 1);
                child2->refcount = 0;
                list_add_tail(&blk->list, &free_area[ord - 1]);
                list_add_tail(&child2->list, &free_area[ord - 1]);
                log_page_add((uint64_t)(blk - mem_map), (uint32_t)(ord - 1));
                log_page_add((uint64_t)(child2 - mem_map), (uint32_t)(ord - 1));
            }
        }
    }
}

/* ── Dynamic allocator (chunk pools + page allocator)
 * ────────────────────────── */

/* Index of the smallest pool that fits 'size', or -1 for page-allocator path.
 */
static int pool_index(uint64_t size) {
    for (int i = 0; i < NUM_POOLS; i++)
        if (size <= (uint64_t)pool_sizes[i])
            return i;
    return -1;
}

void *allocate(uint64_t size) {
    if (size == 0 || size > MAX_ALLOC_SIZE)
        return NULL;

    int pi = pool_index(size);

    if (pi >= 0) {
        /* ── Chunk pool path ── */
        if (list_empty(&chunk_pool[pi])) {
            log_chunk_expand(pool_sizes[pi]);
            /* Carve a fresh 4 KiB page into fixed-size chunks */
            struct page *pg = alloc_pages(0);
            if (!pg)
                return NULL;
            uint64_t base = page_to_phys(pg);
            uint32_t csz = pool_sizes[pi];
            pg->chunk_size = csz;

            /*
             * Slab refcount initialisation:
             * alloc_pages() left pg->refcount = 1 (allocated).  We repurpose
             * refcount for the slab layer: it counts how many chunks from this
             * page have NOT yet been handed to a caller (i.e. are either still
             * sitting in chunk_pool or have been returned by free()).
             *
             * Invariant:
             *   refcount == total_chunks  →  page is fully idle; reclaim it.
             *   refcount == 0             →  every chunk is in a caller's
             *                                hands.
             *
             * We set refcount = total_chunks now (before any chunk is popped)
             * and decrement by 1 each time we return a chunk to the caller.
             */
            uint32_t total_chunks = (uint32_t)(PAGE_SIZE / csz);
            pg->refcount = total_chunks;

            /* Use the chunk memory itself as list_head nodes (min chunk = 16 B
             * == sizeof(struct list_head), so this is always safe). */
            for (uint64_t off = 0; off + csz <= PAGE_SIZE; off += csz) {
                struct list_head *node = (struct list_head *)(base + off);
                list_init(node);
                list_add_tail(node, &chunk_pool[pi]);
            }
        }

        /* Take first free chunk */
        struct list_head *node = chunk_pool[pi].next;
        list_del(node);

        /*
         * One more chunk is now in a caller's hands: decrement the owning
         * page's free-chunk counter.  We recover the page by aligning the
         * chunk address down to a PAGE_SIZE boundary (all chunks carved from
         * the same page share the same aligned base).
         */
        struct page *owner =
            phys_to_page(ALIGN_DOWN((uint64_t)node, PAGE_SIZE));
        owner->refcount--;

        log_chunk_alloc((uint64_t)node, size, pool_sizes[pi]);

        return (void *)node;

    } else {
        /* ── Page-frame path (size > largest pool) ── */
        uint32_t order = 0;
        uint64_t s = PAGE_SIZE;
        while (s < size) {
            s <<= 1;
            order++;
        }
        log_alloc_pages_path(size, order);

        struct page *pg = alloc_pages(order);
        if (!pg)
            return NULL;
        /* chunk_size == 0: free() will call free_pages */
        return (void *)page_to_phys(pg);
    }
}

void free(void *ptr) {
    if (!ptr)
        return;
    uint64_t addr = (uint64_t)ptr;
    if (addr < mem_base || addr >= mem_base + mem_size)
        return;

    uint64_t pfn = (addr - mem_base) >> PAGE_SHIFT;
    struct page *pg = &mem_map[pfn];

    if (pg->chunk_size > 0) {
        uint32_t csz = pg->chunk_size;
        log_chunk_free(addr, csz);

        /* Locate the pool slot for this chunk size. */
        int pi = -1;
        for (int i = 0; i < NUM_POOLS; i++) {
            if (pool_sizes[i] == csz) {
                pi = i;
                break;
            }
        }
        if (pi < 0)
            return;

        /* Return this chunk to its pool. */
        struct list_head *node = (struct list_head *)ptr;
        list_init(node);
        list_add_tail(node, &chunk_pool[pi]);

        /*
         * Slab reclaim: increment the owning page's free-chunk counter.
         * If refcount reaches total_chunks, every single chunk from this
         * page is now idle — reclaim the entire page back to the buddy system.
         *
         * Why page_base = addr & ~(PAGE_SIZE-1)?
         *   All chunks carved from the same 4 KiB page share the same
         *   page-aligned base, so masking off the low PAGE_SHIFT bits
         *   gives us the page start without a division.
         */
        pg->refcount++;

        uint32_t total_chunks = (uint32_t)(PAGE_SIZE / csz);
        if (pg->refcount == total_chunks) {
            log_slab_reclaim(page_to_phys(pg), csz, total_chunks);

            /*
             * Walk chunk_pool[pi] and unlink every chunk that belongs to
             * this page.  We identify a chunk as belonging here by checking
             * that its address falls within [page_base, page_base+PAGE_SIZE).
             * list_for_each_safe lets us delete during iteration.
             */
            uint64_t page_base = page_to_phys(pg);
            uint64_t page_end = page_base + PAGE_SIZE;

            struct list_head *pos, *tmp;
            list_for_each_safe(pos, tmp, &chunk_pool[pi]) {
                uint64_t chunk_addr = (uint64_t)pos;
                if (chunk_addr >= page_base && chunk_addr < page_end)
                    list_del(pos);
            }

            /*
             * Clear chunk_size so free_pages() sees an ordinary page, then
             * return the page to the buddy system.  free_pages() will also
             * set refcount = 0 and attempt to merge with adjacent free blocks.
             */
            pg->chunk_size = 0;
            free_pages(pg);
        }
    } else {
        log_free_pages_path(addr);
        free_pages(pg);
    }
}

/* ── mm_init ──────────────────────────────────────────────────────────────────
 */

void mm_init(const void *fdt) {
    uart_puts("\n[MM] Initializing memory manager...\n");

    /* 1. Discover all physical memory regions. */
    parse_memory_regions(fdt);
    total_pages = mem_size >> PAGE_SHIFT;

    uart_puts("[MM] mem_base=");
    uart_hex(mem_base);
    uart_puts("  span=");
    uart_hex(mem_size);
    uart_puts("  total_pages=");
    uart_put_u64(total_pages);
    uart_puts("  usable_pages=");
    uart_put_u64(usable_pages);
    uart_puts("\n");

    /* 2. Record all reserved regions.
     *    (a) DTB blob */
    const struct fdt_header *fhdr = (const struct fdt_header *)fdt;
    uint64_t dtb_size = fdt_be32((const unsigned char *)&fhdr->totalsize);
    record_reserved("dtb", (uint64_t)fdt, (uint64_t)fdt + dtb_size);

    /* (b) Kernel image (linker symbols) */
    record_reserved("kernel", (uint64_t)_start, (uint64_t)_end);

    /* (c) Initramfs */
    uint64_t initrd_s = fdt_get_chosen_addr(fdt, "linux,initrd-start");
    uint64_t initrd_e = fdt_get_chosen_addr(fdt, "linux,initrd-end");
    if (initrd_s && initrd_e > initrd_s)
        record_reserved("initramfs", initrd_s, initrd_e);

    /* (d) Platform-specific /reserved-memory entries */
    fdt_foreach_reserved_memory(fdt, on_reserved_region, 0);

    /* 3. Startup allocator: place Frame Array at the first free address.
     *    Must pass fa_bytes so the search checks the FULL range, not just
     *    the start address — otherwise the array can straddle the kernel. */
    uint64_t fa_bytes = total_pages * sizeof(struct page);
    fa_bytes = ALIGN_UP(fa_bytes, PAGE_SIZE);
    uint64_t fa_start = first_free_addr(fa_bytes);

    uart_puts("[MM] Frame array: ");
    uart_hex(fa_start);
    uart_puts(" - ");
    uart_hex(fa_start + fa_bytes);
    uart_puts("\n");

    mem_map = (struct page *)fa_start;
    memset(mem_map, 0, fa_bytes);

    /* Also reserve the frame array so the buddy system won't hand it out. */
    record_reserved("frame-array", fa_start, fa_start + fa_bytes);

    /* 4. Initialise buddy system. */
    for (int i = 0; i <= MAX_ORDER; i++)
        list_init(&free_area[i]);

    /* Seed free_area with MAX_ORDER blocks covering all managed pages. */
    uint64_t seed_i = 0;
    for (; seed_i + (1ULL << MAX_ORDER) <= total_pages;
         seed_i += (1ULL << MAX_ORDER)) {
        mem_map[seed_i].order = MAX_ORDER;
        mem_map[seed_i].refcount = 0;
        list_add_tail(&mem_map[seed_i].list, &free_area[MAX_ORDER]);
    }

    /* Seed remaining pages that don't fill a MAX_ORDER block. */
    for (int ord = MAX_ORDER - 1; ord >= 0; ord--) {
        if (seed_i + (1ULL << ord) <= total_pages) {
            mem_map[seed_i].order = (uint32_t)ord;
            mem_map[seed_i].refcount = 0;
            list_add_tail(&mem_map[seed_i].list, &free_area[ord]);
            seed_i += (1ULL << ord);
        }
    }

    /* Mark every reserved region; memory_reserve() splits/removes blocks. */
    for (int i = 0; i < num_reserved; i++) {
        uint64_t s = reserved_regions[i].start;
        uint64_t e = reserved_regions[i].end;
        if (e <= mem_base || s >= mem_base + mem_size)
            continue;
        memory_reserve(s, e - s);
    }

    /* 5. Initialise chunk pool free-lists. */
    for (int i = 0; i < NUM_POOLS; i++)
        list_init(&chunk_pool[i]);

    dump_free_areas();

    uart_puts("[MM] Memory manager ready.\n\n");
}
