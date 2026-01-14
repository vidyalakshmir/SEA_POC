#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "trace_defs.h"

// Definitions (Must match Recorder/Replayer)
// Use packed attribute to ensure C doesn't add padding between members
/*typedef struct __attribute__((packed)) {
    uint32_t length;      // TotalLen
    uint16_t type;        // Type
    int64_t  ret_val;     // Ret
    int32_t  saved_errno; // Err
} record_header_t;
*/

//#define SYS_TYPE_RECV 2 // Match your schema
#define ECONNRESET_VAL 104

void read_trace_file(const char *trace_path)
{
    FILE *trace_f = fopen(trace_path, "rb");
    if (!trace_f) {
        perror("Failed to open trace file");
        exit(1);
    }
    
    record_header_t header;
    
    while (fread(&header, sizeof(record_header_t), 1, trace_f) == 1) 
    {
    	uint32_t payload_size = header.body_len;
    	uint8_t *payload = NULL;

    	if (payload_size > 0) 
    	{
        	payload = malloc(payload_size);
        	if (!payload) 
        	{ 
        		perror("malloc"); 
        		exit(1); 
        	}
	        if (fread(payload, payload_size, 1, trace_f) != 1) 
	        {
            		perror("fread payload");
	 	        free(payload);
	                break;
        	}
    	}

    	switch (header.type) 
    	{
        	case SYS_TYPE_SOCKET:
        	    printf("SOCKET : %lld %d\n", (long long)header.ret_val, header.saved_errno);
        	    break;
        	case SYS_TYPE_FCNTL:
        	    printf("FCNTL   : %lld %d\n", (long long)header.ret_val, header.saved_errno);
        	    break;
        	case SYS_TYPE_SETSOCKOPT:
        	    printf("SETSOCKOPT   : %lld %d\n", (long long)header.ret_val, header.saved_errno);
        	    break;
        	case SYS_TYPE_BIND:
        	    printf("BIND   : %lld %d\n", (long long)header.ret_val, header.saved_errno);
        	    break;
        	case SYS_TYPE_LISTEN:
        	    printf("LISTEN   : %lld %d\n", (long long)header.ret_val, header.saved_errno);
        	    break;
        	case SYS_TYPE_ACCEPT:
        	    printf("ACCEPT   : %lld %d\n", (long long)header.ret_val, header.saved_errno);
        	    break;
        	case SYS_TYPE_NANOSLEEP:
        	    printf("NANOSLEEP   : %lld %d\n", (long long)header.ret_val, header.saved_errno);
        	    break;
        	case SYS_TYPE_RECV:
        	    printf("RECV   : %lld %d\n", (long long)header.ret_val, header.saved_errno);
        	    break;
        	// handle other syscalls here
    	}

    	if (payload) 
    		free(payload);
    }
}

void run_mutator(const char *trace_path) {
    FILE *trace_f = fopen(trace_path, "rb");
    if (!trace_f) {
        perror("Failed to open trace file");
        exit(1);
    }

    // Standard input/output are our pipes
    int pipe_in = STDIN_FILENO;
    int pipe_out = STDOUT_FILENO;

    while (1) {
    
    	replay_req_t req;

        /* Read request */
        ssize_t n = read(pipe_in, &req, sizeof(req));
        if (n <= 0)
            break;
            

        // 2. Peek at the trace file
        long pos = ftell(trace_f);
        
        record_header_t header;
        if (fread(&header, sizeof(header), 1, trace_f) != 1) {
            replay_resp_t resp = { .match = 0 };
            write(pipe_out, &resp, sizeof(resp));
            clearerr(trace_f);	 // Reset EOF for potential future reads
            continue;
        }
   
            
            // 3. Validation Logic
            if (req.seq_num == header.seq_num &&
            req.syscall_type == header.type) 
            {
                // MATCH: Read the rest of the payload
                uint32_t payload_size = header.body_len;
    		uint8_t *payload = NULL;

    		if (payload_size > 0) 
    		{
        		payload = malloc(payload_size);
        		if (!payload) 
        		{ 
        			perror("malloc"); 
        			exit(1); 
        		}
	        	if (fread(payload, payload_size, 1, trace_f) != 1) 
	        	{
            			perror("fread payload");
	 	        	free(payload);
	                	break;
        		}
    		}
                // --- MUTATION: Change recvfrom errno ---
                if (header.type == SYS_TYPE_RECV) {
                    header.ret_val = -1;
                    header.saved_errno = EWOULDBLOCK;
                    header.body_len = 0;   // no payload on error
	            free(payload);
                    payload = NULL;
                }
                
                // Send Match Signal (1)
                replay_resp_t resp = { .match = 1 };
                write(pipe_out, &resp, sizeof(resp));
                
                // Send Header + Payload
                write(pipe_out, &header, sizeof(header));	
                if (payload_size > 0) 
                {
                    write(pipe_out, payload, payload_size);
                }
                free(payload);

            } 
            else 
            {
                // NO MATCH: Rewind trace file pointer
                fseek(trace_f, pos, SEEK_SET);
                replay_resp_t resp = { .match = 0 };
                write(pipe_out, &resp, sizeof(resp));
            }

    }
    fclose(trace_f);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <trace_file>\n", argv[0]);
        return 1;
    }
    //read_trace_file(argv[1]);
    run_mutator(argv[1]);
    return 0;
}
