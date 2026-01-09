CC = gcc
# Define the paths for your binaries
BIN_DIR = bin
RECEIVE_SERVER = $(BIN_DIR)/receive_server
SEND_CLIENT = $(BIN_DIR)/send_client

# The 'all' target needs to know what it's building
all: $(RECEIVE_SERVER) $(SEND_CLIENT)

$(RECEIVE_SERVER): src/receive_server.c
	@mkdir -p $(BIN_DIR)
	$(CC) src/receive_server.c -o $(RECEIVE_SERVER)

$(SEND_CLIENT): src/send_client.c
	@mkdir -p $(BIN_DIR)
	$(CC) src/send_client.c -o $(SEND_CLIENT)

clean:
	rm -rf $(BIN_DIR)

# Phony targets prevent issues if files named 'all' or 'clean' exist
.PHONY: all clean
