#!/bin/bash

# 1. Setup names
TRACE_FILE="trace.bin"
TO_MUTATOR="pipe_to_mutator"
FROM_MUTATOR="pipe_from_mutator"
APP_BIN=$1
REPLAYER_BIN="shim/bin/replayer"

# 2. Clean up old pipes and create new ones
rm -f $TO_MUTATOR $FROM_MUTATOR
mkfifo $TO_MUTATOR
mkfifo $FROM_MUTATOR

echo "[*] Starting Mutator"
# The Mutator reads from TO_MUTATOR and writes to FROM_MUTATOR

 shim/bin/mutator trace.bin < pipe_to_mutator > pipe_from_mutator  2>/dev/null &
MUTATOR_PID=$!

echo "[*] Launching Replayer to run $APP_BIN..."
# The Replayer writes to TO_MUTATOR and reads from FROM_MUTATOR
$REPLAYER_BIN $TO_MUTATOR $FROM_MUTATOR $APP_BIN


# 3. Cleanup
kill $MUTATOR_PID
rm $TO_MUTATOR $FROM_MUTATOR
echo "[*] Replay complete."
