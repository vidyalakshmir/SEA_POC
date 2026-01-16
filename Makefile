CC = gcc

BIN_DIR = bin
SHIM_BIN_DIR = shim/bin

RECEIVE_SERVER = $(BIN_DIR)/receive_server
SEND_CLIENT = $(BIN_DIR)/send_client
RECORD_SYSCALL = $(SHIM_BIN_DIR)/recorder
MUTATE_SYSCALL = $(SHIM_BIN_DIR)/mutator
REPLAY_SYSCALL = $(SHIM_BIN_DIR)/replayer


# The 'all' target needs to know what it's building
all: $(RECEIVE_SERVER) $(SEND_CLIENT) $(RECORD_SYSCALL) $(MUTATE_SYSCALL) $(REPLAY_SYSCALL)

$(RECEIVE_SERVER): src/receive_server.c
	@mkdir -p $(BIN_DIR)
	$(CC) src/receive_server.c -o $(RECEIVE_SERVER)


$(SEND_CLIENT): src/send_client.c
	@mkdir -p $(BIN_DIR)
	$(CC) src/send_client.c -o $(SEND_CLIENT)

$(RECORD_SYSCALL) : shim/recorder.c
		@mkdir -p $(SHIM_BIN_DIR)
		$(CC) $(CFLAGS) shim/recorder.c -o $(RECORD_SYSCALL)

$(MUTATE_SYSCALL) : shim/mutator.c
		@mkdir -p $(SHIM_BIN_DIR)
		$(CC) $(CFLAGS) shim/mutator.c -o $(MUTATE_SYSCALL)

$(REPLAY_SYSCALL) : shim/replayer.c
		@mkdir -p $(SHIM_BIN_DIR)
		$(CC) $(CFLAGS) shim/replayer.c -o $(REPLAY_SYSCALL)

clean:
	rm -rf $(BIN_DIR) $(SHIM_BIN_DIR)

# Phony targets prevent issues if files named 'all' or 'clean' exist
.PHONY: all clean
