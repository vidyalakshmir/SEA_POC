/**
 * mutator.c
 *
 * The mutator program reads system call traces from a binary trace file (trace.bin)
 * produced by the recorder. It communicates with the replayer program through
 * named pipes (FIFOs). The replayer sends requests via the pipe, providing the
 * sequence number and system call identifier of each system call being executed.
 *
 * For each request received from the replayer:
 *   - The mutator reads the next trace record from trace.bin and compares the
 *     sequence number and system call number with the request.
 *   - If the trace does not match, the mutator sends a signal (0) back through
 *     the pipe, indicating that the replayer should execute the system call normally.
 *   - If the trace matches, the mutator sends the complete system call record
 *     back through the pipe.
 *
 * Special handling is applied to certain system calls (e.g., recv):
 *   - The mutator modifies the return value and errno in the trace record to simulate
 *     anomalies (e.g., sets return value to -1 and errno to EWOULDBLOCK).

 * This mechanism enables controlled injection of anomalies into the program’s
 * execution and allows observation of how the program behaves under such conditions.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "trace_defs.h"


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
        if (fread(&header, sizeof(record_header_t), 1, trace_f) != 1) {
            fprintf(stderr,"\nDEBUG: Received %ld %d. Error reading header. Sending signal 0", req.seq_num, req.syscall_type);
            replay_resp_t resp = { .match = 0 };
            write(pipe_out, &resp, sizeof(resp));
            clearerr(trace_f);	 // Reset EOF for potential future reads
            continue;
        }
   
            
            // 3. Validation Logic
            if (req.seq_num == header.seq_num &&
            req.syscall_type == header.type) 
            {
                fprintf(stderr, "\nDEBUG: Received %ld %d. MATCH", req.seq_num, req.syscall_type);

                // Send Match Signal (1)
                replay_resp_t resp = { .match = 1 };
                fprintf(stderr, "\nDEBUG: Sending RESP %d", resp.match);

                write(pipe_out, &resp, sizeof(resp));
                
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
                    payload_size = 0;
	                free(payload);
                    payload = NULL;
                }
                
                
                // Send Header + Payload
                write(pipe_out, &header, sizeof(header));
                fprintf(stderr, "\nDEBUG: Send header and payload");
	
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
    run_mutator(argv[1]);
    return 0;
}
