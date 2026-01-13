#ifndef TRACE_DEFS_H
#define TRACE_DEFS_H

#include <stdint.h>

typedef enum 
{
	SYS_TYPE_SOCKET,
	SYS_TYPE_FCNTL,
	SYS_TYPE_BIND,
	SYS_TYPE_LISTEN,
	SYS_TYPE_SETSOCKOPT,
	SYS_TYPE_ACCEPT,
	SYS_TYPE_RECV
} syscall_type_t;

// The Common Header: Every record has this
typedef struct {
    uint64_t seq_num;		//For single-threaded this works. For multi-threaded, add timestamp
    syscall_type_t type;
    int64_t ret_val;
    int32_t saved_errno;
    uint32_t body_len;
} record_header_t;

typedef struct {
    int domain;
    int type;
    int protocol;
} socket_data_t;

typedef struct {
    int fd;        // file descriptor
    int cmd;       // fcntl command
    int64_t arg;   // optional argument, 0 if unused
} fcntl_data_t;

typedef struct {
    int sockfd;
    int level;
    int optname;
    uint32_t optlen;
    uint8_t optval[64]; 
} setsockopt_data_t;

typedef struct {
    int sockfd;
    uint32_t addrlen;
    uint8_t addr[128];  // length enough to hold sockaddr_in, sockaddr_in6, sockaddr_un
} bind_data_t;

typedef struct {
    int sockfd;
    int backlog;
} listen_data_t;

typedef struct {
    int sockfd;            // listening socket
    int newfd;             // returned accepted socket
    uint32_t addrlen;      // length of addr which is returned after accept() is invoked
    uint8_t addr[128];     // address of peer socket (sockaddr_in, sockaddr_in6, sockaddr_un)
} accept_data_t;


// The "Universal" Event Wrapper
typedef struct {
    record_header_t header;
    union {
        socket_data_t socket_pkt;
        uint64_t      raw_args[6]; // Fallback for generic syscalls
    } body;
} syscall_event_t;

#endif
