#include "sanitizer_common/sanitizer_common.h"

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void metalloc_widememset(unsigned long *base, unsigned long size, unsigned long value1, unsigned long value2) {
    for (unsigned long i = 0; i < size; ++i) {
        base[2 * i] = value1;
        base[2 * i + 1] = value2;
    }
}
