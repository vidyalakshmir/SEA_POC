#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char* argv[])
{
	pid_t child = fork();
	struct user_regs_struct regs;
	int status;

	int is_exit = 0;
	if(child == 0)
	{
		ptrace(PTRACE_TRACEME, 0, NULL, NULL);
		execv(argv[1], &argv[1]);
	}
	else
	{
		while(1)
		{
			wait(&status);
			if(WIFEXITED(status)) break;

			ptrace(PTRACE_GETREGS, child, NULL, &regs);
			long orig_rax = regs.orig_rax;

			switch(orig_rax)
			{
				case SYS_socket:
						printf("socket() called\n");
					break;
				case SYS_bind:
						printf("bind() called\n");
					break;
				case SYS_listen:
						printf("listen() called\n");
					break;
				case SYS_accept:
					if(is_exit)	
					{
						printf("accept() finished with return value %lld. Changing to %d\n", regs.rax, 4);
                                                regs.rax = 4;
                                                ptrace(PTRACE_SETREGS, child, NULL, &regs);
						is_exit = !is_exit;
					}
					else
					{
						printf("accept() called\n");
						is_exit = !is_exit;
					}
					break;
				 case SYS_recvfrom:
                                        if(is_exit)
                                        {
                                                printf("recv() finished with return value %lld. Changing to %d\n", regs.rax, EWOULDBLOCK);
                                                regs.rax = -EWOULDBLOCK;
                                                ptrace(PTRACE_SETREGS, child, NULL, &regs);
						is_exit = !is_exit;

                                        }
                                        else
                                        {
                                                printf("recv() about to execute()..\n");
						is_exit = !is_exit;
                                        }
                                        break;
				case SYS_sendto:
						printf("send() called\n");
					break;
			}
			ptrace(PTRACE_SYSCALL, child, NULL, NULL);

		}
	}
}
