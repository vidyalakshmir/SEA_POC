# POC for Simulating Environmental Anomalies (SEA)

## TLDR

This is a proof-of-concept demonstration for a technique to take a program running in one environment (operating system, network type, etc.) and find bugs that would occur when it runs in a different environment.  This example takes code that runs correctly on Linux, but will error on a Mac or BSD system, discovering the bug while testing only on Linux.

To try it, check out the code, run `make` and `./runner.sh`.

## Introduction
By some estimates, as many as 80% of software bugs are discovered after deployment - an alarming statistic given that such bugs cost 15-60× more to fix than those identified earlier in the development process. A major reason for this disparity is that many applications contain bugs that are specific to the environments in which they are deployed. While conventional testing can uncover logical errors, some flaws manifest only under very specific environmental conditions.

Simulating Environmental Anomalies (SEA) is a technique designed to expose such environment-dependent bugs by simulating an application’s behavior under diverse deployment conditions. Because an application’s interaction with external resources—such as the filesystem, network, and operating system—is mediated entirely through system calls, SEA operates by recording these system calls and selectively mutating their outcomes. By injecting anomalous data or error codes  that model real-world failures (i.e., fault injection) by altering system call return values and error codes, SEA enables developers to evaluate how applications respond to environmental stress and edge cases without relying on costly or complex physical testbeds.


This proof of concept (PoC) presents a minimal implementation of SEA that simulates one such environmental anomaly. The goal of this PoC is to evaluate whether a server program developed and tested on Linux would behave correctly when executed on macOS (or BSD-based systems), given differences in socket flag inheritance semantics. In macOS and BSD socket implementations, flags set on a listening socket such as `O_NONBLOCK` are inherited by the connection sockets (client sockets) returned by `accept()`. In contrast, on Linux, newly created connection sockets do not inherit flags from the listening socket. Programs that are not designed with this cross-platform behavioral difference in mind may therefore exhibit incorrect behavior when ported across environments. Specifically, non-blocking sockets can produce errors such as `EWOULDBLOCK` or `EAGAIN` that applications may not anticipate or handle correctly. As a result, these semantic differences can lead to subtle, environment-dependent bugs that are difficult to detect through conventional testing. [[1]](#ref1).   

In this POC, we simulate the non-blocking behavior of client sockets by mutating the return value and error code of the `recv()` call invoked by the server to receive client data. The application under test is a simple server program that does not account for differences in socket flag inheritance across operating systems and does not explicitly handle whether the client socket returned by `accept()` is blocking or non-blocking. The server waits for a client connection using `accept()`, invokes `recv()` to receive data, prints the received data upon success, and then exits. If `recv()` returns an error, the server terminates immediately, without handling non-blocking semantics such as `EWOULDBLOCK` or `EAGAIN`.

The SEA framework used in this PoC consists of several components. A recorder captures all system calls executed by the server during a normal execution, which is driven by a client program that connects to the server and transmits data. A mutator consumes the recorded system call trace and determines how individual system calls should be modified during replay. A replayer then re-executes the server deterministically by tracing it with `ptrace`, intercepting each system call, and queries the mutator for the appropriate action. Based on the mutator’s response, the replayer either replays the system call using recorded or mutated information (e.g., return values, error codes, or data written to user space), or allows the system call to execute normally.

For this PoC, the replayer supports only the subset of system calls required by the server: `socket`, `fcntl`, `setsockopt`, `bind`, `listen`, `accept`, `nanosleep`, and `recvfrom`. In particular, it mutates the return value and error code of `recvfrom`, which underlies the function call`recv()`, to simulate the non-blocking behavior of client sockets and expose environment-dependent bugs.

Overall, this record-and-replay approach enables deterministic execution by capturing and selectively replaying a program’s external interactions, providing a controlled mechanism for simulating environmental anomalies. The current PoC is limited to `single-threaded applications`; extending the framework to support multi-threaded programs would require additional mechanisms to capture and replay thread scheduling and synchronization behavior. The code is currently written in C, but future versions will be implemented in Rust.

## Building all programs
Run `make` from the repo root directory to build the server, client, recorder, mutator and replayer programs. The server and client binaries are created in the `bin` directory, while the recorder, mutator and replayer binaries are created within `shim/bin` directory.

```
make
```


## Executing the server program + client
This program implements a simple server that creates a non-blocking listening socket and accepts incoming client connections. The server does not explicitly account for whether the client socket returned by `accept()` is blocking or non-blocking, and instead immediately invokes `recv()` on the accepted socket. The interaction between the server and client during the initial trace-capture run proceeds as follows.

The server and client programs are executed concurrently. The server creates a listening socket, marks it as non-blocking, and then invokes `bind()` and `listen()`. It subsequently enters a loop in which it repeatedly calls `accept()` to wait for incoming connections. Meanwhile, the client initially sleeps to allow the server sufficient time to start listening, and then attempts to connect to the server.

Once the client connects, the server’s `accept()` call succeeds and returns a client socket. The server then invokes `recv()` on this socket. If `recv()` succeeds - that is, if data has already been sent by the client - the server reads .the message, prints it, and exits normally. If `recv()` fails (return value is -1), the server treats this as an error and exits. Such a failure may occur either because the client has not yet sent any data (in the case of a non-blocking socket, where `recv()` sets errno to be `EWOULDBLOCK` or `EAGAIN`) or due to other error conditions.

Run the following command from the repo home directory to invoke both server and client programs.
   ```
   ./runner.sh
   ```


### Observed Behavior
In Linux, the server blocks or waits to receive data from the client and once received, prints the message and exits. This is because the client socket is by default bocking and doesn't inherit its blocking/non-behaving behavior from the listening socket. While in macOS, since the client socket inherits `O_NONBLOCK` flag from the listening socket, the `recv()` call is non-blocking. Since the server just exits if `recv()` fails without specifically handling specific errors like `EWOULDBLOCK` / `EAGAIN`, the server exits with an error saying `recv failed: errno=11 (EWOULDBLOCK): Resource temporarily unavailable`.

Adding `--patched` flag to `runner.sh` script runs the patched server, which handles the non-blocking behavior of the client socket.
```
   ./runner.sh --patched
```

This patched server would exhibit correct behavior in both Linux and macOS.

## Simulating Anomalies using Record and Replay (RR) and mutator
We simulate this anomaly—where the server does not handle the non-blocking behavior of a client socket inherited from the listening socket—using record-and-replay (RR) and mutator programs. The recorder program records all system calls required to replay the program successfully. It records the system call arguments, return value, error code as well as data copied to userspace after the execution of the system call. It also keeps track of the sequence number of system calls invoked by the program. This is also stored in the trace file. To run the recorder program, from the repo home directory, execute the command

```
./run_recorder.sh
```

This script records the system calls of the flawed server program and creates the trace file `trace.bin` in the repo home directory.


The replayer program traces the program that needs to be analyzed using `ptrace`. It keeps track of a counter which determines the sequence number of each system call. At each system call entry, the replayer sends the sequence number as well as the system call number to the mutator program through pipes. The mutator program meanwhile reads the `trace.bin` and when it receives the sequence number and the system call number from the replayer program, reads the next trace record in `trace.bin` and compares if the sequence number and system call number matches with the one send from the replayer program. If it doesn't match, the mutator sends signal 0 saying to ignore the system call. Essentially, in this case, the replayer does nothing and the system call gets executed. This is the case of system calls like `write()` which writes to the console.

If the mutator finds a match, it sends the system call record to the replayer, which then uses that information to replay that system call without executing the system call. In case of the system call `recv`, the mutator changes the error code to `EWOULDBLOCK` and the return value to -1 to simulate the anomaly. In this way, the anomaly is simulated and the program behavior is observed.

```
./run_replay.sh
```

The replayer should produce the output
`SERVER: recv failed: errno=11 (EWOULDBLOCK): Resource temporarily unavailable`

To pretty print a system call trace stored in `trace.bin`
```
shim/bin/print_trace trace.bin
```

To observe patched server's behavior when this anomaly is introduced, run the commands

```
./run_recorder.sh --patched
./run_replay.sh --patched
```

This records, mutates, and replays the execution of a patched server. During replay, when the mutator changes the return value of the `recvfrom` system call to -1 with the error code `EWOULDBLOCK`, the program diverges from the originally recorded execution path. In the patched server, this condition is handled correctly: the server sleeps for one second to accommodate non-blocking `recv()` semantics and then repeatedly invokes `recv()` until it succeeds.

In our PoC, whenever such divergence occurs, specifically when the mutator alters the return value or error code of a system call and the execution deviates from the recorded path at a program point (denoted as point C), we pause deterministic replay. The program is then allowed to execute freely until it reaches program point C again, at which point deterministic replay is resumed.

In contrast, the flawed server fails to handle the non-blocking `recv()` and hence and exits immediately with an error. The patched server, however, correctly sleeps and retries `recv()`, eventually re-entering the recorded execution path, at which point replay continues deterministically.

<a name="ref1"></a>
[1] https://github.com/dotnet/runtime/issues/25069


