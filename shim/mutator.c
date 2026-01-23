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
 * Special handling is applied to certain system calls (e.g., recvfrom):
 *   - The mutator modifies the return value and errno in the trace record to simulate
 *     anomalies (e.g., sets return value to -1 and errno to EWOULDBLOCK).
 *   - When mutated values are sent to the replayer, diverges from the originally recorded 
 *     execution path. The deterministic replay of the program is resumed only when the
 *     program comes back to the point where it deviated. 
 * 
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

void run_mutator(const char *trace_path)
{
    FILE *trace_f = fopen(trace_path, "rb");
    if (!trace_f)
    {
        perror("Failed to open trace file");
        exit(1);
    }

    // Standard input/output are our pipes
    int pipe_in = STDIN_FILENO;
    int pipe_out = STDOUT_FILENO;

    /* Flag indicating that the program takes a different route than the one recorded 
     * due to providing mutated values.
     * pause_flag=0 indicates that the program is taking the recorded path
     * pause_flag=1 indicates that the program has deferred from the recorded route and 
     * the program is continued as usual without replay
     */
    int pause_flag = 0;
    while (1)
    {

        fprintf(stderr, "\nDEBUG: pause_flag=%d", pause_flag);
        replay_req_t req;

        /* Read request */
        ssize_t n = read(pipe_in, &req, sizeof(req));
        if (n <= 0)
            break;

        /* Peek at the trace file and store the position of this record */
        long pos = ftell(trace_f);

        record_header_t header;

        /* When there is an error reading the header of the system call trace, 0 is send to the replayer 
         * indicated to continue the system call */
        if (fread(&header, sizeof(record_header_t), 1, trace_f) != 1)
        {
            fprintf(stderr, "\nDEBUG: Received %ld %d. Error reading header. Sending signal 0", req.seq_num, req.syscall_type);
            replay_resp_t resp = {.match = 0};
            write(pipe_out, &resp, sizeof(resp));
            clearerr(trace_f);
            continue;
        }

        /* When pause_flag=1, indicating that the program is taking a path different than the recorded path,
         * send the signal 2 to the replayer instructs the replayer to the continue executing the system call
         */

        if (req.syscall_type != SYS_TYPE_RECVFROM && pause_flag)
        {
            replay_resp_t resp = {.match = 2};
            fprintf(stderr, "\nDEBUG: Received %ld %d. Sending signal 2", req.seq_num, req.syscall_type);
            write(pipe_out, &resp, sizeof(resp));
            fseek(trace_f, pos, SEEK_SET);
            continue;
        }

        /*  Validation Logic */
        if (req.seq_num == header.seq_num &&
            req.syscall_type == header.type)
        {
            fprintf(stderr, "\nDEBUG: Received %ld %d. MATCH", req.seq_num, req.syscall_type);
            replay_resp_t resp;

            /* This indicates the first time recvfrom system call is invoked. At this
             * point, mutated values are send to the replayer and a signal of 2 indicates
             * that the program is entering a path different than the recorded path
             */
            if (req.syscall_type == SYS_TYPE_RECVFROM && !pause_flag)
            {
                /* Send Match Signal (1) */
                resp.match = 2;
                fprintf(stderr, "\nDEBUG: Sending RESP %d", resp.match);
            }
            else
            {
                /* Send Match Signal (1) */
                resp.match = 1;
                fprintf(stderr, "\nDEBUG: Sending RESP %d", resp.match);
            }

            write(pipe_out, &resp, sizeof(resp));

            /* MATCH: Read the rest of the payload */
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
            /* MUTATION: Change recvfrom errno and return value at its first invokation in the program (pause_flag=0) */
            if (req.syscall_type == SYS_TYPE_RECVFROM && !pause_flag)
            {
                header.ret_val = -1;
                header.saved_errno = EWOULDBLOCK;
                header.body_len = 0; // no payload on error
                payload_size = 0;
                free(payload);
                payload = NULL;
                fprintf(stderr, "\nMUTATOR: Changing recvfrom errno");
            }

            /* Send Header + Payload
            write(pipe_out, &header, sizeof(header));
            fprintf(stderr, "\nDEBUG: Send header and payload");

            if (payload_size > 0)
            {
                write(pipe_out, payload, payload_size);
            }
            free(payload);
            
            /*
                In case of recvfrom system call, we toggle the pause_flag and
                rewinds the trace file pointer when mutated values are sent
            */
            if (req.syscall_type == SYS_TYPE_RECVFROM)
            {
                if (!pause_flag)
                    fseek(trace_f, pos, SEEK_SET);
                pause_flag = !pause_flag;
            }
        }
        else
        {

            /* NO MATCH: Rewind trace file pointer */
            fseek(trace_f, pos, SEEK_SET);
            replay_resp_t resp = {.match = 0};
            write(pipe_out, &resp, sizeof(resp));
            fprintf(stderr, "\nMUTATOR NO MATCH: Send 0");
        }
    }
    fclose(trace_f);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <trace_file>\n", argv[0]);
        return 1;
    }
    run_mutator(argv[1]);
    return 0;
}
