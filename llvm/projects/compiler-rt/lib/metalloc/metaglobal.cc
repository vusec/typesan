#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <link.h>
#include <metapagetable_core.h>

#include "sanitizer_common/sanitizer_common.h"

extern char __executable_start;
extern char _etext;
extern char __data_start;
extern char _edata;
extern char __bss_start;
extern char _end;

const int kMetaPageSize = 4096;
const unsigned kMetaGlobalAlignBits = 3;

static int shared_object_callback(struct dl_phdr_info *info, size_t size, void *data) {
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type != PT_LOAD) {
            continue;
        }
        unsigned long base_addr = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
        unsigned long section_size = info->dlpi_phdr[i].p_memsz;
        if (section_size == 0) {
            continue;
        }
        if (pageTable[(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr) >> METALLOC_PAGESHIFT] != 0) {
            return 0;
        }
        unsigned long page_align_offset = kMetaPageSize - 1;
        unsigned long page_align_mask = ~((unsigned long)kMetaPageSize - 1);
        unsigned long aligned_start = base_addr & page_align_mask;
        unsigned long aligned_size = ((section_size + base_addr - aligned_start) + page_align_offset) & page_align_mask;
        void *exec_metadata = allocate_metadata(aligned_size, kMetaGlobalAlignBits);
        set_metapagetable_entries((void*)aligned_start, aligned_size, exec_metadata, kMetaGlobalAlignBits);
    }
    return 0;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void metalloc_init_globals(unsigned int object) {
    // Check if this shared object has already been loaded or not
    // Enough to check single object for mapping
    if (pageTable[object >> METALLOC_PAGESHIFT] != 0) {
        return;
    }
    dl_iterate_phdr(&shared_object_callback, NULL);

    return;
}

extern "C" __attribute__((visibility("default")))
#if !SANITIZER_CAN_USE_PREINIT_ARRAY
// On ELF platforms, the constructor is invoked using .preinit_array (see below)
__attribute__((constructor(0)))
#endif
void __metaglobal_init() {
    metalloc_init_globals((unsigned long)&__executable_start);
}
               
#if SANITIZER_CAN_USE_PREINIT_ARRAY
// On ELF platforms, run safestack initialization before any other constructors.
// On other platforms we use the constructor attribute to arrange to run our
// initialization early.
extern "C" {
__attribute__((section(".preinit_array"),
               used)) void (*__metaglobal_preinit)(void) = __metaglobal_init;
}
#endif
