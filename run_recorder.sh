#!/bin/bash
APP=$1
# Start the server and client in the background
shim/bin/recorder $1 & bin/send_client 127.0.0.1


