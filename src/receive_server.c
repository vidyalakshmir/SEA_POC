/*
 ============================================================================
 Name        : receive_server.c
 Description : 	This program implements a simple server that creates a non-blocking listening socket, binds to port 8090
		and listens on it. The server creates a listening socket, marks it as non-blocking, and then invokes bind()
		and listen(). It subsequently enters a loop in which it calls accept() to wait for incoming connections.
		The accept() is non-blocking; if no client connects, the server sleeps for 1 second and loops back to accept()
		for the next connection. Once the client connects, the server’s accept() call succeeds and returns a client
		socket. The server then invokes recv() on this socket. If recv() succeeds - that is, if data has already been
		sent by the client - the server reads the message, prints it, and exits normally. If recv() returns -1 indicating
		a failure, the server prints the error code and error description and and exits. Such a failure may occur either
		because the client has not yet sent any data (in the case of a non-blocking socket, where recv() sets errno to
		be EWOULDBLOCK or EAGAIN) or due to other error conditions.

		On platforms where client sockets can inherit non-blocking behavior from the listening socket, passing the
		--patched flag enables the server to handle EWOULDBLOCK and EAGAIN error for recv() by sleeping 1 second
		and retrying recv() rather than exiting.

 ============================================================================
 */

#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define PORT 8090
#define MAX_CONNECTIONS 5
#define BUF_SIZE 1024

static const char *errno_name(int err)
{
	switch (err)
	{
	case EWOULDBLOCK:
		return "EWOULDBLOCK";
	case EBADF:
		return "EBADF";
	case ECONNREFUSED:
		return "ECONNREFUSED";
	case EFAULT:
		return "EFAULT";
	case EINTR:
		return "EINTR";
	case EINVAL:
		return "EINVAL";
	case ENOMEM:
		return "ENOMEM";
	case ENOTCONN:
		return "ENOTCONN";
	case ENOTSOCK:
		return "ENOTSOCK";
	default:
		return "UNKNOWN_ERRNO";
	}
}

int main(int argc, char *argv[])
{
	int patched = 0;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--patched") == 0)
		{
			patched = 1;
		}
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
			printf("Usage: %s [--patched]\n", argv[0]);
			printf("  --patched    Run patched server\n");
			printf("  -h, --help   display this help message\n");
			return 0;
		}
		else
		{
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
			return 1;
		}
	}

	/* Create a non-blocking listening socket. A socket can be made non-blocking through the following means:
	 * 1. Setting the type argument of socket() to be the bitwise OR of SOCK_NOBLOCK
	 * int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	 * 2. Setting O_NONBLOCK status flag on the socket file descriptor using fcntl()
	 Since macOS doesn't support setting non-blocking socket through socket(), we use fcntl() here
	 */
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (server_fd < 0)
	{
		perror("SERVER: Socket failed");
		exit(1);
	}

	int flags = fcntl(server_fd, F_GETFL, 0);
	if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		perror("SERVER: fcntl failed");
		exit(1);
	}

	/* When a server is shutdown, the OS keeps that port reserved for 1–2 minutes to ensure
	 * no late-arriving packets from the old connection get mixed up with a new one.
	 * When the server is tested successively, setting SO_REUSEADDR allows the server
	 * to bind to the port immediately after a restart, bypassing the "Address already in use" error
	 */

	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT);

	/* Binds the socket with the port number specified by PORT */
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		perror("SERVER: Bind failed");
		exit(1);
	}

	/* Listens for incoming client connections */
	if (listen(server_fd, MAX_CONNECTIONS) < 0)
	{
		perror("SERVER: Listen failed");
		exit(1);
	}

	printf("SERVER: Server listening on port %d\n", PORT);
	socklen_t addrlen = sizeof(address);
	int client_sockdesc = -1;
	char buffer[BUF_SIZE];
	while (1)
	{
		printf("\nSERVER: Waiting for client to connect\n");
		/* Accepts client connections */
		struct sockaddr_in client_address;
		socklen_t client_addrlen = sizeof(client_address);
		client_sockdesc = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen);
		if (client_sockdesc < 0)
		{
			/* Since the listening socket is non-blocking, if no connection is pending,
			 * accept() returns -1 and sets errno to EWOULDBLOCK or EAGAIN.
			 * The server sleeps for 1 second to throttle the loop, preventing a "busy-wait"
			 * scenario that would consume 100% CPU usage.
			 */
			if (errno == EWOULDBLOCK || errno == EAGAIN)
			{
				printf("\nSERVER: Sleeping for 1 second\n");
				fflush(stdout);
				usleep(1000000); // Sleep for 1 second
				continue;
			}
			else
			{
				perror("SERVER: Accept failed");
				exit(1);
			}
		}
		else
			break;
	}
	printf("\nSERVER: Connection accepted! Waiting to receive data from the client!\n");
	memset(buffer, 0, sizeof(buffer));

	/* Receives data from client. If successful, recv() returns
	 * the number of bytes of data received. How an unsuccessful
	 * recv() is handled differs between the flawed and the patched
	 * server. In case of a patched server, an error code equal to EWOULDBE or EAGAIN
	 * which indicates that the client socket is non-blocking and that there
	 * was no data send from the client yet, the server sleeps for 1 second
	 * and invokes recv() again. In all other cases the patched server exits printing the error.
	 * While the flaowed server, just prints the error and exits not handling any error explicitly.
	 *
	 */

	int recvflag = 0;
	while (1)
	{
		recvflag = recv(client_sockdesc, buffer, sizeof(buffer) - 1, 0);
		if (recvflag <= 0)
		{
			if (patched)
			{
				if (errno == EWOULDBLOCK || errno == EAGAIN)
				{
					printf("\nSleeping for 1 second\n");
					fflush(stdout);
					usleep(1000000); // Sleep for 1 second
					continue;
				}
			}
			int err = errno;
			printf("\nSERVER: recv failed: errno=%d (%s): %s\n",
				   err,
				   errno_name(err),
				   strerror(err));
			printf("\nSERVER: Bye!!)");
			exit(1);
		}
		else
			break;
	}

	/* Adding the null terminator at the end of received data to avoid
	 * information leak or segmentation fault while printing the data using printf().
	 * This is because recv() does not null-terminate the data it receives while printf
	 * reads the buffer till it encounters a null character (\0).
	 */
	buffer[recvflag] = '\0';
	printf("\nSERVER: Received data : %s\n", buffer);
	printf("\nSERVER: Bye!!");
	fflush(stdout);
	close(client_sockdesc);
	close(server_fd);
	return 0;
}
