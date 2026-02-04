#!/bin/bash

usage() {
    echo "Usage: $0 [--patched]"
    echo
    echo "Options:"
    echo "  --patched     Replay the server in patched mode (handles EAGAIN/EWOULDBLOCK)"
    echo "  -h, --help    display this help message"
    exit 0
}

PATCHED=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --patched)
            PATCHED="--patched"
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# 1. Setup names
TRACE_FILE="trace.bin"
TO_MUTATOR="pipe_to_mutator"
FROM_MUTATOR="pipe_from_mutator"
APP_BIN="bin/receive_server"
REPLAYER_BIN="shim/bin/replayer"

# 2. Clean up old pipes and create new ones
rm -f $TO_MUTATOR $FROM_MUTATOR
mkfifo $TO_MUTATOR
mkfifo $FROM_MUTATOR

echo "[*] Starting Mutator"
# The Mutator reads from TO_MUTATOR and writes to FROM_MUTATOR

 shim/bin/mutator trace.bin < pipe_to_mutator > pipe_from_mutator 2>/dev/null &
MUTATOR_PID=$!

echo "[*] Launching Replayer to run $APP_BIN..."
# The Replayer writes to TO_MUTATOR and reads from FROM_MUTATOR
<<<<<<< HEAD
$REPLAYER_BIN $TO_MUTATOR $FROM_MUTATOR $APP_BIN $PATCHED 2>/dev/null
=======
$REPLAYER_BIN $TO_MUTATOR $FROM_MUTATOR $APP_BIN $PATCHED 2> /dev/null
>>>>>>> bbfd678b0117f5b0e6594e2faceedff15ad58dfc


# 3. Cleanup
kill $MUTATOR_PID  2>/dev/null
rm $TO_MUTATOR $FROM_MUTATOR
echo "[*] Replay complete."
