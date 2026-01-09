/*
 ============================================================================
 Name        : send_client.c
 Description : A client program which would connect to a TCP server running on port 8090 at machine whose ipaddress is provided during runtime. The client sends a string to the server and then terminates. It also sleeps before connect() and recv() to test the blocking/non-blocking behavior of accept() and recv() at the server side
 ============================================================================
 */

#include<sys/types.h>
#include<string.h>
#include<sys/socket.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<netdb.h>
#include<arpa/inet.h>

#define PORT 8090

int main(int argc, char *argv[])
{
	int status;
	struct sockaddr_in server_addr;
    	server_addr.sin_family = AF_INET;
    	server_addr.sin_port = htons(PORT);
   
	/************ Creating the socket *************/
    	int sockdesc = socket(AF_INET, SOCK_STREAM, 0);

    	if(sockdesc < 0)
    	{
		perror("Socket creation failed");
		exit(1);
    	}

    	const char* server_ip = argv[1];
    	if(inet_pton(AF_INET, server_ip , &server_addr.sin_addr) <=0)
    	{
		perror("\nInvaid address \n");
	    	exit(1);
    	}
   

	/* Sleep for 1 second before attempting to connect.
	 * This ensures the server's accept() loop has a chance to execute
	 * while no connection is pending, verifying the non-blocking
	 * EWOULDBLOCK/EAGAIN error handling. */
	printf("\nSleeping for 1 second before connect()\n");
        usleep(1000000);

    	/*********** Connecting to the server socket *********/
    	printf("Connecting to server at %s:%d .. \n", server_ip, PORT);

    	if (connect(sockdesc, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
	{
        	perror("\nConnection Failed \n");
        	return -1;
    	}
	
	/* Delay sending data for 1 second to test the server's recv() behavior. 
	 * If the server's recv() is blocking, it will wait here for the data. 
	 * If non-blocking, recv() should handler EWOULDBLOCK OR EAGAIN error 
	 * before this message even arrives. */
	
	printf("\nSleeping for 1 second before send()\n");
	usleep(1000000);
    	char message[] = "Hello from the client!";
    
    	int sendflag = send(sockdesc, message, strlen(message), 0);

    	if (sendflag == -1)
    	{
		perror("Send failed");
    	}
    	else
    	{
		printf("Sent %d bytes to the server\n", sendflag);
    	}


    	// 6. Cleanup
    	close(sockdesc);
    	return 0;
}
