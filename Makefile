CC=gcc
CFLAGS=-O2 -Wall -Wextra -std=c99 -pedantic
LD=gcc
LDFLAGS=

.PHONY: all-win
all-win: gdrw.exe

gdrw.exe: gdrw.o serial_win.o
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	rm -f gdrw.exe gdrw.o serial_win.o
