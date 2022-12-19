.DEFAULT_GOAL := all

CXX=clang++
CXXFLAGS=-g -std=c++17 -Wall -pedantic -DOIL_AOT_COMPILATION=1

.PHONY: oilgen

INC=-I../../include

OilVectorOfStrings.o: OilVectorOfStrings.cpp
	${CXX} ${CXXFLAGS} ${INC} OilVectorOfStrings.cpp -c -o OilVectorOfStrings.o

oilgen:
	rm -f oilgen
	(cd ../../ && make oid-devel)
	ln -s ../../build/oilgen oilgen

JitCompiled.o: oilgen OilVectorOfStrings.o
	DRGN_ENABLE_TYPE_ITERATOR=1 ./oilgen -o JitCompiled.o -c ../../build/sample.oid.toml -d OilVectorOfStrings.o

OilVectorOfStrings: OilVectorOfStrings.o JitCompiled.o
	${CXX} ${CXXFLAGS} OilVectorOfStrings.o JitCompiled.o -o OilVectorOfStrings

all: OilVectorOfStrings

clean:
	rm -f oilgen OilVectorOfStrings{,.o,.o.dwarf} JitCompiled.o jit.cpp

