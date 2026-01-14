/*
 ============================================================================
 Name        : receive_server.c
 Description : This simple server program creates a non-blocking socket(), binds to port 8090,
 	       and listens on it. The accept() function is invoked to receive client connections,
	       and once it succeeds, the server receives data from the client, closes the client
	       socket and stops. The server checks the error code if recv() fails, and handles 
	       error code EWOULDBLOCK / EAGAIN, which handles case when the client socket is non-blocking 
	       and there is no data send by the client. The accept() is non-blocking; if no client connects, 
	       the server sleeps for 1 second and loops back to accept() for the next connection. 
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


int main()
{
	/* Create a non-blocking listening socket. A socket can be made non-blocking through the following means:
	 * 1. Setting the type argument of socket() to be the bitwise OR of SOCK_NOBLOCK
	 * int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	 * 2. Setting O_NONBLOCK status flag on the socket file descriptor using fcntl()
	 Since macOS doesn't support setting non-blocking socket through socket(), we use fcntl() here
	 */
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	
	if(server_fd < 0)
	{
		perror("Socket failed");
		exit(1);
	}
	
	int flags = fcntl(server_fd, F_GETFL, 0);
	if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
                perror("fcntl failed");
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

	//Binds the socket with the port number specified by PORT
	if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		perror("Bind failed");
		exit(1);
	}

	//Listens for incoming client connections
	if(listen(server_fd, MAX_CONNECTIONS) < 0)
	{
		perror("Listen failed");
		exit(1);
	}

	printf("Server listening on port %d\n",PORT);
	socklen_t addrlen = sizeof(address);
	int client_sockdesc = -1; 
	char buffer[BUF_SIZE];
	while(1)
	{
		printf("\nWaiting for client to connect\n");
		//Accepts client connections
		struct sockaddr_in client_address;
		socklen_t client_addrlen = sizeof(client_address);
		client_sockdesc = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen);
		printf("\nReturn value of accept() is %d. ErrorCode is %d\n",client_sockdesc, errno);		
		if(client_sockdesc < 0)
		{
			/* * Handling Non-Blocking Accept:
			   * Since the listening socket is non-blocking, if no connection is pending,
			   * accept() returns -1 and sets errno to EWOULDBLOCK or EAGAIN.
			   * The server sleeps for 1 second to throttle the loop, preventing a "busy-wait"
			   * scenario that would consume 100% CPU usage.
 			*/
			if(errno == EWOULDBLOCK || errno == EAGAIN)
			{
				printf("\nSleeping for 1 second\n");
				fflush(stdout);
				usleep(1000000); //Sleep for 1 second
				continue;
			}
			else
			{
				perror("Accept failed");
				exit(1);
			}
		}
		else
			break;
	}
	printf("\nConnection accepted! Waiting to receive data from the client!\n");
	memset(buffer, 0, sizeof(buffer)); // Clear the buffer
					   //
	/* Receives data from client. If successful, recv() returns
	 * the number of bytes of data received. If not successful,
	 * it returns -1. In case of error code equal to EWOULDBE or EAGAIN
	 * which indicates that the client socket is non-blocking and that there
	 * was no data send from the client yet, the server sleeps for 1 second
	 * and invokes recv() again.
	 */
	int recvflag = 0;
	while(1)
	{
		recvflag = recv(client_sockdesc, buffer, sizeof(buffer) - 1,0);
		if(recvflag <= 0)
		{
			if(errno == EWOULDBLOCK || errno == EAGAIN)
			{
				printf("\nSleeping for 1 second\n");
				fflush(stdout);
				usleep(1000000); //Sleep for 1 second
				continue;
			}
			else
			{
				perror("Receive failed");
				exit(1);
			}
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
	printf("\nReceived data : %s\n",buffer);
	close(client_sockdesc);
	close(server_fd);
	return 0;	
}
