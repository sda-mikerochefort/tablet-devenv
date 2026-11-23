CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -Wno-missing-field-initializers -std=gnu11
usb-emu: usb-emu.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread
clean:
	rm -f usb-emu
.PHONY: clean
