/*
 * This program records system calls of a target program passed as an argument.
 * It specifically traces networking-related system calls (e.g., socket, fcntl, setsockopt, bind, listen, accept, recvfrom)
 * and stores their arguments, return values, errno and any data written to user space by the kernel
 * during syscall execution in a binary file called 'trace.bin'.
 */

#include <stdint.h>
#include <sys/socket.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/ptrace.h>
#include <stdio.h>
#include <errno.h>
/**
 * recorder.c
 *
 * This program traces the execution of a target application passed as an argument,
 * capturing its system calls for later replay. It focuses on networking-related
 * system calls (e.g., socket, fcntl, setsockopt, bind, listen, accept, recvfrom)
 * but can be extended to other calls as needed.
 *
 * For each system call, the recorder captures:
 *   - The system call arguments
 *   - The return value
 *   - The error code (errno) if any
 *   - Any data copied to user space by the kernel
 *   - A sequence number to preserve the original execution order
 *
 * All captured information is serialized and stored in a binary trace file
 * (trace.bin), which can then be consumed by the mutator and replayer
 * to analyze, modify, or replay the program’s execution.
 */

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include "trace_defs.h"

/* This struct is used to store the system call arguments passed through registers at system call entry. */
struct saved_args_t
{
	long rdi, rsi, rdx, r10, r8, r9;
};

/* This global variable is used to record the sequence number of the system call. It is incremented at
 * each system call entry.
 */
static uint64_t global_seqno = 0;

/**
 * record_socket
 *
 * Records a socket() system call.
 * Captures:
 *   - Domain, type, protocol arguments
 *   - Return value (file descriptor or -1 on error)
 *   - errno value if socket() fails
 *   - Sequence number of the call
 *
 * Stores this information in the trace file.
 */
void record_socket(FILE *fp, syscall_type_t type, struct saved_args_t regs, int64_t ret_val, int saved_errno)
{
	record_header_t header;
	header.seq_num = global_seqno;
	header.type = type;
	header.ret_val = ret_val;
	header.saved_errno = saved_errno;
	header.body_len = sizeof(socket_data_t);

	socket_data_t body = {0};
	body.domain = regs.rdi;
	body.type = regs.rsi;
	body.protocol = regs.rdx;

	if (fwrite(&header, sizeof(header), 1, fp) != 1)
	{
		perror("fwrite header failed");
	}
	if (fwrite(&body, sizeof(body), 1, fp) != 1)
	{
		perror("fwrite body failed");
	}
	fflush(fp);
}

/**
 * record_fcntl
 *
 * Records an fcntl() system call.
 * Captures:
 *   - File descriptor and command arguments
 *   - Return value
 *   - errno if the call fails
 *   - Sequence number
 *
 * Useful for tracking flags or file descriptor operations on sockets.
 */
void record_fcntl(FILE *fp, syscall_type_t type, struct saved_args_t regs, int64_t ret_val, int saved_errno)
{
	record_header_t header;
	header.seq_num = global_seqno;
	header.type = type;
	header.ret_val = ret_val;
	header.saved_errno = saved_errno;
	header.body_len = sizeof(fcntl_data_t);

	fcntl_data_t body = {0};
	body.fd = regs.rdi;
	body.cmd = regs.rsi;
	body.arg = regs.rdx; // Currently do not track `struct flock` which is stored to arg when cmd = F_GETLK

	if (fwrite(&header, sizeof(header), 1, fp) != 1)
	{
		perror("fwrite header failed");
	}
	if (fwrite(&body, sizeof(body), 1, fp) != 1)
	{
		perror("fwrite body failed");
	}
	fflush(fp);
}

/**
 * record_setsockopt
 *
 * Records a setsockopt() system call.
 * Captures:
 *   - Socket file descriptor, option name and level
 *   - Option value (data passed to kernel)
 *   - Return value and errno if the call fails
 *   - Sequence number
 *
 */
void record_setsockopt(FILE *fp, syscall_type_t type, struct saved_args_t regs, int64_t ret_val, int saved_errno, pid_t child)
{
	record_header_t header;
	header.seq_num = global_seqno;
	header.type = type;
	header.ret_val = ret_val;
	header.saved_errno = saved_errno;
	header.body_len = sizeof(setsockopt_data_t);

	setsockopt_data_t body = {0};
	body.sockfd = regs.rdi;
	body.level = regs.rsi;
	body.optname = regs.rdx;
	body.optlen = regs.r10;

	long word;		   // Stores one 'word' of data read from the child
	size_t copied = 0; // Keeps track of number of bytes read so far

	/* This loop is used to copy optval from child. This ensures we copy
	 * until body.optlen bytes are copied or till we reach the sizeof body.optval
	 */
	while (copied < body.optlen && copied < sizeof(body.optval))
	{
		/*  errno is set by PTRACE_PEEKDATA if there is an error.
			This resets errno before calling ptrace
		 */
		errno = 0;

		/* Reads one machine word from the child process memory
		 * regs.r8 contains the pointer to optval in the chid
		 * copied provides the offset within optval
		 */
		word = ptrace(PTRACE_PEEKDATA, child, regs.r8 + copied, NULL);

		/* If ptrace failed, stop copying
		 */
		if (errno)
			break;

		/* If the last remaining bytes copied is less than size(word)
		 * we correctly obtain the number of bytes which was actually copied
		 */
		size_t copy_size = (body.optlen - copied < sizeof(word)) ? (body.optlen - copied) : sizeof(word);
		/* Copy the word which was read into body.optval at
		 * position (body.optval + copied)
		 * within body.optval
		 */
		memcpy(body.optval + copied, &word, copy_size);

		// Adds the size of 'word' to keep track of bytes copied so far
		copied += sizeof(word);
	}

	if (fwrite(&header, sizeof(header), 1, fp) != 1)
	{
		perror("fwrite header failed");
	}
	if (fwrite(&body, sizeof(body), 1, fp) != 1)
	{
		perror("fwrite body failed");
	}
	fflush(fp);
}

/**
 * record_bind
 *
 * Records a bind() system call.
 * Captures:
 *   - Socket file descriptor
 *   - Address and port structure passed to kernel
 *   - Return value and errno
 *   - Sequence number
 *
 */

void record_bind(FILE *fp, syscall_type_t type, struct saved_args_t regs, int64_t ret_val, int saved_errno, pid_t child)
{
	record_header_t header;
	header.seq_num = global_seqno;
	header.type = type;
	header.ret_val = ret_val;
	header.saved_errno = saved_errno;
	header.body_len = sizeof(bind_data_t); 

	bind_data_t body = {0};
	body.sockfd = regs.rdi;
	body.addrlen = regs.rdx;

	long word;		   // Stores one 'word' of data read from the child
	size_t copied = 0; // Keeps track of number of bytes read so far

	/* This loop is used to copy addr from child. This ensures we copy
	 * until body.addrlen bytes are copied or till we reach the sizeof body.addr
	 */
	while (copied < body.addrlen && copied < sizeof(body.addr))
	{
		/*  errno is set by PTRACE_PEEKDATA if there is an error.
			This resets errno before calling ptrace
		 */
		errno = 0;

		/* Reads one machine word from the child process memory
		 * regs.rsi contains the pointer to addr in the child
		 * copied provides the offset within addr
		 */
		word = ptrace(PTRACE_PEEKDATA, child, regs.rsi + copied, NULL);

		/* If ptrace failed, stop copying
		 */
		if (errno)
			break;

		/* If the last remaining bytes copied is less than size(word)
		 * we correctly obtain the number of bytes which was actually copied
		 */
		size_t copy_size = (body.addrlen - copied < sizeof(word)) ? (body.addrlen - copied) : sizeof(word);

		/* Copy the word which was read into body.optval at
		 * position (body.optval + copied)
		 * within body.optval
		 */
		memcpy(body.addr + copied, &word, copy_size);

		// Adds the size of 'word' to keep track of bytes copied so far
		copied += sizeof(word);
	}

	if (fwrite(&header, sizeof(header), 1, fp) != 1)
	{
		perror("fwrite header failed");
	}
	if (fwrite(&body, sizeof(body), 1, fp) != 1)
	{
		perror("fwrite body failed");
	}
	fflush(fp);
}

/**
 * record_listen
 *
 * Records a listen() system call.
 * Captures:
 *   - Socket file descriptor
 *   - Backlog argument
 *   - Return value and errno
 *   - Sequence number
 *
 */

void record_listen(FILE *fp, syscall_type_t type, struct saved_args_t regs, int64_t ret_val, int saved_errno)
{
	record_header_t header;
	header.seq_num = global_seqno;
	header.type = type;
	header.ret_val = ret_val;
	header.saved_errno = saved_errno;
	header.body_len = sizeof(listen_data_t);

	listen_data_t body = {0};
	body.sockfd = regs.rdi;
	body.backlog = regs.rsi;

	if (fwrite(&header, sizeof(header), 1, fp) != 1)
	{
		perror("fwrite header failed");
	}
	if (fwrite(&body, sizeof(body), 1, fp) != 1)
	{
		perror("fwrite body failed");
	}
	fflush(fp);
}

/**
 * record_accept
 *
 * Records an accept() system call.
 * Captures:
 *   - Listening socket file descriptor
 *   - Returned client socket file descriptor
 *   - Client address structure
 *   - Return value and errno
 *   - Sequence number
 *
 * Ensures accepted client connections are replayed correctly.
 */

void record_accept(FILE *fp, syscall_type_t type, struct saved_args_t regs,
				   int64_t ret_val, int saved_errno, pid_t child)
{

	record_header_t header;
	header.seq_num = global_seqno;
	header.type = type;
	header.ret_val = ret_val;
	header.saved_errno = saved_errno;
	header.body_len = sizeof(accept_data_t);

	accept_data_t body = {0};
	;
	body.sockfd = regs.rdi;
	body.newfd = ret_val;

	/* This loop is used to copy addr from child. This ensures we copy
	 * until only if accept() is successful and if addrlen and addr is not NULL
	 */
	if (regs.rsi != 0 && regs.rdx != 0 && ret_val >= 0)
	{
		/*  errno is set by PTRACE_PEEKDATA if there is an error.
			This resets errno before calling ptrace
		 */
		errno = 0;

		socklen_t actual_len;

		/* Reads one machine word from the child process memory
		 * regs.r8 contains the pointer to optval in the chid
		 * copied provides the offset within optval
		 */
		long length_word = ptrace(PTRACE_PEEKDATA, child, regs.rdx, NULL);

		/* If ptrace failed, stop copying
		 */
		if (!errno)
		{

			memcpy(&actual_len, &length_word, sizeof(socklen_t));

			if (actual_len > sizeof(body.addr))
				actual_len = sizeof(body.addr);
			body.addrlen = actual_len;

			size_t copied = 0; // Keeps track of number of bytes read so far
			long word;		   // Stores one 'word' of data read from the child

			while (copied < actual_len)
			{
				errno = 0;
				word = ptrace(PTRACE_PEEKDATA, child, regs.rsi + copied, NULL);

				if (errno)
					break;
				/* If the last remaining bytes copied is less than size(word)
				 * we correctly obtain the number of bytes which was actually copied
				 */
				size_t copy_size = actual_len - copied;
				if (copy_size > sizeof(word))
					copy_size = sizeof(word);
				/* Copy the word which was read into body.optval at
				 * position (body.optval + copied)
				 * within body.optval
				 */
				memcpy(body.addr + copied, &word, copy_size);

				// Adds the size of 'word' to keep track of bytes copied so far
				copied += copy_size;
			}
		}
	}

	if (fwrite(&header, sizeof(header), 1, fp) != 1)
	{
		perror("fwrite header failed");
	}
	if (fwrite(&body, sizeof(body), 1, fp) != 1)
	{
		perror("fwrite body failed");
	}
	fflush(fp);
}

/**
 * record_nanosleep
 *
 * Records a nanosleep() system call.
 * Captures:
 *   - Requested sleep time
 *   - Remaining time if interrupted
 *   - Return value and errno
 *   - Sequence number
 *
 * Useful for reproducing timing-dependent behaviors.
 */

void record_nanosleep(FILE *fp, syscall_type_t type, struct saved_args_t args, int64_t ret_val, int saved_errno, pid_t child)
{

	record_header_t header;
	header.seq_num = global_seqno;
	header.type = type;
	header.ret_val = ret_val;
	header.saved_errno = saved_errno;
	header.body_len = sizeof(nanosleep_data_t);

	nanosleep_data_t body = {0};
	
	// Copy requested time
	long word;
	size_t copied = 0;
	if (args.rdi != 0) // If duration for sleep is not NULL
	{
		while (copied < sizeof(struct timespec))
		{
			errno = 0;
			word = ptrace(PTRACE_PEEKDATA, child, args.rdi + copied, NULL);
			if (errno)
				break;
			size_t copy_size = sizeof(word);
			if (copied + copy_size > sizeof(struct timespec))
				copy_size = sizeof(struct timespec) - copied;
			memcpy((uint8_t *)&body.usec + copied, &word, copy_size);
			copied += copy_size;
		}
	}

	// Copy remaining time if rem pointer is non-NULL
	body.rem_valid = 0;
	if (args.rsi != 0)
	{
		copied = 0;
		while (copied < sizeof(struct timespec))
		{
			errno = 0;
			word = ptrace(PTRACE_PEEKDATA, child, args.rsi + copied, NULL);
			if (errno)
				break;
			size_t copy_size = sizeof(word);
			if (copied + copy_size > sizeof(struct timespec))
				copy_size = sizeof(struct timespec) - copied;
			memcpy((uint8_t *)&body.rem + copied, &word, copy_size);
			copied += copy_size;
		}
		body.rem_valid = 1;
	}

	// Write to trace file
	if (fwrite(&header, sizeof(header), 1, fp) != 1)
		perror("fwrite header failed");
	if (fwrite(&body, sizeof(body), 1, fp) != 1)
		perror("fwrite body failed");
	fflush(fp);
}

/**
 * record_recv
 *
 * Records a recv() or recvfrom() system call.
 * Captures:
 *   - Socket file descriptor
 *   - Buffer contents read from kernel
 *   - Number of bytes received
 *   - Return value and errno
 *   - Sequence number
 *
 * Ensures data received by the program is faithfully replayed.
 */

void record_recv(FILE *fp, syscall_type_t type, struct saved_args_t regs, int64_t ret_val, int saved_errno, pid_t child)
{

	record_header_t header;
	header.seq_num = global_seqno;
	header.type = type;
	header.ret_val = ret_val;
	header.saved_errno = saved_errno;
	header.body_len = sizeof(recv_data_t);

	recv_data_t body = {0};
	body.sockfd = regs.rdi;
	body.len = regs.rdx; // requested length
	body.flags = regs.r10;

	/* -------- Copy received data buffer -------- */
	if (ret_val > 0 && regs.rsi != 0)
	{
		size_t to_copy = ret_val;
		if (to_copy > sizeof(body.buf))
			to_copy = sizeof(body.buf);

		size_t copied = 0;
		while (copied < to_copy)
		{
			errno = 0;
			long word = ptrace(PTRACE_PEEKDATA,
							   child,
							   regs.rsi + copied,
							   NULL);
			if (errno)
				break;

			size_t copy_size = to_copy - copied;
			if (copy_size > sizeof(word))
				copy_size = sizeof(word);

			memcpy(body.buf + copied, &word, copy_size);
			copied += copy_size;
		}
	}

	/* -------- Copy src_addr + addrlen -------- */
	if (ret_val >= 0 && regs.r8 != 0 && regs.r9 != 0)
	{
		errno = 0;
		long len_word = ptrace(PTRACE_PEEKDATA, child, regs.r9, NULL);
		if (!errno)
		{
			socklen_t actual_len;
			memcpy(&actual_len, &len_word, sizeof(socklen_t));

			if (actual_len > sizeof(body.src_addr))
				actual_len = sizeof(body.src_addr);

			body.addrlen = actual_len;

			size_t copied = 0;
			while (copied < actual_len)
			{
				errno = 0;
				long word = ptrace(PTRACE_PEEKDATA,
								   child,
								   regs.r8 + copied,
								   NULL);
				if (errno)
					break;

				size_t copy_size = actual_len - copied;
				if (copy_size > sizeof(word))
					copy_size = sizeof(word);

				memcpy(body.src_addr + copied, &word, copy_size);
				copied += copy_size;
			}
		}
	}

	/* -------- Write record -------- */
	fwrite(&header, sizeof(header), 1, fp);
	fwrite(&body, sizeof(body), 1, fp);
	fflush(fp);
}

int main(int argc, char *argv[])
{
	pid_t child;

	int status;

	child = fork();

	if (child == 0)
	{
		/* The child process sets the parent process to trace it. Kernel marks the child as ptrace-enabled */
		ptrace(PTRACE_TRACEME, 0, NULL, NULL); 

		/* Replace the child process with the user-provided target program which needs to be traced. 
		* Arguments passed would be all arguments starting from argv[1]. Since this process has called PTRACE_TRACEME, 
		* after the process images is replaced, the process is stopped and a SIGTRAP signal is send to the child by the kernel
		*/
		execv(argv[1], &argv[1]); 

		/* Terminate the child process if execv() fails. Reaches here only if execv was not successful 
		 * since a successful execv() never returns
		 */
		exit(1); 
	}

	/* The parent process (tracer) waits for the child to stop immediately after execv(), at which
	 * point the kernel has delivered a SIGTRAP because the child previously called
	 * PTRACE_TRACEME. This ensure that the child has not executed any user instructions in the
	 * target program yet.
	 */

	waitpid(child, &status, 0);

	/* PTRACE_O_TRACESYSGOOD makes syscall entry/exit stops distinguishable
	 * from real SIGTRAP and other signals by setting bit 7 in the signal number
	   (SIGTRAP | 0x80). (For info: SIGTRAP == 5)
	 */

	ptrace(PTRACE_SETOPTIONS, child, 0, PTRACE_O_TRACESYSGOOD);

	/*
	 * Opens the file in binary mode to write the serialized syscall trace of the tracee.
	 * The trace includes syscall arguments, return values, errno (if any), and any data
	 * written to user space by the kernel during syscall execution.
	 * If opening the file fails, the tracer exits and terminates the child process.
	 */

	FILE *trace_file = fopen("trace.bin", "wb");

	if (!trace_file)
	{
		perror("fopen trace.bin");
		kill(child, SIGKILL);	 // make sure child does not keep running
		waitpid(child, NULL, 0); // reap child to avoid zombie
		exit(1);
	}

	/* A flag used to indicate if currently processing syscall entry (0)/exit (1) */
	int in_syscall = 0;
	long current_syscall = -1; // Stores the syscall number being processed
	struct saved_args_t args_entry;

	/* This is the tracing loop which will run until the traced child process exits
	 * This loop repeatedly resumes the traced child process and waits for ptrace stop events. It
	 * distinguishes syscall entry and exit stops using PTRACE_SYSCALL and PTRACE_O_TRACESYSGOOD,
	 * retrieves register state at each stop, forwards real signals to preserve correct program
	 * behavior, and processes syscall arguments and return values in a structured manner until
	 * the child process exits.
	 */
	while (1)
	{
		/* Resumes the child process and requests that it stop at the next
		 * syscall entry or syscall exit. From the tracer's perspective, the tracee
		 * will appear to have been stopped by the receipt of a SIGTRAP. The tracee will still
		 * continue to stop for other signals like SIGINT, SIGSEGV, SIGTERM etc.
		 */
		ptrace(PTRACE_SYSCALL, child, 0, 0);

		/* Blocks the tracer until the child changes state
		 * syscall entry/exit, signal delivery or process exit
		 */

		waitpid(child, &status, 0);

		/* Checks whether the child process has terminated normally.
		 * If so, exit the tracing loop
		 */

		if (WIFEXITED(status))
			break;

		/* Ensures the child is stopped (not running or exited)
		 * before attempting to inspect its state.
		 */
		if (!WIFSTOPPED(status))
			continue;

		// Extracts the signal number that caused the child to stop.
		int sig = WSTOPSIG(status);

		/* Filters out non-syscall stops. SIGTRAP | 0X80 uniquely identifies
		 * syscall entry/exit stops when PTRACE_O_TRACESYSGOOD is enabled.
		   Forwards real signals (e.g., SIGINT, SIGSEGV) to the child and resumes syscall
		   tracing.
		 */

		if (sig != (SIGTRAP | 0X80))
		{

			ptrace(PTRACE_SYSCALL, child, 0, sig);
			continue;
		}

		// Declares a structure to hold the child’s CPU register state at the syscall stop.
		struct user_regs_struct regs;

		/* Retrieves the child’s register state, allowing inspection of syscall numbers,
		 * arguments, and return values.
		 */
		ptrace(PTRACE_GETREGS, child, 0, &regs);

		/* During system call entry, all argument register values are stored to a struct. The global sequence number which recorders the sequence number
		 * of the system call is incremented.
		 */
		if (!in_syscall)
		{

			// Extracts the syscall number from the architecture-specific register (org_rax on x86-64)
			current_syscall = regs.orig_rax;
			args_entry.rdi = regs.rdi;
			args_entry.rsi = regs.rsi;
			args_entry.rdx = regs.rdx;
			args_entry.r10 = regs.r10;
			args_entry.r8 = regs.r8;
			args_entry.r9 = regs.r9;
			global_seqno++;
			in_syscall = 1;
		}
		/* During system call exit, the return value is extracted from the appropriate register (rax on x86-64).
		 * Based on the syscall number recorded at entry, the corresponding record_* function is invoked to
		 * serialize and store the syscall details in the trace file.
		 */
		else
		{	

			int64_t ret = regs.rax;
			int saved_errno = 0;
			if (ret < 0)
				saved_errno = -ret;
			switch (current_syscall)
			{

			case __NR_socket:
				record_socket(trace_file, SYS_TYPE_SOCKET, args_entry, ret, saved_errno);
				break;

			case __NR_fcntl:
				record_fcntl(trace_file, SYS_TYPE_FCNTL, args_entry, ret, saved_errno);
				break;
			case __NR_setsockopt:
				record_setsockopt(trace_file, SYS_TYPE_SETSOCKOPT, args_entry, ret, saved_errno, child);
				break;
			case __NR_bind:
				record_bind(trace_file, SYS_TYPE_BIND, args_entry, ret, saved_errno, child);
				break;
			case __NR_listen:
				record_listen(trace_file, SYS_TYPE_LISTEN, args_entry, ret, saved_errno);
				break;
			case __NR_accept:
				record_accept(trace_file, SYS_TYPE_ACCEPT, args_entry, ret, saved_errno, child);
				break;
			case __NR_nanosleep:
				record_nanosleep(trace_file, SYS_TYPE_NANOSLEEP, args_entry, ret, saved_errno, child);
				break;
			case __NR_recvfrom:
				record_recv(trace_file, SYS_TYPE_RECV, args_entry, ret, saved_errno, child);
				break;
			}
			in_syscall = 0;
		}
	}
	fclose(trace_file);
}
