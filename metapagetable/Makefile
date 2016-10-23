PAGESIZE:= $(shell getconf PAGESIZE)
CC=libtool --tag=CC --mode=compile gcc -prefer-pic
LINKER=libtool --tag=CC --mode=link gcc
INCLUDES=-I. -I../gperftools-metalloc/src/
CFLAGS=-fPIC -c -Werror -Wall -O3 -std=gnu11 -DSYSTEM_PAGESIZE=$(PAGESIZE)
LDFLAGS=-static

EXE=libmetapagetable.la

SRCS    := $(wildcard *.c)
OBJS    := $(patsubst %.c,obj/%.lo,$(SRCS))
DEPS    := $(OBJS:.lo=.d)

all: directories metapagetable.h $(EXE)

clean:
	rm -f metapagetable.h
	rm -f $(OBJS)
	rm -f $(DEPS)
	rm $(EXE)

$(EXE): $(OBJS)
	$(LINKER) $(OBJS) $(LDFLAGS) -o $@
#ar crv $@ $(OBJS)
#ranlib $@

-include $(DEPS)

obj/%.lo: %.c
	$(CC) $(INCLUDES) $(CFLAGS) -MT $@ -MMD -o $@ $<

metapagetable.h: metapagetable.h.in linker-options.in config_metapagetable.cmake
	rm -f metapagetable.h
	rm -f linker-options
	cmake $(METALLOC_OPTIONS) -P ./config_metapagetable.cmake

touch_config:
	touch metapagetable.h.in
	touch linker-options.in

config: touch_config
	$(MAKE) metapagetable.h

directories:
	mkdir -p obj


