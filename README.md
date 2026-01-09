# SEA_POC
The goal of this POC is to test if a program run on Linux would run correctly on macOS, etc. given the different socket flag inheritance behavior. In macOS, which uses BSD sockets implementation, flags of listening sockets such as `O_NONBLOCK` are inherited by connection sockets while in Linux, the newly created connection sockets returned by `accept()` does not inherit flags of listening sockets such as `O_NONBLOCK`.  Since a non-blocking socket may raise errors like `EWOULDBLOCK` or `EAGAIN` which the application may not expect, this can cause bugs in applications in practice [[1]](#ref1).      

## Flawed server program + client
The goal of this program is to accept a socket and check to see if the `O_NONBLOCK` flag is inherited by the client socket from a listen call.  However, rather than just look directly at the flag on the new socket, we will use the behavior of recv to determine this.  The way this works in the initial trace-capture run is as follows.

The server and the client programs are executed. The server program creates a non-blocking socket. After `bind()` and `listen()` invocations, the server invokes `accept()` in a loop. In the meantime, the client sleeps, so the server has a chance to start, and then tries to connect to it.  At this point the server’s `accept()` will be successful and the server invokes `recv()` on the client socket. Since the client has not sent any data at this point, `recv()` will either block (if the socket is blocking) or return `EWOULDBLOCK` / `EAGAIN`, which causes an error in the code.  After sleeping a few seconds, the client sends a message to the server and both exit.

## Build the server and client
Run `make` from the repo root directory to build the server and client programs. 
              
## How to run  

1. Start the server
   ```
   bin/receive_server
   ```

2. Start the client
   ```
   bin/send_client 127.0.0.1
   ```

## Expected Behavior (Tested on Linux)
Server invokes accept() in a loop and sleeps for 1  second until the client connects. The server then blocks or waits to receive data from the client and once received, both the server and the client program exits.

<a name="ref1"></a>
[1] https://github.com/dotnet/runtime/issues/25069


