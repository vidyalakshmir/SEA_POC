# SEA_POC
The goal of this POC is to test if a program run on Linux would run correctly on macOS, etc. given the different socket flag inheritance behavior. MacOS and BSD sockets implementation, flags of listening sockets such as `O_NONBLOCK` are inherited by connection sockets while in Linux, the newly created connection sockets returned by `accept()` does not inherit flags of listening sockets such as `O_NONBLOCK`.  Since a non-blocking socket may raise errors like `EWOULDBLOCK` or `EAGAIN` which the application may not expect, this can cause bugs in applications in practice [[1]](#ref1).      

## Flawed server program + client
The goal of this program is to accept a socket and check to see if the `O_NONBLOCK` flag is inherited by the client socket from a listen call.  However, rather than just look directly at the flag on the new socket, we will use the behavior of recv to determine this.  The way this works in the initial trace-capture run is as follows.

The server and the client programs are executed. The server program creates a non-blocking socket. After `bind()` and `listen()` invocations, the server invokes `accept()` in a loop. In the meantime, the client sleeps, so the server has a chance to start, and then tries to connect to it.  At this point the server’s `accept()` will be successful and the server invokes `recv()` on the client socket. Since the client has not sent any data at this point, `recv()` will either block (if the socket is blocking) or return `EWOULDBLOCK` / `EAGAIN`, which causes an error in the code.  After sleeping a few seconds, the client sends a message to the server and both exit.

### Build the server and client
Run `make` from the repo root directory to build the server and client programs. The server and client binary are created in the `bin` directory.
              
### How to run  

1. Run the script runner.sh
   ```
   ./runner.sh bin/receive_server
   ```
This would invoke both server and client program.

### Expected Behavior (Tested on Linux)
Server invokes accept() in a loop and sleeps for 1  second until the client connects. The server then blocks or waits to receive data from the client and once received, both the server and the client program exits.

### Observed Behavior in MacOS
In macOS, since the client socket inherits `O_NONBLOCK` flag from the listening socket, the `recv()` call is non-blocking. Since the server just exits if `recv()` fails without specifically handling specific errors like `EWOULDBLOCK` / `EAGAIN`, the server exits with an error saying `Receive failed: Resource temporarily unavailable`.

## Patched Server 
A patched server program is provided which handles cases where if `recv()` call is non-blocking and fails with errors `EWOULDBLOCK` / `EAGAIN`, would cause the server to sleep for 1 second, and then invoke `recv()` again.

To run this patched server with client run the script
```
   ./runner.sh bin/patched_receive_server
```

This should succeed in both Linux and macOS.

## Simulating Execution Anomalies using Record and Replay (RR)
A record-and-replay system enables the deterministic execution of programs by capturing and replaying its external interactions. SEA(Simulating Environmental Anomalies) is a technique for simulating environments so an application's behavior in those environments can be assessed before deployment. Because an application’s interaction with external resources—such as the filesystem, network, and operating system—is mediated entirely through system calls, SEA works by intercepting these calls and mutating their return values. By injecting "anomalous" data or error codes (fault injection) that represent real-world failures, SEA allows developers to assess how an application handles environmental stress and edge cases without the need for expensive or complex physical testbeds.


Inorder to simulate the above anomaly, we have created a recorder, replayer and mutator program.

### Build the recorder, replayer and mutator
```
cd shim
make
```

### Run the recorder program
The recorder program records all system calls required to replay the program successfully. It records the system call arguments, return value, error code as well as data copied to userspace after the execution of the system call. It also keeps track of the sequence number of system calls invoked by the program. This is also stored in the trace file. To run the recorder program, from the repo home directory, execute the command

```
./run_recorder.sh bin/receive_server
```

This script records the system calls of the  flawed server program and creates the trace file `trace.bin` in the repo home directory.


### Run the replayer and mutator program
The replayer program traces the program that needs to be analyzed(in this case the flawed server program) using ptrace. It keeps track of a counter which determines the sequence number of each system call. At each system call entry, the replayer sends the sequence number as well as the system call number to the mutator program through pipes. The mutator program meanwhile reads the `trace.bin` and when it receives the sequence number and the system call number from the replayer program, reads the next trace record in `trace.bin` and compares if the sequence number and system call number matches with the one send from the replayer program. If it doesn't match, the mutator sends signal 0 saying to ignore the system call. Essentially, in these case, the replayer does nothing and the system call gets executed. This is the case of system calls like `write()` which writes to the console.

If the mutator finds a match, it sends the system call record to the replayer, which then uses that information to replay that system call. In case of the system call `recv`, the mutator changes the error code to `EWOULDBLOCK` and the return value to -1 to simulate the anomaly. In this way, the anomaly is simulated and the program behavior is observed.

```
./run_replay.sh bin/receive_server
```

<a name="ref1"></a>
[1] https://github.com/dotnet/runtime/issues/25069


