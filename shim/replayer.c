/**
 * replayer.c
 *
 * The replayer program executes a target program (e.g., a server under analysis)
 * under ptrace to monitor its system calls. It maintains a counter to track the
 * sequence number of each system call invoked by the program.
 *
 * At each system call entry, the replayer sends the current sequence number and
 * the system call identifier to the mutator program through pipes.
 *
 * Depending on the response from the mutator:
 *   - If the mutator indicates a match, the replayer receives the corresponding
 *     system call record and replays it using the recorded data (e.g., modifying
 *     return value or errno as instructed by the mutator).
 *   - If there is no match, the replayer allows the system call to execute
 *     normally. This is typically used for calls like write() to the console.
 * 	The replayer continues this process until the target program exits.
 */
#include <stdint.h>
#include <sys/socket.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/ptrace.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>

#include "trace_defs.h"

static uint64_t global_seqno = 0;

/* copy_to_child() copies len bytes from the tracer’s address space into the
 * memory of a traced process (child) at virtual address addr.
 */
void copy_to_child(pid_t child, unsigned long addr, const void *src, size_t len)
{
	size_t offset = 0;

	while (offset < len)
	{
		errno = 0;

		unsigned long dst_addr = addr + offset;
		// Read's tracee's memory at address dst_addr
		long word = ptrace(PTRACE_PEEKDATA, child, dst_addr, NULL);
		if (word == -1 && errno)
		{
			perror("PTRACE_PEEKDATA");
			return;
		}

		size_t copy = sizeof(long);
		if (offset + copy > len)
			copy = len - offset;

		memcpy((char *)&word, (const char *)src + offset, copy);

		// Overwrite tracee's memory at address dst_addr with word. This supports partial overwrites (where word < 8 bytes)
		if (ptrace(PTRACE_POKEDATA, child, dst_addr, word) == -1)
		{
			perror("PTRACE_POKEDATA");
			return;
		}

		offset += copy;
	}
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

		/* Replace the child process with the user-provided target program which needs to be traced. Arguments
		 * passed would be all arguments starting from argv[1]. Since this process has called PTRACE_TRACEME, after
		 * the process images is replaced, the process is stopped and a SIGTRAP signal is send to the child by the kernel
		 */

		execv(argv[3], &argv[3]);

		/* Terminate the child process if execv() fails. Reaches here only if execv was not successful since a successful execv() never returns */
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

	/* argv[2] is "pipe_to_mutator", argv[3] is "pipe_from_mutator" */
	int pipe_to_mutator = open(argv[1], O_WRONLY);
	int pipe_from_mutator = open(argv[2], O_RDONLY);

	if (pipe_to_mutator < 0 || pipe_from_mutator < 0)
	{
		perror("Replayer pipe open failed");
		kill(child, SIGKILL);	 // make sure child does not keep running
		waitpid(child, NULL, 0); // reap child to avoid zombie
		exit(1);
	}

	/* A flag used to indicate if currently processing syscall entry (0)/exit (1) */
	int in_syscall = 0;
	int skip_this_syscall = 0;
	/* Stores the syscall number being processed */
	long current_syscall = -1;

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

		/* Extracts the signal number that caused the child to stop. */
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

		/* Declares a structure to hold the child’s CPU register state at the syscall stop. */
		struct user_regs_struct regs;

		/* Retrieves the child’s register state, allowing inspection of syscall numbers,
		 * arguments, and return values. This is not supported in all architectures.
		 * Another alternative might be to use PTRACE_PEEKUSER to extract each register value
		 * orig_eax = ptrace(PTRACE_PEEKUSER, pid, 8 * ORIG_RAX, NULL)
		 */
		ptrace(PTRACE_GETREGS, child, 0, &regs);

		/* During system call entry, the global sequence number which recorders the sequence number
		 * of the system call is incremented. The replayer sends the sequence number and the system
		 * call to the mutator. Based on the response from the mutator, it either skips the syscall
		 * (if matched) or allows normal execution (if not matched).
		 */
		if (!in_syscall)
		{

			/* Extracts the syscall number from the architecture-specific register (org_rax on x86-64) */
			current_syscall = regs.orig_rax;
			global_seqno++;
			syscall_type_t sys_type;

			/* This is used to get an architecture-independent number for
			 * each system call. This could be later changed to include all/most
			 * system calls through a a map
			 */
			switch (current_syscall)
			{

			case __NR_socket:
				sys_type = SYS_TYPE_SOCKET;
				break;
			case __NR_fcntl:
				sys_type = SYS_TYPE_FCNTL;
				break;
			case __NR_setsockopt:
				sys_type = SYS_TYPE_SETSOCKOPT;
				break;
			case __NR_bind:
				sys_type = SYS_TYPE_BIND;
				break;
			case __NR_listen:
				sys_type = SYS_TYPE_LISTEN;
				break;
			case __NR_clock_nanosleep:
				sys_type = SYS_TYPE_NANOSLEEP;
				break;
			case __NR_accept:
				sys_type = SYS_TYPE_ACCEPT;
				break;
			case __NR_recvfrom:
				sys_type = SYS_TYPE_RECV;
				break;
			default:
				sys_type = -1;
				break;
			}

			/* Send mutator the sequence number and syscall number */
			replay_req_t req =
				{
					.seq_num = global_seqno,
					.syscall_type = sys_type};

			write(pipe_to_mutator, &req, sizeof(req));

			replay_resp_t resp;
			read(pipe_from_mutator, &resp, sizeof(resp));

			if (resp.match)
			{
				/* --- SKIP syscall --- */
				regs.orig_rax = -1;
				ptrace(PTRACE_SETREGS, child, 0, &regs);
				skip_this_syscall = 0;
			}
			else
			{
				skip_this_syscall = 1;
			}
			in_syscall = 1;
		}
		else
		{
			/* 	At syscall exit, if the system call was marked to be skipped and replayed, the replayer receives the system call information from the
			 *  mutator and replays the system call according to the received data.
			 */
			if (!skip_this_syscall)
			{
				record_header_t header;
				read(pipe_from_mutator, &header, sizeof(header));

				uint8_t payload[header.body_len];
				if (header.body_len > 0)
				{
					read(pipe_from_mutator, payload, header.body_len);
				}
				switch (header.type)
				{
				case SYS_TYPE_ACCEPT:
				{
					accept_data_t *body = (accept_data_t *)payload;
					/* Copy addr */
					if (regs.rsi && body->addrlen > 0)
					{
						copy_to_child(child, regs.rsi, body->addr, body->addrlen);
					}

					/* Copy addrlen */
					if (regs.rdx)
					{
						copy_to_child(child, regs.rdx, &body->addrlen, sizeof(socklen_t));
					}

					/* Inject fd */
					regs.rax = body->newfd;
					break;
				}

				case SYS_TYPE_NANOSLEEP:
				{
					nanosleep_data_t *body = (nanosleep_data_t *)payload;
					if (body->rem_valid && regs.rsi)
					{
						copy_to_child(child, regs.rsi, &body->rem, sizeof(struct timespec));
					}
					break;
				}

				case SYS_TYPE_RECV:
				{
					recv_data_t *body = (recv_data_t *)payload;
					/* Copy recv buffer */
					if (header.ret_val > 0 && regs.rsi)
					{
						copy_to_child(child, regs.rsi, body->buf, header.ret_val);
					}

					/* Copy src_addr */
					if (regs.r8 && body->addrlen > 0)
					{
						copy_to_child(child, regs.r8, body->src_addr, body->addrlen);
					}

					/* Copy addrlen */
					if (regs.r9)
					{
						copy_to_child(child, regs.r9, &body->addrlen, sizeof(uint32_t));
					}
					break;
				}
				default:
					break;
				}
				/*
				 * Inject the recorded (or mutated) return value into the traced process.
				 *
				 * The return value of a system call on x86-64 is stored in the RAX register.
				 * For failed system calls, the kernel reports errors by placing the negated
				 * errno value in RAX (i.e., -errno). For successful calls, RAX contains the
				 * actual return value.
				 *
				 * If the recorded return value indicates an error (ret_val < 0), the replayer
				 * sets RAX to the negated saved errno to accurately simulate a failed system
				 * call. Otherwise, RAX is set to the recorded return value.
				 *
				 * The modified register state is written back to the traced process using
				 * PTRACE_SETREGS, ensuring that the target program observes the injected
				 * return value when execution resumes.
				 */
				if (header.ret_val < 0)
					regs.rax = -header.saved_errno;
				else
					regs.rax = header.ret_val;

				ptrace(PTRACE_SETREGS, child, 0, &regs);
			}

			in_syscall = 0;
		}
	}
}
