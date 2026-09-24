# c64-git - Makefile
#
#   make            build host tool (git64tool) + C64 binary (GIT64.PRG)
#   make tool       host tool only (needs cc + zlib)
#   make prg        C64 binary only (needs cl65/cc65)
#   make test       full PoC test: fixture repo -> pak -> host report ->
#                   d64 -> VICE run -> diff (needs c1541 + x64sc)
#   make test-host  host-only validation (no emulator needed)

SRC_COMMON := src/pak.c src/gitobj.c src/report.c src/sha1.c src/puff.c
BUILD      := build

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
CL65    ?= cl65
C1541   ?= c1541
X64SC   ?= x64sc

all: $(BUILD)/git64tool $(BUILD)/GIT64.PRG

tool: $(BUILD)/git64tool
prg:  $(BUILD)/GIT64.PRG

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/git64tool: src/host/git64tool.c $(SRC_COMMON) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ -lz

$(BUILD)/GIT64.PRG: src/c64/git64.c $(SRC_COMMON) | $(BUILD)
	$(CL65) -t c64 -O -o $@ $^

test: all
	tests/run_tests.sh

test-host: $(BUILD)/git64tool
	tests/run_tests.sh --host-only

clean:
	rm -rf $(BUILD)

.PHONY: all tool prg test test-host clean
