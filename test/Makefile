CXX=clang++
CXXFLAGS=-g -fdebug-types-section -I../extern/folly -O2 -pthread -no-pie
FILTER ?=

TARGETS=integration_mttest integration_sleepy integration_packed integration_cycles mapiter mttest1 mttest2 mttest2_inline mttest3 mttest4 tester userDef1 vector inlined_test

mttest2_inline: CXXFLAGS+=-DMTTEST2_INLINE_DO_STUFF
all: $(TARGETS)

clean:
	rm -f $(TARGETS)

test-integration: integration_mttest integration_sleepy
	sudo python3 integration.py -k '$(FILTER)'
