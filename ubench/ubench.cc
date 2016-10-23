#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define ITERCOUNT 1024
#define LOOPCOUNT 1024

#include "ubench-gen.h"

volatile int always_zero;
static double cpu_freq;
volatile void *globalptr;
static int objcountlog;

static inline uint64_t rdtsc(void) {
	uint32_t eax, edx;
	__asm volatile ("rdtsc" : "=a" (eax), "=d" (edx));
	return eax | ((uint64_t) edx << 32);
}

static int timeval_cmp(const struct timeval *x, const struct timeval *y) {
	if (x->tv_sec < y->tv_sec) return -1;
	if (x->tv_sec > y->tv_sec) return 1;
	if (x->tv_usec < y->tv_usec) return -1;
	if (x->tv_usec > y->tv_usec) return 1;
	return 0;
}

static struct timeval timeval_diff(const struct timeval *x, const struct timeval *y) {
	struct timeval z;
	z.tv_sec = x->tv_sec - y->tv_sec;
	if (x->tv_usec >= y->tv_usec) {
		z.tv_usec = x->tv_usec - y->tv_usec;
	} else {
		z.tv_sec -= 1;
		z.tv_usec = x->tv_usec + 1000000 - y->tv_usec;
	}
	return z;
}

static double timeval2sec(const struct timeval *x) {
	return x->tv_sec + x->tv_usec / 1000000.0;
}

static double calibrate(void) {
	struct timeval timediff, timestart, timedeadline, timeend;
	uint64_t tscstart, tscend;
	double freq, freqprev = 0;

	for (;;) {
		gettimeofday(&timestart, NULL);
		tscstart = rdtsc();
		timedeadline = timestart;
		timedeadline.tv_sec++;
		do {
			gettimeofday(&timeend, NULL);
			tscend = rdtsc();
		} while (timeval_cmp(&timeend, &timedeadline) < 0);

		timediff = timeval_diff(&timeend, &timestart);
		freq = (tscend - tscstart) / timeval2sec(&timediff);
		if (freq <= freqprev) return freqprev;
		freqprev = freq;
	}
}

static int qsort_cmp_uint64_t(const void *p1, const void *p2) {
	uint64_t i1 = *(const uint64_t *) p1;
	uint64_t i2 = *(const uint64_t *) p2;
	if (i1 < i2) return -1;
	if (i1 > i2) return 1;
	return 0;
}

static void measure_report(
	const char *desc,
	int logsize,
	int nested,
	uint64_t *tscdiff) {
	double factor = 1000000000.0 / cpu_freq / LOOPCOUNT;
	int i;
	uint64_t sum = 0, sum2 = 0, x;
	double var;

	qsort(tscdiff, ITERCOUNT, sizeof(tscdiff[0]), qsort_cmp_uint64_t);

	for (i = 0; i < ITERCOUNT; i++) {
		x = tscdiff[i];
		sum += x;
		sum2 += x * x;
	}
	var = (sum2 - sum * sum / ITERCOUNT) / (ITERCOUNT - 1);
	printf("%s\t%d\t%d\t%d\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\n",
		desc,
		logsize,
		nested,
		objcountlog,
		ITERCOUNT,
		sum / ITERCOUNT * factor,
		sqrt(var) * factor,
		tscdiff[ITERCOUNT * 0 / 4] * factor,
		tscdiff[ITERCOUNT * 1 / 4] * factor,
		tscdiff[ITERCOUNT * 2 / 4] * factor,
		tscdiff[ITERCOUNT * 3 / 4] * factor,
		tscdiff[ITERCOUNT - 1] * factor);
}

#define MEASURE(desc, logsize, nested, code1, code2, code3)		\
	do {								\
		int i, j;						\
		uint64_t tscdiff[ITERCOUNT];				\
		uint64_t tscstart;					\
		for (i = 0; i < 2; i++) {				\
			for (j = 0; j < ITERCOUNT; j++) {		\
				code1					\
				tscstart = rdtsc();			\
				code2					\
				tscdiff[j] = rdtsc() - tscstart;	\
				code3					\
			}						\
		}							\
		measure_report(desc, logsize, nested, tscdiff);		\
	} while (0)

static void test_rdtsc(void) {
	MEASURE("rdtsc", 0, 0, {}, {}, {});
}

#define TESTSIZE(logsize, nested, class) 				\
static void test_alloc_stack_helper_##logsize##_##nested(void) __attribute__((noinline)); \
static void test_alloc_stack_helper_##logsize##_##nested(void) {	\
	class obj;							\
	if (always_zero) {						\
		BaseClass *base = &obj;					\
		class *ptr = static_cast<class *>(base);		\
		globalptr = ptr;					\
	}								\
}									\
static void test_alloc_stack_##logsize##_##nested(void) { 		\
	MEASURE("alloc_stack", logsize, nested,				\
		int loop;						\
	,								\
		for (loop = 0; loop < LOOPCOUNT; loop++) {		\
			test_alloc_stack_helper_##logsize##_##nested();	\
		}							\
	, {});								\
}
#include "ubench-gen-inc.h"
#undef TESTSIZE

static void test_alloc_stack(void) {
#define TESTSIZE(logsize, nested, class) test_alloc_stack_##logsize##_##nested();
#include "ubench-gen-inc.h"
#undef TESTSIZE
}

#define TESTSIZE(logsize, nested, class) 				\
static void test_alloc_heap_##logsize##_##nested(void) { 		\
	MEASURE("alloc_heap", logsize, nested, 				\
		class *objs[LOOPCOUNT];		 			\
		int loop;						\
	,								\
		for (loop = 0; loop < LOOPCOUNT; loop++) {		\
			objs[loop] = new class();			\
		}							\
	,								\
		for (loop = 0; loop < LOOPCOUNT; loop++) {		\
			BaseClass *base = objs[loop];			\
			class *ptr = static_cast<class *>(base);	\
			globalptr = ptr;				\
			delete objs[loop];				\
		}							\
	);								\
}
#include "ubench-gen-inc.h"
#undef TESTSIZE

static void test_alloc_heap(void) {
#define TESTSIZE(logsize, nested, class) test_alloc_heap_##logsize##_##nested();
#include "ubench-gen-inc.h"
#undef TESTSIZE
}

#define TESTSIZE(logsize, nested, class) 				\
static void test_cast_stack_##logsize##_##nested(void) { 		\
	MEASURE("cast_stack", logsize, nested, 				\
		BaseClass *bases[LOOPCOUNT];				\
		class obj;						\
		class *objs[LOOPCOUNT];					\
		int loop;						\
		for (loop = 0; loop < LOOPCOUNT; loop++) {		\
			bases[loop] = &obj;				\
		}							\
	, 								\
		for (loop = 0; loop < LOOPCOUNT; loop++) {		\
			objs[loop] = static_cast<class *>(bases[loop]);	\
		}							\
	,								\
		for (loop = 0; loop < LOOPCOUNT; loop++) {		\
			globalptr = objs[loop];				\
		}							\
	);								\
}
#include "ubench-gen-inc.h"
#undef TESTSIZE

static void test_cast_stack(void) {
#define TESTSIZE(logsize, nested, class) test_cast_stack_##logsize##_##nested();
#include "ubench-gen-inc.h"
#undef TESTSIZE
}

#define TESTSIZE(logsize, nested, class) 				\
static void test_cast_heap_##logsize##_##nested(void) { 		\
	MEASURE("cast_heap", logsize, nested, 				\
		BaseClass *bases[LOOPCOUNT];				\
		class *obj = new class();				\
		class *objs[LOOPCOUNT];					\
		int loop;						\
		for (loop = 0; loop < LOOPCOUNT; loop++) {		\
			bases[loop] = obj;				\
		}							\
	, 								\
		for (loop = 0; loop < LOOPCOUNT; loop++) {		\
			objs[loop] = static_cast<class *>(bases[loop]);	\
		}							\
	,								\
		for (loop = 0; loop < LOOPCOUNT; loop++) {		\
			globalptr = objs[loop];				\
		}							\
		delete obj;						\
	);								\
}
#include "ubench-gen-inc.h"
#undef TESTSIZE

static void test_cast_heap(void) {
#define TESTSIZE(logsize, nested, class) test_cast_heap_##logsize##_##nested();
#include "ubench-gen-inc.h"
#undef TESTSIZE
}

static void test_recurse(int objcount) {
	BaseClass obj;
	globalptr = &obj;

	if (objcountlog > 16) return;
	objcount++;
	if (objcount == (1 << objcountlog)) {
		test_alloc_stack();
		test_alloc_heap();
		test_cast_stack();
		test_cast_heap();
		objcountlog++;
	}
	test_recurse(objcount);
}

int main(void) {
	int objcount = 0;

	cpu_freq = calibrate();

	printf("desc\tlogsize\tnested\tobjcountlog\tn\tmean\tstdev\tmin\tq25\tmedian\tq75\tmax\n");
	printf("cpu_freq\t\t\t%.1f\t\t\t\n", cpu_freq);
	test_rdtsc();
	test_recurse(0);
}
