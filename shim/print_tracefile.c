/* This program receives a system call trace file as its argument and 
 * prints all records including header and its data 
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "pretty_printer.h"
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
            print_trace_record(&header, payload);
    	}
    	if (payload) 
    		free(payload);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <trace_file>\n", argv[0]);
        return 1;
    }
    read_trace_file(argv[1]);
    return 0;
}
