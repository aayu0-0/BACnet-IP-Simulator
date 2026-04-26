# ── BACnet Simulator Makefile ────────────────────────────────
CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c99
LDFLAGS = -lm
SRC     = src/bacnet_sim.c
TARGET  = bacnet_sim

# Windows cross-compile (from Linux)
WIN_CC  = x86_64-w64-mingw32-gcc
WIN_OUT = bacnet_sim.exe

.PHONY: all clean windows

all:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)
	@echo "Built: ./$(TARGET)"

windows:
	$(WIN_CC) $(CFLAGS) -o $(WIN_OUT) $(SRC) -lws2_32 -lm
	@echo "Built: ./$(WIN_OUT)"

clean:
	rm -f $(TARGET) $(WIN_OUT)
