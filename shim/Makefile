CC=gcc

CFLAGS = -Wall -fPIC
LDFLAGS = -ldl

TRACE_SO = trace_syscalls


all: $(TRACE_SO)

$(TRACE_SO) : trace_syscalls.c
		$(CC) $(CFLAGS) trace_syscalls.c -o $(TRACE_SO) $(LDFLAGS)

clean:
	rm -f $(RECORD_SO) $(REPLAY_SO) $(TRACE_SO)

.PHONY: all clean

