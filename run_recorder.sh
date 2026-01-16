#!/bin/bash

SERVER_APP="bin/receive_server"
CLIENT_APP="bin/send_client"
usage() {
    echo "Usage: $0 [--patched]"
    echo
    echo "Options:"
    echo "  --patched     run the server in patched mode (handles EAGAIN/EWOULDBLOCK)"
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

echo "[runner] Recording the syscalls made by the server ($SERVER_APP) $PATCHED while it interacts with the client ($CLIENT_APP) "
echo " "

# Start server under recorder
shim/bin/recorder "$SERVER_APP" $PATCHED & "$CLIENT_APP" 127.0.0.1 >/dev/null 2>&1 &
SERVER_PID=$!
# Wait for server to exit
wait $SERVER_PID
echo " "
echo " "
echo "[runner] System call trace is saved in trace.bin"
echo "[runner] Done."

