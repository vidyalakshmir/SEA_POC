CC=gcc

CFLAGS = -Wall -fPIC
BIN_DIR = bin

RECORD_SYSCALL = $(BIN_DIR)/recorder
MUTATE_SYSCALL = $(BIN_DIR)/mutator
REPLAY_SYSCALL = $(BIN_DIR)/replayer


all: $(RECORD_SYSCALL) $(MUTATE_SYSCALL) $(REPLAY_SYSCALL)

$(RECORD_SYSCALL) : recorder.c
		@mkdir -p $(BIN_DIR)
		$(CC) $(CFLAGS) recorder.c -o $(RECORD_SYSCALL)

$(MUTATE_SYSCALL) : mutator.c
		@mkdir -p $(BIN_DIR)
		$(CC) $(CFLAGS) mutator.c -o $(MUTATE_SYSCALL)

$(REPLAY_SYSCALL) : replayer.c
		@mkdir -p $(BIN_DIR)
		$(CC) $(CFLAGS) replayer.c -o $(REPLAY_SYSCALL)

clean:
	rm -f $(RECORD_SYSCALL) $(MUTATE_SYSCALL) $(REPLAY_SYSCALL)

.PHONY: all clean

