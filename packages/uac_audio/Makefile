CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
CFLAGS := -O2 -std=c11 -w -Isrc
LIBS := -ldl -pthread -lm

.PHONY: all clean

all: uac_device_loop

uac_device_loop: src/uac_device_loop.c src/miniaudio.h
	$(CC) $(CFLAGS) -o $@ src/uac_device_loop.c $(LIBS)

clean:
	rm -f uac_device_loop
