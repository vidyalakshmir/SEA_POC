/*
 * This module provides the 'Pretty Printing' logic for system call traces. 
 * It deserializes binary records into C structures and decodes complex data
 * such as bitwise flags, network socket structures, and data buffers into a 
 * human-readable format. Each entry is displayed with its sequence number 
 * (to preserve the chronological order of execution), return value, and error status.
 */
#include<stdio.h>
#include <fcntl.h>
#include <sys/socket.h>  
#include <netinet/in.h> 
#include <netinet/tcp.h> 
#include "pretty_printer.h"

void print_header(const record_header_t* hdr)
{
	printf("SeqNo: %04lu Type: %-2d | Ret: %-3ld | Err: %-2d | BodyLen: %-3u\n",
           hdr->seq_num, 
           hdr->type, 
           (long)hdr->ret_val, 
           hdr->saved_errno, 
           hdr->body_len);
}

const char* domain_to_str(int domain) {
    switch (domain) {
        case 1:  return "AF_UNIX";
        case 2:  return "AF_INET";
        case 10: return "AF_INET6";
        case 16: return "AF_NETLINK";
        default: return "AF_UNKNOWN";
    }
}

const char* type_to_str(int type) {
    // Mask out modern Linux flags like SOCK_NONBLOCK (0x800) or SOCK_CLOEXEC (0x80000)
    int base_type = type & 0xf; 
    switch (base_type) {
        case 1: return "SOCK_STREAM";
        case 2: return "SOCK_DGRAM";
        case 3: return "SOCK_RAW";
        default: return "SOCK_UNKNOWN";
    }
}

const char* fcntl_cmd_to_str(int cmd) {
    switch (cmd) {
        case F_DUPFD:         return "F_DUPFD";
        case F_GETFD:         return "F_GETFD";
        case F_SETFD:         return "F_SETFD";
        case F_GETFL:         return "F_GETFL";
        case F_SETFL:         return "F_SETFL";
        case F_SETLK:         return "F_SETLK";
        case F_SETLKW:        return "F_SETLKW";
        case F_GETLK:         return "F_GETLK";
        case F_DUPFD_CLOEXEC: return "F_DUPFD_CLOEXEC";
        default:              return "F_UNKNOWN";
    }
}

const char* socket_level_to_str(int level) {
    switch (level) {
        case SOL_SOCKET:   return "SOL_SOCKET";
        case IPPROTO_TCP:  return "IPPROTO_TCP";
        case IPPROTO_IP:   return "IPPROTO_IP";
        case IPPROTO_IPV6: return "IPPROTO_IPV6";
        default:           return "SOL_UNKNOWN";
    }
}

// Example for SOL_SOCKET level
const char* sockopt_name_to_str(int optname) {
    switch (optname) {
        case SO_REUSEADDR: return "SO_REUSEADDR";
        case SO_KEEPALIVE: return "SO_KEEPALIVE";
        case SO_BROADCAST: return "SO_BROADCAST";
        case SO_RCVBUF:    return "SO_RCVBUF";
        case SO_SNDBUF:    return "SO_SNDBUF";
        default:           return "OPT_UNKNOWN";
    }
}

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

void print_sockaddr(const uint8_t* raw_addr, uint32_t len) {
    if (len == 0 || raw_addr == NULL) {
        printf("none");
        return;
    }

    uint16_t family;
    memcpy(&family, raw_addr, sizeof(family));
    char addr_str[INET6_ADDRSTRLEN];

    if (family == AF_INET) {
        struct sockaddr_in sin;
        size_t copy_len = (len < sizeof(sin)) ? len : sizeof(sin);
        memcpy(&sin, raw_addr, copy_len);

        inet_ntop(AF_INET, &(sin.sin_addr), addr_str, sizeof(addr_str));
        printf("%s:%d", addr_str, ntohs(sin.sin_port));
    } 
    else if (family == AF_INET6) {
        struct sockaddr_in6 sin6;
        size_t copy_len = (len < sizeof(sin6)) ? len : sizeof(sin6);
        memcpy(&sin6, raw_addr, copy_len);

        inet_ntop(AF_INET6, &(sin6.sin6_addr), addr_str, sizeof(addr_str));
        printf("[%s]:%d", addr_str, ntohs(sin6.sin6_port));
    }
    else if (family == AF_UNIX) {
        struct sockaddr_un sun;
        size_t copy_len = (len < sizeof(sun)) ? len : sizeof(sun);
        memcpy(&sun, raw_addr, copy_len);

        int path_len = (int)len - sizeof(sun.sun_family);
        if (path_len > 0) {
            // Use 'unix:' prefix to distinguish from IP addresses
            printf("unix:%.*s", path_len, sun.sun_path);
        } else {
            printf("unix:(abstract)");
        }
    }
    else {
        printf("fam:%d", family);
    }
}

void print_trace_record(const record_header_t* hdr, const void* body)
{
	if(!hdr || !body)
		return;

	print_header(hdr);

	switch(hdr->type)
	{
		case SYS_TYPE_SOCKET:
		{
			socket_data_t* d = (socket_data_t*)body;
			printf("[SOCKET] Domain: %s(%d) Type: %s(%d) Protocol: %d", 
           		domain_to_str(d->domain), d->domain,
           		type_to_str(d->type & 0xF), d->type & 0xF,
           		d->protocol);
    		
    
    		// Check for modern Linux flags in the 'type' field
    		if (d->type & (SOCK_NONBLOCK | SOCK_CLOEXEC)) 
    		{
        		printf(" Flags:%s%s",
               		(d->type & SOCK_NONBLOCK) ? "NONBLOCK " : "",
               		(d->type & SOCK_CLOEXEC)  ? "CLOEXEC" : "");
        	}
        	printf("\n");
    	
            break;
		}

		case SYS_TYPE_FCNTL:
		{
			fcntl_data_t* d = (fcntl_data_t*) body;
			printf("[FCNTL] FD: %d | Cmd: %s(%d) | Arg: ", 
           d->fd, fcntl_cmd_to_str(d->cmd), d->cmd);

    		// Inline Context-aware argument printing
    		if (d->cmd == F_SETFL) 
    		{
        		printf("0x%lx%s%s%s", (unsigned long)d->arg,
               (d->arg & O_NONBLOCK) ? "[NONBLOCK]" : "",
               (d->arg & O_APPEND)   ? "[APPEND]"   : "",
               (d->arg & O_ASYNC)    ? "[ASYNC]"    : "");
    		} 
    		else if (d->cmd == F_SETFD || d->cmd == F_DUPFD) 
    		{
        		printf("%ld", d->arg);
    		} 
    		else if (d->cmd == F_SETLK || d->cmd == F_GETLK || d->cmd == F_SETLKW) 
    		{
        		printf("ptr:%p(struct flock)", (void*)d->arg);
    		} 
    		else 
    		{
        		printf("%ld", d->arg);
    		}

    		printf("\n");
    		break;
		}

		case SYS_TYPE_SETSOCKOPT:
		{
			setsockopt_data_t* d = (setsockopt_data_t*)body;
    
    		printf("[SETSOCKOPT] FD: %d | Level: %s(%d) | Opt: %s(%d) | Val: ",
           		d->sockfd, 
           		socket_level_to_str(d->level), d->level,
           		sockopt_name_to_str(d->optname), d->optname);

    		if (d->optlen == sizeof(int)) 
    		{
        		int val;
        		memcpy(&val, d->optval, sizeof(int));
        		printf("%d", val);
    		} 
    		else if (d->optlen == sizeof(struct linger) && d->optname == SO_LINGER) 
    		{
        		struct linger *l = (struct linger*)d->optval;
        		printf("{onoff:%d, linger:%d}", l->l_onoff, l->l_linger);
    		} 
    		else 
    		{
        		printf("[Hex:%ub]", d->optlen);
    		}

    		printf("\n"); 
			break;
		}

		case SYS_TYPE_BIND:
		{
			bind_data_t* d = (bind_data_t*)body;
            printf("[BIND] FD: %d | Addr: ", d->sockfd);
            print_sockaddr(d->addr, d->addrlen);
            printf("\n");
			break;
		}

		case SYS_TYPE_LISTEN:
		{
			listen_data_t* d = (listen_data_t*)body;
			if (hdr->ret_val == 0) 
			{
        		printf("[LISTEN] FD: %d, Backlog: %d (Success)\n", 
                d->sockfd, d->backlog);
    		} 
    		else 
    		{
        		printf("[LISTEN] FD: %d, Backlog: %d (FAILED: %s)\n", 
                d->sockfd, d->backlog, strerror(hdr->saved_errno));
    		}
			break;
		}

		case SYS_TYPE_ACCEPT:
		{
			accept_data_t* d = (accept_data_t*)body;
			if (hdr->ret_val >= 0) 
			{
        		printf("[ACCEPT] Listening FD:%d -> New FD:%ld | Remote:", 
               			d->sockfd, (long)hdr->ret_val);
        		print_sockaddr(d->addr, d->addrlen);
    		} 
    		else 
    		{
        		printf("[ACCEPT] Listening FD:%d | (FAILED: %s (%d))", d->sockfd, strerror(hdr->saved_errno), hdr->saved_errno);
    		}

    		printf("\n"); 
			break;
		}

		case SYS_TYPE_NANOSLEEP:
		case SYS_TYPE_CLOCK_NANOSLEEP:
		{
			nanosleep_data_t* d = (nanosleep_data_t*)body;
    		printf("[SLEEP] Requested Time:%lu us(%.2f ms)", d->usec, d->usec / 1000.0);

		    // If interrupted, show the remaining time inline
    		if (d->rem_valid) 
    		{
        		printf(" | INTERRUPTED: %ld.%09lds remaining", d->rem_sec, d->rem_nsec);
    		} 
    		else 
    		{
        		printf(" | Success");
    		}

   			printf("\n"); 
			break;
		}

		case SYS_TYPE_RECV:
		{
			recv_data_t* d = (recv_data_t*)body;
			printf("[RECV] FD: %d, Received: %ld bytes", d->sockfd, hdr->ret_val);

    		if (hdr->ret_val > 0) 
    		{
        
        		int actual_data = (hdr->ret_val < 4096) ? hdr->ret_val : 4096;
        
        		printf("  Data Received: \"");
        		for (int i = 0; i < actual_data; i++) 
        		{
           	 		uint8_t c = d->buf[i];
            
            		// 1. Handle common whitespace
            		if (c == '\n')      
            			printf("\\n");
            		else if (c == '\r') 
            			printf("\\r");
            		else if (c == '\t') 
            			printf("\\t");
            
            		// 2. Print printable ASCII (32 is space, 126 is ~)
            		else if (c >= 32 && c <= 126) 
            		{
                		printf("%c", c);
            		}
            
            		// 3. Print everything else as a dot
            		else 
            		{
                		printf(".");
            		}
        		}
        		printf("\"\n");
    		}
    		else
    		{
    			printf("(FAILED: %s (%d))", strerror(hdr->saved_errno), hdr->saved_errno);
    		}
			break;
		}

		default:
			break;
	}
	printf("\n\n");
}