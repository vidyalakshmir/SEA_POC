CC=gcc

CFLAGS = -Wall -fPIC

RECORD_SYSCALL = recorder


all: $(RECORD_SYSCALL)

$(RECORD_SYSCALL) : recorder.c
		$(CC) $(CFLAGS) recorder.c -o $(RECORD_SYSCALL)

clean:
	rm -f $(RECORD_SYSCALL)

.PHONY: all clean

