CC=gcc

CFLAGS = -Wall -fPIC

RECORD_SYSCALL = recorder
MUTATE_SYSCALL = mutator
REPLAY_SYSCALL = replayer


all: $(RECORD_SYSCALL) $(MUTATE_SYSCALL) $(REPLAY_SYSCALL)

$(RECORD_SYSCALL) : recorder.c
		$(CC) $(CFLAGS) recorder.c -o $(RECORD_SYSCALL)

$(MUTATE_SYSCALL) : mutator.c
		$(CC) $(CFLAGS) mutator.c -o $(MUTATE_SYSCALL)

$(REPLAY_SYSCALL) : replayer.c
		$(CC) $(CFLAGS) replayer.c -o $(REPLAY_SYSCALL)

clean:
	rm -f $(RECORD_SYSCALL) $(MUTATE_SYSCALL) $(REPLAY_SYSCALL)

.PHONY: all clean

