# FTLCheck — build with:  make            (bin/ftlcheck.exe)
#                         make test       (run all tests + HTML report)
#                         make demo       (run the narrated demo)
#                         make memguard   (build with MemGuard as the allocator)
#                         make clean

CXX      = g++
CC       = gcc
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
BIN      = bin

SRCS = src/nand/nand_device.cpp \
       src/ftl/ftl.cpp \
       src/framework/test_registry.cpp \
       src/framework/test_runner.cpp \
       src/framework/report.cpp \
       src/tests/basic_io_tests.cpp \
       src/tests/gc_tests.cpp \
       src/tests/wear_tests.cpp \
       src/tests/bad_block_tests.cpp \
       src/tests/power_loss_tests.cpp \
       src/main.cpp

all: $(BIN)/ftlcheck.exe

$(BIN):
	mkdir -p $(BIN)

$(BIN)/ftlcheck.exe: $(SRCS) | $(BIN)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@

test: $(BIN)/ftlcheck.exe
	$(BIN)/ftlcheck.exe run --report $(BIN)/report.html

demo: $(BIN)/ftlcheck.exe
	$(BIN)/ftlcheck.exe demo

# --- integration with project 2: MemGuard becomes the allocator ---
$(BIN)/memguard_pool.o: ../memguard/src/memguard.c ../memguard/src/memguard.h | $(BIN)
	$(CC) -std=c99 -Wall -Wextra -O2 -g -DMG_POOL_SIZE=8388608 \
	    -c ../memguard/src/memguard.c -o $@

memguard: $(BIN)/ftlcheck-mg.exe

$(BIN)/ftlcheck-mg.exe: $(SRCS) extras/memguard_newdelete.cpp $(BIN)/memguard_pool.o
	$(CXX) $(CXXFLAGS) -DFTL_WITH_MEMGUARD $(SRCS) \
	    extras/memguard_newdelete.cpp $(BIN)/memguard_pool.o -o $@

clean:
	rm -rf $(BIN)

.PHONY: all test demo memguard clean
