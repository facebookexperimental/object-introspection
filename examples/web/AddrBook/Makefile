.DEFAULT_GOAL := addrbook
CC = clang++
CFLAGS = -std=c++20 -g -O3
SRC = AddrBook.cpp

default: addrbook


.PHONY: addrbook
addrbook: $(SRC)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm addrbook
