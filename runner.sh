#!/bin/bash
SERVER="bin/receive_server"

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

# Start the server and client in the background
$SERVER $PATCHED & bin/send_client 127.0.0.1