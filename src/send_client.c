/*
 ============================================================================
 Name        : client.c
 Description : A client program which would connect to a TCP server running on port 8090 at machine whose ipaddress is provided during runtime. The client prints the information send from the server on the screen.
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
        struct addrinfo hints;
        struct addrinfo *servinfo, *p;
        char ipstr[INET6_ADDRSTRLEN];
        
        memset(&hints,0,sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        
        status = getaddrinfo(argv[1],"8089",&hints, &servinfo);        
        if(status!=0)
        {
                printf("\n%s\n",gai_strerror(status));
        }


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
    
    /*********** Connecting to the server socket *********/
    printf("Connecting to server at %s:%d .. \n", server_ip, PORT);

    if (connect(sockdesc, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("\nConnection Failed \n");
        return -1;
    }

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
