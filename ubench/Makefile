.PHONY: all clean

all: ubench$(SUFFIX)

clean:
	rm -f ubench *.o ubench-gen.h ubench-gen-inc.h

ubench$(SUFFIX): ubench$(SUFFIX).o
	$(CXX) $(LDFLAGS) -o $@ $^

ubench-gen.h ubench-gen-inc.h: ubench-gen.sh
	./ubench-gen.sh

ubench$(SUFFIX).o: ubench.cc ubench-gen.h ubench-gen-inc.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<
