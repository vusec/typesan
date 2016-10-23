#include "ubsan/ubsan_platform.h"
#include "ubsan/ubsan_handlers_cxx.h"
#include "ubsan/ubsan_diag.h"
#include "ubsan/ubsan_type_hash.h" 
#include "ubsan/ubsan_value.h"

#include "sanitizer_common/sanitizer_report_decorator.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_libc.h"

#include <cxxabi.h>
#include <stdio.h>
#include <csignal>
#include <signal.h>
#include <ucontext.h>
#include <vector>
#include <set>
#include <unordered_map>

#include "metalloc/metapagetable_core.h"

using namespace __ubsan;
using namespace std;

#define SAFECAST 0
#define BADCAST 1

//#define LOG_CAST_COUNT
#define DO_REPORT_BADCAST
#define DO_REPORT_BADCAST_FATAL
#define DO_REPORT_BADCAST_FATAL_NOCOREDUMP
//#define DO_REPORT_MISSING

#ifdef DO_REPORT_BADCAST_FATAL_NOCOREDUMP
#define TERMINATE exit(-1);
#else
#define TERMINATE abort();
#endif

#define UNW_LOCAL_ONLY
#include <cxxabi.h>
#include <libunwind.h>
#include <cstdio>
#include <cstdlib>

static void backtrace() {
  unw_cursor_t cursor;
  unw_context_t context;

  // Initialize cursor to current frame for local unwinding.
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  // Unwind frames one by one, going up the frame stack.
  while (unw_step(&cursor) > 0) {
    unw_word_t offset, pc;
    unw_get_reg(&cursor, UNW_REG_IP, &pc);
    if (pc == 0) {
      break;
    }
    std::printf("0x%lx:", pc);

    char sym[256];
    if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
      char* nameptr = sym;
      int status;
      char* demangled = abi::__cxa_demangle(sym, nullptr, nullptr, &status);
      if (status == 0) {
        nameptr = demangled;
      }
      std::printf(" (%s+0x%lx)\n", nameptr, offset);
      std::free(demangled);
    } else {
      std::printf(" -- error: unable to obtain symbol name for this frame\n");
    }
  }
}


static FILE *op = nullptr;

static void write_log(string result) {
        if (op == nullptr) {
            op = fopen("cast_results.txt", "a");
        }
	fprintf(op, "%s\n", result.c_str());
	fflush(op);
}

#ifdef LOG_CAST_COUNT
static volatile unsigned long type1 = 0;
static volatile unsigned long type2 = 0;
static volatile unsigned long type3 = 0;
static volatile unsigned long type4 = 0;
static volatile unsigned long type5 = 0;
static volatile unsigned long type6 = 0;
__attribute__ ((visibility ("default"))) long __typesan_alloc_count; /* enable TRACK_ALLOCATIONS in llvm/lib/Transforms/Utils/HexTypeUtil.cpp */

static void write_log_casts(int signum) {
    char outputStr[2048];
    sprintf(outputStr, "%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu",
        type1, type2, type3, type4, type5, type6, __typesan_alloc_count);
    write_log(outputStr);
}

struct LoggerType {
    LoggerType() {
        struct sigaction sa = {};
	sa.sa_handler = write_log_casts;
        sigaction(50, &sa, NULL);        
    }
    ~LoggerType() {
        write_log_casts(-1);
    }
};
static LoggerType logger;
#endif

typedef vector<uint64_t> parentHashSetTy;
// Mapping from class-hash to pointer into parent-hashes set
// Guaranteed to be fixed in size and the number of hashes is limited to the number of classes
static unordered_map<uint64_t, parentHashSetTy*> *hashToSetMap;

const static int pageSize = 4096;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __update_cinfo(unsigned int classCount, unsigned long *infoArray) {

	#ifdef FDEBUG_LOG
	  char tmp[1000];

	  numUpdate += 1;
	  numObj += classCount;
	  sprintf(tmp, "[FDEBUG] %d: Update_cinfo call total number: (%d) total object: (%d)\n", numUpdate, classCount, numObj);
	  string print(tmp);
	  write_flog(print);
	#endif

        // init STL data structures (global initializer interacts poorly with this code)
        if (hashToSetMap == nullptr) {
            hashToSetMap = new unordered_map<uint64_t, parentHashSetTy*>(1024);
        }
        
        unsigned int processedCount = 0;
        unsigned int pos = 0;
        while(processedCount < classCount) {
            unsigned long hashCount = infoArray[pos++];
            uint64_t classHash = infoArray[pos++];
            // Upmost bit of count signals needs for merger
            bool doMerge = (hashCount & (1 << 31)) != 0;
            hashCount &= ~(1 << 31);

            // See if class already has index associated or not
            auto mapEntry = hashToSetMap->find(classHash);
            bool alreadyProcessed = false;
            parentHashSetTy* parentSet;
            if (mapEntry != hashToSetMap->end()) {
                alreadyProcessed = true;
                parentSet = mapEntry->second;
            } else {
                parentSet = new parentHashSetTy();
                hashToSetMap->insert(make_pair(classHash, parentSet));
            }
            
            // No merging requested and class already seen
            // Skip to next class
            if (!doMerge && alreadyProcessed) {
                pos += hashCount - 1;
                processedCount++;
                continue;
            }
            
            // Read and insert hashes for the selected class
            // Class never processed yet, so include all entries
            if (!alreadyProcessed) {
                parentSet->reserve(hashCount - 1);
                for(unsigned int i = 0; i < hashCount - 1; i++) {
                    parentSet->push_back(infoArray[pos++]);
                }
            // Class already processed, but merging requested
            // Merge new elements uniquely using a set proxy
            } else {
                set<uint64_t> hashSet(parentSet->begin(), parentSet->end());
                for(unsigned int i = 0; i < hashCount - 1; i++) {
                    uint64_t hash = infoArray[pos++];
                    auto insertIt = hashSet.insert(hash);
                    if (insertIt.second) {
                        parentSet->push_back(hash);
                    }
                }
            }
            
            processedCount++;
        }
}

__attribute__((always_inline)) inline static void check_cast(uptr* src_addr, uptr* dst_addr, uint64_t dst) {
#ifdef LOG_CAST_COUNT
    type1++;
#endif

        if (src_addr == nullptr)
            return;

#ifdef LOG_CAST_COUNT
    type2++;
#endif 

	uint64_t src = 0;

        unsigned long ptrInt = (unsigned long)src_addr;
        unsigned long pageIndex = (unsigned long)ptrInt / pageSize;
        unsigned long pageEntry = pageTable[pageIndex];
        unsigned long *metaBase = (unsigned long*)(pageEntry >> 8);
        unsigned long alignment = pageEntry & 0xFF;
        char *alloc_base = (char*)(metaBase[2 * ((ptrInt & (pageSize - 1)) >> alignment)]);
        // No metadata for object
        if (alloc_base == nullptr) {
#ifdef DO_REPORT_MISSING
		static int missingc = 0;
		static int missingt = 1;
		missingc++;
		if (missingc >= missingt) {
			printf("\n\t\t== Missing metadata ==\n");
			printf("src_addr=%p dst_addr=%p dst=%lu\n", src_addr, dst_addr, (unsigned long) dst);
			backtrace();
			missingt *= 2;
		}
#endif
		return;
	}
        
#ifdef LOG_CAST_COUNT
    type3++;
#endif

        long offset = (char*)dst_addr - alloc_base;
        if (offset < 0) {
#ifdef DO_REPORT_BADCAST
            printf("\n\t\t== TypeSan Bad-casting Reports ==\n");
            printf("\t\tDetected type confusion from negative offset (%ld) to %lu\n", offset, (unsigned long) dst);
            backtrace();
#endif
#ifdef DO_REPORT_BADCAST_FATAL
            TERMINATE
#endif
	    return;
        }
        unsigned long *typeInfo = (unsigned long*)(metaBase[2 * ((ptrInt & (pageSize - 1)) >> alignment) + 1]);
        long currentOffset = typeInfo[0];
        // If first offset is not 0, then we are pointing to size field
        // This suggests an array allocation and we need to adjust offset to match
        if (currentOffset != 0) {
            if (currentOffset == -1) {
		// special case: no typeinfo at all means blacklisted
#ifdef LOG_CAST_COUNT
		type6++;
#endif
                return;
            }
            offset %= currentOffset;
            currentOffset = 0;
            typeInfo++;
        }
        while(1) {
            // Found matching entry
            if (offset == currentOffset) {
                src = typeInfo[1];
                break;
            // Move to next entry if needed
            } else if (offset >= (long)(typeInfo[2] & ~((long)1 << 63))) {
                typeInfo += 2;
                currentOffset = (long)typeInfo[0];
                if (currentOffset == -1) {
                    break;
                }
                continue;
            }
            // Try to match with current array entry
            long currentArrayOffset = currentOffset & ~((long)1 << 63);
            if (currentOffset != currentArrayOffset) {
                offset -= currentArrayOffset;
                unsigned long *arrayTypeInfo = (unsigned long*)(typeInfo[1]);
                offset %= (long)arrayTypeInfo[0];
                typeInfo = arrayTypeInfo + 1;
                currentOffset = 0;
                continue;
            // No match found
            } else {
                break;
            }
        }
        if (src == 0) {
#ifdef DO_REPORT_BADCAST
            //SourceLocation Loc = Data->Loc.acquire();
            printf("\n\t\t== TypeSan Bad-casting Reports ==\n");
            //printf("\t\tFileName : %s Line: %d Column %d\n", Loc.getFilename(), Loc.getLine(), Loc.getColumn());
            printf("\t\tDetected type confusion from unknown offset (%ld) in type-info (%p) to %lu\n", (char*)dst_addr - alloc_base, (unsigned long*)(metaBase[2 * ((ptrInt & (pageSize - 1)) >> alignment) + 1]), (unsigned long) dst);
            backtrace();
#endif
#ifdef DO_REPORT_BADCAST_FATAL
            TERMINATE
#endif
	    return;
        }
            
        // Types match perfectly
        if(src == dst) {

#ifdef LOG_CAST_COUNT
    type4++;
#endif

            return;
        }
        
	int result = -1;
    
	{
                auto indexIt = hashToSetMap->find(src);
                if (indexIt == hashToSetMap->end()) {
#ifdef DO_REPORT_BADCAST
                    printf("\n\t\t== TypeSan Bad-casting Reports ==\n");
                    printf("\t\tDetected type confusion from unknown hash (%lu) to %lu\n", (unsigned long) src, (unsigned long) dst);
                    backtrace();
#endif
#ifdef DO_REPORT_BADCAST_FATAL
                    TERMINATE
#endif
		    return;
                }

                auto *parentHashSet = indexIt->second;
                for(uint64_t hash : *parentHashSet) {
                    if(hash == dst) {

                        result = SAFECAST;
                        break;
                    }
                }
                if(result != SAFECAST) {

                    result = BADCAST;
                }
	}

#ifdef LOG_CAST_COUNT
        if (result == BADCAST) {
            type5++;
        }
#endif

	if (result == BADCAST) {
#ifdef DO_REPORT_BADCAST
		printf("\n\t\t== TypeSan Bad-casting Reports ==\n");
		printf("\t\tDetected type confusion from %lu to %lu\n", (unsigned long) src, (unsigned long) dst);
		backtrace();
#endif
#ifdef DO_REPORT_BADCAST_FATAL
		TERMINATE
#endif
		return;
	}

	return;
}

// Checking bad-casting 
extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __changing_type_casting_verification(uptr* src_addr, uptr* dst_addr, uint64_t dst) {
    check_cast(src_addr, dst_addr, dst);
}

// Checking bad-casting 
extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __type_casting_verification(uptr* src_addr, uint64_t dst) {
    check_cast(src_addr, src_addr, dst);
}

