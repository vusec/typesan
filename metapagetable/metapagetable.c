#include <sys/mman.h>             // for mmap, memadvise
#include <string.h>               // for memchr
#include <stdlib.h>               // for getenv
#include <stdio.h>                // for printf
#include <metapagetable.h>
#include "../gperftools-metalloc/src/base/linux_syscall_support.h"

#define unlikely(x)     __builtin_expect((x),0)

// Size of the pagetable (one entry per page)
#if FLAGS_METALLOC_FIXEDCOMPRESSION == false
#define PAGETABLESIZE (((unsigned long)1 << 48) / METALLOC_PAGESIZE)
#else
#define PAGETABLESIZE (((unsigned long)1 << 48) / ((METALLOC_FIXEDSIZE / FLAGS_METALLOC_METADATABYTES) * 16))
#endif
// Number of pagetable pages covered by each reftable entry
#define PTPAGESPERREFENTRY 1
// Size of the reftable (one entry per PAGESPERREFENTRY pages in the pagetable)
// #define REFTABLESIZE ((unsigned long)((PAGETABLESIZE << sizeof(unsigned long)) / METALLOC_PAGESIZE) * PTPAGESPERREFENTRY)
// Number of real pages covered by each reftable entry
// #define REALPAGESPERREFENTRY ((unsigned long)(METALLOC_USEDSIZE >> sizeof(unsigned long)) * PTPAGESPERREFENTRY)

//unsigned long pageTable[PAGETABLESIZE];
bool isPageTableAlloced = false;
//unsigned short refTable[REFTABLESIZE];

int is_fixed_compression() {
    return FLAGS_METALLOC_FIXEDCOMPRESSION ? 1 : 0;
}

void page_table_init() {
    if (unlikely(!isPageTableAlloced)) {
        void *pageTableMap = sys_mmap(pageTable, PAGETABLESIZE * sizeof(unsigned long), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (pageTableMap == MAP_FAILED) {
            perror("Could not allocate pageTable");
            exit(-1);
        }
        isPageTableAlloced = true;
    }
}

__attribute__((section(".init_array"), used))
void (*init_func1)(void) = page_table_init;

bool is_metapagetable_alloced() {
    return isPageTableAlloced;
}

void* allocate_metadata(unsigned long size, unsigned long alignment) {
    /*if (unlikely(isPageTableAlloced == false))
        page_table_init();*/
    unsigned long pageAlignOffset = SYSTEM_PAGESIZE - 1;
    unsigned long pageAlignMask = ~((unsigned long)SYSTEM_PAGESIZE - 1);
    unsigned long metadataSize = (((size * FLAGS_METALLOC_METADATABYTES) >> alignment) + pageAlignOffset) & pageAlignMask;
    void *metadata = sys_mmap(NULL, metadataSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (unlikely(metadata == MAP_FAILED)) {
        perror("Could not allocate metadata");
        exit(-1);
    }
    return metadata;
}

void deallocate_metadata(void *ptr, unsigned long size, unsigned long alignment) {
    unsigned long pageAlignOffset = SYSTEM_PAGESIZE - 1;
    unsigned long pageAlignMask = ~((unsigned long)SYSTEM_PAGESIZE - 1);
    unsigned long metadata = pageTable[((unsigned long)ptr) / METALLOC_PAGESIZE] >> 8;
    unsigned long metadataSize = (((size * FLAGS_METALLOC_METADATABYTES) >> alignment) + pageAlignOffset) & pageAlignMask;
    munmap((void*)metadata, metadataSize);
    return;
}

void set_metapagetable_entries(void *ptr, unsigned long size, void *metaptr, int alignment) {
    if (unlikely(isPageTableAlloced == false))
        page_table_init();
    if (unlikely(size % METALLOC_PAGESIZE != 0)) {
        printf("Meta-pagetable must be configured for ranges that are multiple of METALLOC_PAGESIZE");
        exit(-1);
    }
    // Get the page number
    unsigned long page = (unsigned long)ptr / METALLOC_PAGESIZE;
    // Get the page count
    unsigned long count = size / METALLOC_PAGESIZE;
    // For each page set the appropriate pagetable entry
    for (unsigned long i = 0; i < count; ++i) {
        // Compute the pointer towards the metadata
        // Shift the pointer by 8 positions to the left
        // Inject the alignment to the lower byte
        unsigned long metaOffset = (i * METALLOC_PAGESIZE >> alignment) * FLAGS_METALLOC_METADATABYTES;
        unsigned long pageMetaptr;
        if (metaptr == 0)
            pageMetaptr = 0;
        else 
            pageMetaptr = (unsigned long)metaptr + metaOffset;
        pageTable[page + i] = (pageMetaptr << 8) | (char)alignment;
    }
}

unsigned long get_metapagetable_entry(void *ptr) {
    if (unlikely(isPageTableAlloced == false))
        page_table_init();
    // Get the page number
    unsigned long page = (unsigned long)ptr / METALLOC_PAGESIZE;
    // Get table entry
    return pageTable[page];
}

void allocate_metapagetable_entries(void *ptr, unsigned long size) {
    /*if (unlikely(size % METALLOC_PAGESIZE != 0)) {
        printf("Meta-pagetable must be configured for ranges that are multiple of METALLOC_PAGESIZE");
        exit(-1);
    }
    // Get the page number
    unsigned long page = (unsigned long)ptr / METALLOC_PAGESIZE;
    // Get the page count
    unsigned long count = size / METALLOC_PAGESIZE;
    // Get the ref entry
    unsigned long refEntry = page / REALPAGESPERREFENTRY;
    // The first ref entry is only partially covered by the allocated range (from the specified start page to the end)
    // Compute the maximum potential ref-count increase
    unsigned long potentialMaxRefCount = REALPAGESPERREFENTRY - (page - (refEntry * REALPAGESPERREFENTRY)) / REALPAGESPERREFENTRY;
    unsigned long i = 0;
    // Increase the ref-counts until there are more allocated pages
    while (count > 0) {
        // The remaining size of the allocation completely covers the current ref entry
        if (count >= potentialMaxRefCount) {
            refTable[refEntry + i] += potentialMaxRefCount;
            count -= potentialMaxRefCount;
        // The allocation stops within the range covered by this ref entry
        } else {
            refTable[refEntry + i] += count;
            count = 0;
        }
        // Subsequent ref entries are covered from the beginning by the allocated range
        potentialMaxRefCount = REALPAGESPERREFENTRY;
        // Move to the next ref entry
        ++i;
    }*/
}

void deallocate_metapagetable_entries(void *ptr, unsigned long size) {
    /*if (unlikely(size % METALLOC_PAGESIZE != 0)) {
        printf("Meta-pagetable must be configured for ranges that are multiple of METALLOC_PAGESIZE");
        exit(-1);
    }
    // Get the page number
    unsigned long page = (unsigned long)ptr / METALLOC_PAGESIZE;
    // Get the page count
    unsigned long count = size / METALLOC_PAGESIZE;
    // Get the ref entry
    unsigned long refEntry = page / REALPAGESPERREFENTRY;
    // The first ref entry is only partially covered by the deallocated range (from the specified start page to the end)
    // Compute the maximum potential ref-count decrease
    unsigned long potentialMaxRefCount = REALPAGESPERREFENTRY - (page - (refEntry * REALPAGESPERREFENTRY)) / REALPAGESPERREFENTRY;
    unsigned long i = 0;
    // Decrease the ref-counts until there are more allocated pages
    while (count > 0) {
        // The remaining size of the deallocation completely covers the current ref entry
        if (count >= potentialMaxRefCount) {
            refTable[refEntry + i] += potentialMaxRefCount;
            count -= potentialMaxRefCount;
        // The deallocation stops within the range covered by this ref entry
        } else {
            refTable[refEntry + i] += count;
            count = 0;
        }
        // Part of page table corresponding to ref entry completely deallocated
        // Release the physical memory from the corresponding page-table section
        if (refTable[refEntry + i] == 0) {
            madvise(&(pageTable[(refEntry + i) * REALPAGESPERREFENTRY]), PTPAGESPERREFENTRY * METALLOC_PAGESIZE, MADV_DONTNEED);
        }
        // Subsequent ref entries are covered from the beginning by the deallocated range
        potentialMaxRefCount = REALPAGESPERREFENTRY;
        // Move to the next ref entry
        ++i;
    }*/
}

/* TODO this is a bad hack to prevent the system from crashing if Firefox does casts on stack objects that should never have been typecast in the first place */
__attribute__((constructor)) static void allocate_safe_stack_meta(void) {
	char *stackend = (char *) 0x0000800000000000UL;
	size_t stacksize = 0x100000;
	char *stackstart = stackend - stacksize;
	int alignbits = 3;
	void *metadata = allocate_metadata(stacksize, alignbits);
	set_metapagetable_entries((void *) stackstart, stacksize, metadata, alignbits);
}

