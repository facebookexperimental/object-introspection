CXX=clang++
CXXFLAGS=-g -fdebug-types-section -I../extern/folly -O2 -pthread -no-pie
FILTER ?=

TARGETS=integration_mttest integration_sleepy

all: $(TARGETS)

clean:
	rm -f $(TARGETS)

test-integration: integration_mttest integration_sleepy
	sudo python3 integration.py -k '$(FILTER)'
