#!/bin/bash

# Start the server and client in the background
bin/receive_server & bin/send_client 127.0.0.1

