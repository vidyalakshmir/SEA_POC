#!/bin/bash
SERVER=$1
# Start the server and client in the background
$SERVER & bin/send_client 127.0.0.1

