#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define PORT 8090
#define MAX_CONNECTIONS 5

/*
 * This simple server program creates a non-blocking socket(), binds to port 8090,
 * and listens on it. The accept() function is invoked to receive client connections,
 * and once it succeeds, the server receives data from the client, closes the client
 * socket, and then loops back to accept() for the next connection. 
 * The accept() is non-blocking; if no client connects, the server sleeps for 1 second.
 * Behavior of recv() differs between 
 */

int main()
{
	// Create a non-blocking listening socket
	int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if(server_fd < 0)
	{
		perror("Socket failed");
		exit(1);
	}
	//Allow port reuse to avoid "Bind failed" after restarts
	
	int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT);

	//Binds the socket with the port number 8090
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
	char buffer[1024];
	while(1)
	{
		printf("\nWaiting for client to connect\n");
		//Accepts client connections
		client_sockdesc = accept(server_fd, (struct sockaddr *)&address, &addrlen);
		printf("Return value of accept() is %d",client_sockdesc);		
		if(client_sockdesc < 0)
		{
			if(errno == EWOULDBLOCK || errno == EAGAIN)
			{
				printf("Sleeping for client. ErrorCode: %d\n", errno);
				fflush(stdout);
				usleep(1000000); //Sleep for 10 seconds
				continue;
			}
			else
			{
				perror("Accept failed");
				exit(1);
			}
		}
		printf("\nConnection accepted! Waiting to receive data from the client!\n");
		memset(buffer, 0, sizeof(buffer)); // Clear the buffer
		//Receives data from client
		int recvflag = recv(client_sockdesc, buffer, sizeof(buffer),0);
		if(recvflag <= 0)
		{
			perror("Receive failed");
			exit(1);
		}
		printf("\nReceived data : %s\n",buffer);
		close(client_sockdesc);
	}
	close(server_fd);
	return 0;

	
}
